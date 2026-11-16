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
