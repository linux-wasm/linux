/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linker-script replacement, shared by sections_head.c and sections_tail.c.
 *
 * wasm-ld has no linker scripts, so nothing defines the __foo_start/__foo_end
 * symbols that generic code expects around its link-time tables. Instead:
 *
 *  - sections_head.o is the first object on the link line. For every table
 *    section it emits a zero-sized object carrying the start symbol.
 *  - sections_tail.o is the last object and emits the end symbols.
 *
 * With --no-merge-data-segments wasm-ld keeps one output segment per section
 * name, concatenates the input pieces in link order, and creates the segments
 * in order of first appearance. So the order of the definitions in
 * sections_head.c *is* the memory layout, and tables that generic code walks
 * across several sections (initcall levels, scheduler classes) are
 * contiguous. arch/wasm/tools/check-sections.sh verifies this on every link.
 */
#ifndef _WASM_KERNEL_SECTIONS_H
#define _WASM_KERNEL_SECTIONS_H

#include <linux/compiler.h>

/* A zero-sized, pointer-aligned marker object @sym in section @sec. */
#define WASM_SECTION_MARK(sym, sec)					\
	extern char sym[];						\
	char sym[0] __used __section(sec) __aligned(sizeof(void *))

#endif
