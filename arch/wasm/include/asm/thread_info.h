/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_THREAD_INFO_H
#define _ASM_WASM_THREAD_INFO_H

#include <asm/page.h>

/*
 * Only the "shadow stack" (address-taken locals) lives in linear memory;
 * return addresses and most locals are kept by the wasm engine.
 */
#define THREAD_SIZE_ORDER	2
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLY__

struct thread_info {
	unsigned long flags;
	int preempt_count;
};

#define INIT_THREAD_INFO(tsk)				\
{							\
	.flags		= 0,				\
	.preempt_count	= INIT_PREEMPT_COUNT,		\
}

#endif /* __ASSEMBLY__ */

#include <asm-generic/thread_info_tif.h>

/* Not using GENERIC_ENTRY (there is no syscall entry yet). */
#define TIF_SYSCALL_TRACE	16
#define _TIF_SYSCALL_TRACE	BIT(TIF_SYSCALL_TRACE)

#endif
