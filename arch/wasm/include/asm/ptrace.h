/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_PTRACE_H
#define _ASM_WASM_PTRACE_H

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__

/* FIXME(wasm): there is no user mode yet. */
#define user_mode(regs)			(0)
#define instruction_pointer(regs)	(0UL)
#define user_stack_pointer(regs)	(0UL)
#define profile_pc(regs)		instruction_pointer(regs)

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->syscall_ret;
}

#endif /* __ASSEMBLY__ */
#endif
