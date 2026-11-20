/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_IRQ_H
#define _ASM_WASM_IRQ_H

/*
 * Interrupts are bits in one pending word (arch/wasm/kernel/irq.c), so
 * there are at most 32 of them. IRQ 0 is the timer; the rest can be raised
 * by the host through the wasm_raise_irq export.
 */
#define NR_IRQS		32
#define WASM_IRQ_TIMER	0
#define WASM_IRQ_CONSOLE	1	/* part of the host interface, asm/host.h */

/* Device n of the host's virtio devices, see asm/host.h. */
#define WASM_IRQ_VIRTIO0	2
#define WASM_VIRTIO_MAX		(NR_IRQS - WASM_IRQ_VIRTIO0)

#include <asm-generic/irq.h>

#endif
