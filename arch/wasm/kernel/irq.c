// SPDX-License-Identifier: GPL-2.0
/*
 * Cooperative interrupt delivery. The pending-word design follows
 * arch/wasm/kernel/irq.c in tombl/linux, reduced to one CPU and 32 IRQs.
 */
#include <linux/cpu.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqflags.h>
#include <linux/sched.h>
#include <asm/host.h>
#include <asm/irq_regs.h>
#include "wasm.h"

unsigned long wasm_irq_flags = ARCH_IRQ_DISABLED;

/* Written with atomics: the host may raise IRQs from any thread. */
unsigned int wasm_irq_pending;

u64 wasm_timer_deadline;

static void wasm_pend_irq(unsigned int irq)
{
	__atomic_fetch_or(&wasm_irq_pending, 1U << irq, __ATOMIC_SEQ_CST);
}

/*
 * Exported to the host: latch @irq and wake the CPU if it is idle. It is
 * delivered the next time the kernel enables interrupts or idles.
 */
void wasm_raise_irq(unsigned int irq)
{
	if (irq >= NR_IRQS)
		return;
	wasm_pend_irq(irq);
	__builtin_wasm_memory_atomic_notify((int *)&wasm_irq_pending, 1);
}

/* Latch the timer interrupt if its deadline has passed. */
static void wasm_timer_poll(void)
{
	u64 deadline = wasm_timer_deadline;

	if (deadline && wasm_host_clock_ns() >= deadline) {
		wasm_timer_deadline = 0;
		wasm_pend_irq(WASM_IRQ_TIMER);
	}
}

/* Called from arch_local_irq_restore() when enabling with IRQs pending. */
void wasm_deliver_irqs(void)
{
	static struct pt_regs dummy_regs;
	struct pt_regs *old_regs;
	unsigned int pending;

	while ((pending = __atomic_exchange_n(&wasm_irq_pending, 0,
					      __ATOMIC_SEQ_CST))) {
		wasm_irq_flags = ARCH_IRQ_DISABLED;
		old_regs = set_irq_regs(&dummy_regs);
		irq_enter();
		while (pending) {
			unsigned int irq = __builtin_ctz(pending);

			pending &= pending - 1;
			generic_handle_irq(irq);
		}
		irq_exit();
		set_irq_regs(old_regs);
		wasm_irq_flags = ARCH_IRQ_ENABLED;
	}
}

/*
 * Sleep the host thread until an interrupt is raised or the timer is due.
 * Called and returns with interrupts disabled; the caller enables them,
 * which delivers whatever became pending.
 */
void arch_cpu_idle(void)
{
	u64 deadline = wasm_timer_deadline;
	s64 timeout = -1;

	if (deadline) {
		u64 now = wasm_host_clock_ns();

		timeout = now < deadline ? deadline - now : 0;
	}

	if (timeout)
		__builtin_wasm_memory_atomic_wait32((int *)&wasm_irq_pending,
						    0, timeout);
	wasm_timer_poll();
}

/*
 * FIXME(wasm): this is the only place besides idle where the timer is
 * polled. Kernel code that runs for a long time without idling or calling
 * cpu_relax() sees no timer interrupts (jiffies stall) until it does.
 */
void cpu_relax(void)
{
	wasm_timer_poll();
	if (wasm_irq_pending && !irqs_disabled()) {
		unsigned long flags;

		local_irq_save(flags);
		local_irq_restore(flags);
	}
}

void __init init_IRQ(void)
{
	unsigned int irq;

	for (irq = 0; irq < NR_IRQS; irq++)
		irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
}
