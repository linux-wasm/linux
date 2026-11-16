/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_WASM_POSIX_TYPES_H
#define _UAPI_ASM_WASM_POSIX_TYPES_H

/*
 * On wasm32 clang defines size_t as "unsigned long" (not "unsigned int" as
 * asm-generic assumes for 32-bit), and the compiler's builtin prototypes
 * (memcpy, strlen, ...) use that type.
 */
typedef unsigned long	__kernel_size_t;
typedef long		__kernel_ssize_t;
typedef long		__kernel_ptrdiff_t;
#define __kernel_size_t __kernel_size_t

#include <asm-generic/posix_types.h>

#endif
