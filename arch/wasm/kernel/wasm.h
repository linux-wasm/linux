/* SPDX-License-Identifier: GPL-2.0 */
/* Declarations private to arch/wasm/kernel. */
#ifndef _WASM_KERNEL_WASM_H
#define _WASM_KERNEL_WASM_H

#include <linux/types.h>

struct task_struct;

/* entry.S -> C */
void wasm_start_kernel(void);
void wasm_thread_start(struct task_struct *task);

/* Export for the host, see asm/host.h. */
void wasm_raise_irq(unsigned int irq);

void wasm_console_init(void);

/* Absolute host clock time of the next timer event, 0 if none. */
extern u64 wasm_timer_deadline;

#endif
