/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_MMU_CONTEXT_H
#define _ASM_WASM_MMU_CONTEXT_H

#include <linux/list.h>
#include <linux/mm_types.h>
#include <asm-generic/mm_hooks.h>

struct task_struct;
struct vm_area_struct;

/* arch/wasm/mm/fork.c */
void wasm_fork_switch_mm(struct mm_struct *next);
void wasm_fork_destroy_mm(struct mm_struct *mm);
int arch_nommu_dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm);
void arch_nommu_delete_vma(struct mm_struct *mm, struct vm_area_struct *vma);

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	INIT_LIST_HEAD(&mm->context.fork_members);
	return 0;
}
#define init_new_context init_new_context

static inline void destroy_context(struct mm_struct *mm)
{
	wasm_fork_destroy_mm(mm);
}
#define destroy_context destroy_context

/*
 * There is one address space, so there is nothing to switch, except for
 * memory that processes related by fork() have at the same addresses:
 * bring in what @next has there.
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	if (next != &init_mm && !list_empty(&next->context.fork_members))
		wasm_fork_switch_mm(next);
}

#include <asm-generic/mmu_context.h>

#endif
