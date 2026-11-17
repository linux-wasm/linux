// SPDX-License-Identifier: GPL-2.0
/*
 * vfork() by lending the child the parent's host thread.
 *
 * A vfork child continues in its parent's call stack until it calls
 * execve() or _exit(), and the parent is suspended meanwhile. A child on a
 * host thread of its own cannot do that (see copy_thread()), but nothing
 * forces it onto one: since the parent must not run anyway, the child can
 * simply use the parent's thread, wasm call stack included.
 *
 *  wasm_vfork()        Create the child (sharing the address space) without
 *                      a thread, put the parent to sleep and let the calling
 *                      thread carry on as the child: the system call
 *                      returns 0.
 *  execve(), exit()    in such a child end the borrowing (wasm_vfork_leave):
 *                      the child is given a host thread of its own, where
 *                      it starts the new program or exits, and the calling
 *                      thread turns back into the parent. To user space the
 *                      system call returns the child's pid; libc uses that
 *                      to jump back to where vfork() was called (setjmp/
 *                      longjmp, see musl's vfork).
 *
 * While a child runs on a borrowed thread it must not die there, because
 * the parent's frames are underneath: signals are held back until it has
 * left (wasm_exit_to_user_mode()).
 *
 * The thread changes hands in __switch_to() via wasm_thread_handoff: both
 * functions below sleep, and when schedule() returns, the same thread and
 * stack are running the other task.
 */
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <asm/processor.h>
#include "wasm.h"

/*
 * "current" is read from a wasm global that the handoff changes; make sure
 * it is read again rather than reused from before schedule().
 */
static noinline struct task_struct *wasm_current_now(void)
{
	return current;
}

long wasm_vfork(void)
{
	struct kernel_clone_args args = {
		.flags		= CLONE_VM,
		.exit_signal	= SIGCHLD,
	};
	struct task_struct *parent = current;
	pid_t pid;

	/* One level only: a borrowed thread cannot be lent again. */
	if (parent->thread.vfork_parent)
		return -EAGAIN;

	parent->thread.vfork_request = 1;
	pid = kernel_clone(&args);
	parent->thread.vfork_request = 0;
	if (pid < 0)
		return pid;
	parent->thread.vfork_pid = pid;

	wasm_thread_handoff = parent->thread.vfork_child;
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();

	/* This thread is the child now. */
	WARN_ON(wasm_current_now() == parent);
	return 0;
}

/* Called by a vfork child on its parent's thread; returns in the parent. */
long wasm_vfork_leave(void)
{
	struct task_struct *child = current;
	struct task_struct *parent = child->thread.vfork_parent;

	child->thread.vfork_parent = NULL;
	parent->thread.vfork_child = NULL;
	/* The next switch to the child creates a host thread for it. */
	child->thread.spawned = 0;

	wasm_thread_handoff = parent;
	wake_up_process(parent);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();

	/* This thread is the parent again. */
	WARN_ON(wasm_current_now() != parent);
	wake_up_process(child);
	return wasm_current_now()->thread.vfork_pid;
}
