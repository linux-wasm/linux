// SPDX-License-Identifier: GPL-2.0
/*
 * System call entry.
 *
 * A user program imports the function "linux"."syscall" with the wasm type
 * (i32 nr, i32 x 6) -> i32, and the host links that import directly to this
 * kernel instance's wasm_syscall export. A system call is therefore a plain
 * wasm call on the calling task's own host thread: no trap, no mode switch,
 * and the kernel continues on the kernel stack it left when it entered the
 * program. See asm/syscall_wrapper.h for how arguments map to the six slots.
 */
#include <linux/irqflags.h>
#include <linux/linkage.h>
#include <linux/mman.h>
#include <linux/ptrace.h>
#include <linux/resume_user_mode.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <asm/host.h>
#include <asm/processor.h>
#include <asm/unistd.h>
#include "wasm.h"

typedef asmlinkage long (*syscall_fn_t)(const struct pt_regs *regs);

/* sys_ni_syscall() takes no arguments; the table needs the common type. */
static asmlinkage long wasm_sys_ni(const struct pt_regs *regs)
{
	return -ENOSYS;
}
#define sys_ni_syscall wasm_sys_ni

#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)

#define __SYSCALL(nr, call) asmlinkage long call(const struct pt_regs *regs);
#include <asm/syscall_table_32.h>
#undef __SYSCALL

#define __SYSCALL(nr, call) [nr] = call,
static const syscall_fn_t sys_call_table[NR_syscalls] = {
	[0 ... NR_syscalls - 1] = wasm_sys_ni,
#include <asm/syscall_table_32.h>
};
#undef __SYSCALL
#undef sys_ni_syscall

SYSCALL_DEFINE6(mmap2, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	/* The offset is in 4096-byte units regardless of PAGE_SIZE. */
	if (pgoff & ((1UL << (PAGE_SHIFT - 12)) - 1))
		return -EINVAL;
	return ksys_mmap_pgoff(addr, len, prot, flags, fd,
			       pgoff >> (PAGE_SHIFT - 12));
}

/* FIXME(wasm): signal handlers are never run, so nothing returns from one. */
SYSCALL_DEFINE0(rt_sigreturn)
{
	return -ENOSYS;
}

/*
 * Work to do before going back to the user program.
 *
 * FIXME(wasm): signals with a handler are dequeued and dropped: there is no
 * way yet to call into the program at an arbitrary point. Signals whose
 * action is the default (terminate, stop, ...) are handled by get_signal().
 * Interrupted system calls are not restarted.
 */
static void wasm_exit_to_user_mode(struct pt_regs *regs)
{
	unsigned long flags;

	while ((flags = read_thread_flags()) &
	       (_TIF_NEED_RESCHED | _TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL |
		_TIF_NOTIFY_RESUME)) {
		if (flags & _TIF_NEED_RESCHED)
			schedule();

		if (flags & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL)) {
			struct ksignal ksig;

			get_signal(&ksig);
			restore_saved_sigmask();
		}

		if (flags & _TIF_NOTIFY_RESUME)
			resume_user_mode_work(regs);
	}

	switch (regs->syscall_ret) {
	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
	case -ERESTARTNOHAND:
	case -ERESTART_RESTARTBLOCK:
		regs->syscall_ret = -EINTR;
	}
}

long wasm_syscall(long nr, unsigned long a0, unsigned long a1,
		  unsigned long a2, unsigned long a3, unsigned long a4,
		  unsigned long a5)
{
	struct pt_regs *regs = task_pt_regs(current);

	/*
	 * Running user code cannot be interrupted, so this is the only
	 * point where a busy program lets timers and interrupts through.
	 */
	cpu_relax();

	regs->syscall_nr = nr;
	regs->syscall_args[0] = a0;
	regs->syscall_args[1] = a1;
	regs->syscall_args[2] = a2;
	regs->syscall_args[3] = a3;
	regs->syscall_args[4] = a4;
	regs->syscall_args[5] = a5;

	if ((unsigned long)nr < NR_syscalls)
		regs->syscall_ret = sys_call_table[nr](regs);
	else
		regs->syscall_ret = -ENOSYS;

	wasm_exit_to_user_mode(regs);

	/*
	 * After a successful execve the calling program no longer exists as
	 * far as the kernel is concerned, but its frames are still below us
	 * on the host stack. Have the host throw them away; control comes
	 * back in wasm_thread_start(), which enters the new program.
	 */
	if (unlikely(current->thread.exec_pending))
		wasm_host_user_unwind();

	return regs->syscall_ret;
}
