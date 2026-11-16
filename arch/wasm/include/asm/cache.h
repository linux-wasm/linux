/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_CACHE_H
#define _ASM_WASM_CACHE_H

/* The real cache line size is unknowable from inside wasm; assume 64. */
#define L1_CACHE_SHIFT	6
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#endif
