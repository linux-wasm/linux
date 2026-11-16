/* SPDX-License-Identifier: GPL-2.0 */
/* Derived from arch/wasm/include/asm/page.h in tombl/linux. */
#ifndef _ASM_WASM_PAGE_H
#define _ASM_WASM_PAGE_H

#include <linux/const.h>
#include <vdso/page.h>

/* Linear memory starts at address 0; physical == virtual. */
#define PAGE_OFFSET	_AC(0, UL)
#define PHYS_OFFSET	_AC(0, UL)

#ifndef __ASSEMBLY__

#include <linux/pfn.h>

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg) \
	({ (void)(vaddr); (void)(pg); clear_page(page); })
#define copy_user_page(to, from, vaddr, pg) \
	({ (void)(vaddr); (void)(pg); copy_page(to, from); })

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)
#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#define __pa(virt)	((unsigned long)(virt))
#define __va(phys)	((void *)(unsigned long)(phys))

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_virt(page)	__va(page_to_pfn(page) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#endif /* __ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif
