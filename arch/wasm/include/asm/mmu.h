/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_MMU_H
#define _ASM_WASM_MMU_H

#include <linux/list.h>

typedef struct {
	unsigned long end_brk;
	/*
	 * The private regions this address space has in common with other
	 * processes since a fork(): struct wasm_fork_member.mm_node, see
	 * arch/wasm/mm/fork.c.
	 */
	struct list_head fork_members;
} mm_context_t;

#endif
