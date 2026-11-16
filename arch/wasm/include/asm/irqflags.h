/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_IRQFLAGS_H
#define _ASM_WASM_IRQFLAGS_H

/*
 * There is no way to asynchronously interrupt running wasm code, so
 * "interrupts" are purely cooperative: they are latched in a pending word
 * and delivered when interrupts get (re-)enabled or the CPU idles.
 */
#define ARCH_IRQ_DISABLED	0
#define ARCH_IRQ_ENABLED	1

extern unsigned long wasm_irq_flags;
extern unsigned int wasm_irq_pending;

void wasm_deliver_irqs(void);

static inline unsigned long arch_local_save_flags(void)
{
	return wasm_irq_flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	wasm_irq_flags = flags;
	if (flags == ARCH_IRQ_ENABLED && unlikely(wasm_irq_pending))
		wasm_deliver_irqs();
}

#include <asm-generic/irqflags.h>

#endif
