/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_PROCESSOR_H
#define _ASM_WASM_PROCESSOR_H

#include <asm/page.h>
#include <asm/ptrace.h>

#ifndef __ASSEMBLY__

struct task_struct;

struct thread_struct {
	/* Kernel thread entry point, run by wasm_thread_start(). */
	int (*fn)(void *);
	void *fn_arg;
	/*
	 * Set by a successful exec of a wasm program: when fn returns, the
	 * thread enters user mode by calling the program's _start(user_sp).
	 */
	int user;
	unsigned long user_sp;
	/*
	 * Set by exec, checked when the system call that caused it returns:
	 * the old program must not be resumed. See wasm_syscall().
	 */
	int exec_pending;
	/*
	 * A program that binfmt_wasm has loaded but the host has not yet
	 * instantiated; that happens on the thread that will run it.
	 */
	void *exec_image;
	unsigned long exec_len, exec_base, exec_table;
	/* Thread-borrowing vfork, see arch/wasm/kernel/vfork.c. */
	struct task_struct *vfork_parent;	/* child: whose thread I run on */
	struct task_struct *vfork_child;	/* parent: who runs on my thread */
	int vfork_request;			/* parent: clone in progress */
	long vfork_pid;				/* parent: result for vfork() */
	int exit_pending, exit_code;		/* child: exit after leaving */
	/* fork(), see wasm_fork() in arch/wasm/kernel/process.c. */
	int fork_request;			/* parent: clone in progress */
	int fork_child;				/* child: not yet started */
	/*
	 * A signal handler left by throwing an exception (longjmp), which
	 * the host is holding; see signal.c.
	 */
	int user_thrown;
	/* A host thread has been requested for this task. */
	int spawned;
	/*
	 * 1 while this task owns the (single) CPU, 0 while it is switched
	 * out. The task's host thread sleeps on this word with
	 * memory.atomic.wait32. See __switch_to().
	 */
	int run;
};

/* init_task runs on the boot thread: already "spawned" and running. */
#define INIT_THREAD { .spawned = 1, .run = 1 }

void cpu_relax(void);

#define task_pt_regs(tsk) \
	((struct pt_regs *)((unsigned long)task_stack_page(tsk) + THREAD_SIZE) - 1)

/* No MMU: user programs (later) share the kernel's address space. */
#define TASK_SIZE		0xfffff000UL
#define TASK_UNMAPPED_BASE	0

#define KSTK_EIP(tsk)	(0)
#define KSTK_ESP(tsk)	(0)

static inline unsigned long __get_wchan(struct task_struct *p)
{
	return 0;
}

#endif /* __ASSEMBLY__ */
#endif
