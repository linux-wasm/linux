// SPDX-License-Identifier: GPL-2.0
/*
 * Linker-script symbols that have no meaning on wasm.
 *
 * Code is not in linear memory: a function "address" is an index into the
 * indirect call table, so there is no text range, no separate init text and
 * no read-only data protection. All of these markers are zero-sized objects
 * in one section, i.e. they share one address and every range is empty.
 * Generic range checks (core_kernel_text(), is_kernel_rodata(), ...) are
 * therefore always false, and free_initmem() frees nothing.
 *
 * FIXME(wasm): init data is never freed.
 * FIXME(wasm): _sdata/_edata/__bss_start/__bss_stop are empty too (wasm-ld
 * offers no symbols to alias them to), so the "Memory:" boot line reports
 * 0K for kernel code, data and bss.
 */
#include "sections.h"

#define WASM_EMPTY_RANGE(start, end)				\
	WASM_SECTION_MARK(start, ".data..wasm_empty");		\
	WASM_SECTION_MARK(end, ".data..wasm_empty")

WASM_EMPTY_RANGE(_stext, _etext);
WASM_EMPTY_RANGE(_sinittext, _einittext);
WASM_EMPTY_RANGE(__init_begin, __init_end);
WASM_EMPTY_RANGE(__start_rodata, __end_rodata);
WASM_EMPTY_RANGE(__sched_text_start, __sched_text_end);
WASM_EMPTY_RANGE(__cpuidle_text_start, __cpuidle_text_end);
WASM_EMPTY_RANGE(_sdata, _edata);
WASM_EMPTY_RANGE(__bss_start, __bss_stop);
