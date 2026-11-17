// SPDX-License-Identifier: GPL-2.0
/*
 * Signal delivery.
 *
 * Other architectures run a handler by rewriting the saved user registers:
 * push a frame on the user stack, point the program counter at the handler
 * and let the handler "return" into rt_sigreturn. None of that exists on
 * wasm: there is no program counter to redirect and no way to enter a
 * function other than calling it.
 *
 * So the kernel calls the handler. Signals are only ever acted upon when a
 * system call is about to return (running wasm code cannot be interrupted),
 * and at that point the kernel is an ordinary callee of the program, on the
 * program's own host thread. The host is asked to call the handler through
 * the program's function table (user_signal); the handler runs on top of
 * the program's frames, exactly as if the system call stub had called it,
 * and when it returns the kernel carries on returning from the system call.
 *
 *  - The handler's "return" is a plain return; rt_sigreturn is not used.
 *    What sigreturn would do (restore the signal mask from the ucontext)
 *    happens here after the call.
 *  - A handler may make system calls, and those may deliver further
 *    signals: handlers nest on the host stack like any calls. The task has
 *    one pt_regs, so it is saved around the handler.
 *  - A handler may leave with longjmp() instead of returning. On wasm that
 *    is an exception, which must not unwind the kernel frames between the
 *    handler and the interrupted system call. The host catches it at the
 *    user_signal call; the kernel finishes its work and has it thrown again
 *    once it is back at the system call entry (entry.S).
 *  - A system call that has to be restarted is run again by the caller's
 *    loop (wasm_do_syscall()) once the handlers are done.
 *
 * FIXME(wasm): no alternate signal stack (SA_ONSTACK is ignored; the
 * handler runs on the stack the program was using). A program that never
 * makes a system call never sees a signal, not even SIGKILL.
 */
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/resume_user_mode.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <asm/host.h>
#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/unistd.h>
#include "wasm.h"

/* What a handler installed with SA_SIGINFO is given pointers to. */
struct wasm_sigframe {
	struct siginfo info;
	struct ucontext uc;
};

/* Handlers return by returning; nothing ever needs this. */
SYSCALL_DEFINE0(rt_sigreturn)
{
	return -ENOSYS;
}

/*
 * The first signal action after a system call decides whether the call is
 * restarted. @ksig is the handler about to run, or NULL if there is none.
 */
static bool wasm_syscall_restart(struct pt_regs *regs, struct ksignal *ksig)
{
	switch (regs->syscall_ret) {
	case -ERESTART_RESTARTBLOCK:
		if (!ksig) {
			regs->syscall_nr = __NR_restart_syscall;
			return true;
		}
		break;
	case -ERESTARTNOHAND:
		if (!ksig)
			return true;
		break;
	case -ERESTARTSYS:
		if (!ksig || (ksig->ka.sa.sa_flags & SA_RESTART))
			return true;
		break;
	case -ERESTARTNOINTR:
		return true;
	default:
		return false;
	}
	regs->syscall_ret = -EINTR;
	return false;
}

static void wasm_handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	struct wasm_sigframe frame;
	struct pt_regs saved = *regs;
	sigset_t mask;
	int ret;

	memset(&frame, 0, sizeof(frame));
	/* kernel_siginfo is siginfo without the padding. */
	memcpy(&frame.info, &ksig->info, sizeof(ksig->info));
	frame.uc.uc_stack.ss_flags = SS_DISABLE;
	frame.uc.uc_mcontext.regs = saved;
	frame.uc.uc_sigmask = *sigmask_to_save();

	/* Blocks the signal and sa_mask for the duration of the handler. */
	signal_setup_done(0, ksig, 0);

	ret = wasm_host_user_signal((unsigned long)ksig->ka.sa.sa_handler,
				    ksig->sig, &frame.info, &frame.uc);

	*regs = saved;

	switch (ret) {
	case WASM_SIGNAL_RETURNED:
		/* As sigreturn: the handler may have edited the mask. */
		mask = frame.uc.uc_sigmask;
		set_current_blocked(&mask);
		break;
	case WASM_SIGNAL_THREW:
		/* The mask stays as it is, as after a longjmp() anywhere. */
		current->thread.user_thrown = 1;
		break;
	default:
		/* The handler is not a function of the program. */
		force_sigsegv(ksig->sig);
		break;
	}
}

/*
 * Work to do before going back to the user program. Returns true if the
 * system call in @regs has to be run again.
 */
bool wasm_exit_to_user_mode(struct pt_regs *regs)
{
	unsigned long work = _TIF_NEED_RESCHED | _TIF_NOTIFY_RESUME;
	unsigned long flags;
	bool undecided = true;
	bool restart = false;

	/*
	 * A vfork child on its parent's thread must not be killed there
	 * (vfork.c), and its handlers are the parent's; its signals wait
	 * until it has a thread of its own.
	 */
	if (!current->thread.vfork_parent)
		work |= _TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL;

	while ((flags = read_thread_flags()) & work) {
		if (flags & _TIF_NEED_RESCHED)
			schedule();

		if (flags & work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL)) {
			struct ksignal ksig;
			bool handler = get_signal(&ksig);

			if (undecided) {
				restart = wasm_syscall_restart(regs,
							handler ? &ksig : NULL);
				undecided = false;
			}
			if (handler) {
				wasm_handle_signal(&ksig, regs);
				/* Leaving by exception: go at once. */
				if (current->thread.user_thrown)
					return false;
			} else {
				restore_saved_sigmask();
			}
		}

		if (flags & _TIF_NOTIFY_RESUME)
			resume_user_mode_work(regs);
	}

	/* Interrupted, but the signal is being held back (vfork child). */
	if (undecided) {
		switch (regs->syscall_ret) {
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
		case -ERESTARTNOHAND:
		case -ERESTART_RESTARTBLOCK:
			regs->syscall_ret = -EINTR;
		}
	}

	return restart;
}

/*
 * Called by the system call entry (entry.S) after wasm_do_syscall() has
 * returned: does a handler's exception still have to be passed on?
 */
int wasm_user_thrown(void)
{
	int thrown = current->thread.user_thrown;

	current->thread.user_thrown = 0;
	return thrown;
}
