/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_SYSCALL_H
#define _ASM_WASM_SYSCALL_H

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/ptrace.h>

/* FIXME(wasm): no syscall entry path exists yet; these are for generic code. */

static inline long syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->syscall_nr;
}

static inline void syscall_rollback(struct task_struct *task, struct pt_regs *regs)
{
}

static inline long syscall_get_error(struct task_struct *task, struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->syscall_ret) ? regs->syscall_ret : 0;
}

static inline long syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->syscall_ret;
}

static inline void syscall_set_return_value(struct task_struct *task, struct pt_regs *regs,
					    int error, long val)
{
	regs->syscall_ret = error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
					 unsigned long *args)
{
	memcpy(args, regs->syscall_args, sizeof(regs->syscall_args));
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return 0;
}

#endif
