/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_COMPILER_H
#define _ASM_WASM_COMPILER_H

/*
 * A wasm function cannot see its return address (clang rejects the builtin
 * for wasm32-unknown-unknown).
 * FIXME(wasm): _RET_IP_ and every other caller address is always 0.
 */
#define __builtin_return_address(level)	((void *)0)

#endif
