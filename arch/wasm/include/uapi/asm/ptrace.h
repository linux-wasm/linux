/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_WASM_PTRACE_H
#define _UAPI_ASM_WASM_PTRACE_H

#ifndef __ASSEMBLY__

/*
 * A wasm thread has no register file that the kernel can save or restore:
 * locals and the value stack live inside the engine. pt_regs only carries
 * what a (future) syscall entry path needs.
 */
struct pt_regs {
	long syscall_nr;
	unsigned long syscall_args[6];
	long syscall_ret;
};

#endif /* __ASSEMBLY__ */
#endif
