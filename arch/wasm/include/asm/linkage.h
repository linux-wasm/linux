/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_LINKAGE_H
#define _ASM_WASM_LINKAGE_H

/*
 * Code addresses do not exist in wasm: functions are not in linear memory
 * and a call's return address is invisible to the callee.
 * FIXME(wasm): _THIS_IP_/_RET_IP_ are always 0, so anything that reports a
 * caller (lockdep, slab debug, WARN) cannot name it.
 */
#define _THIS_IP_	(0UL)

/*
 * The generic cond_syscall() is a weak assembler alias to sys_ni_syscall.
 * wasm functions are typed, so an alias with a different signature is not
 * valid (and crashes clang 19). asm/syscall_wrapper.h provides COND_SYSCALL
 * as a typed weak C function instead; this must not be used.
 */
#define cond_syscall(x) _Static_assert(0, "use COND_SYSCALL on wasm")

#endif
