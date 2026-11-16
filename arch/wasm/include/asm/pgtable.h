/* SPDX-License-Identifier: GPL-2.0 */
/* Derived from arch/wasm/include/asm/pgtable.h in tombl/linux. */
#ifndef _ASM_WASM_PGTABLE_H
#define _ASM_WASM_PGTABLE_H

#include <asm/page.h>
#include <asm-generic/pgtable-nopmd.h>

/* No MMU: there are no page tables. These only satisfy generic code. */
#define swapper_pg_dir	((pgd_t *)0)

#define PAGE_NONE	__pgprot(0)
#define PAGE_SHARED	__pgprot(0)
#define PAGE_COPY	__pgprot(0)
#define PAGE_READONLY	__pgprot(0)
#define PAGE_KERNEL	__pgprot(0)

#define VMALLOC_START	0
#define VMALLOC_END	0xffffffff
#define KMAP_START	0
#define KMAP_END	0xffffffff

#endif
