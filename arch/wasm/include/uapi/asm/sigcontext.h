/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_WASM_SIGCONTEXT_H
#define _UAPI_ASM_WASM_SIGCONTEXT_H

#include <asm/ptrace.h>

struct sigcontext {
	struct pt_regs regs;
};

#endif
