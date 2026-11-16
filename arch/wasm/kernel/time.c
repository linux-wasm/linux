// SPDX-License-Identifier: GPL-2.0
/*
 * Clocksource and clockevent on top of the host's monotonic clock.
 * Derived from arch/wasm/kernel/time.c in tombl/linux.
 */
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched/clock.h>
#include <linux/timekeeping.h>
#include <asm/host.h>
#include <asm/irq.h>
#include "wasm.h"

static u64 wasm_clock_origin;

/* printk timestamps call this long before time_init(). */
unsigned long long sched_clock(void)
{
	u64 now = wasm_host_clock_ns();

	if (!wasm_clock_origin)
		wasm_clock_origin = now;
	return now - wasm_clock_origin;
}

/* The kernel's wall clock starts from the host's. */
void read_persistent_clock64(struct timespec64 *ts)
{
	*ts = ns_to_timespec64(wasm_host_clock_realtime_ns());
}

static u64 wasm_clocksource_read(struct clocksource *cs)
{
	return wasm_host_clock_ns();
}

static struct clocksource wasm_clocksource = {
	.name	= "wasm-host",
	.rating	= 400,
	.read	= wasm_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int wasm_timer_set_next_event(unsigned long delta_ns,
				     struct clock_event_device *evt)
{
	wasm_timer_deadline = wasm_host_clock_ns() + delta_ns;
	return 0;
}

static int wasm_timer_shutdown(struct clock_event_device *evt)
{
	wasm_timer_deadline = 0;
	return 0;
}

static struct clock_event_device wasm_clockevent = {
	.name			= "wasm-timer",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 300,
	.set_next_event		= wasm_timer_set_next_event,
	.set_state_shutdown	= wasm_timer_shutdown,
	.set_state_oneshot	= wasm_timer_shutdown,
	.irq			= WASM_IRQ_TIMER,
};

static irqreturn_t wasm_timer_interrupt(int irq, void *dev)
{
	struct clock_event_device *evt = dev;

	if (evt->event_handler)
		evt->event_handler(evt);
	return IRQ_HANDLED;
}

void __init time_init(void)
{
	if (clocksource_register_hz(&wasm_clocksource, NSEC_PER_SEC))
		panic("wasm: cannot register clocksource");

	if (request_irq(WASM_IRQ_TIMER, wasm_timer_interrupt, IRQF_TIMER,
			"timer", &wasm_clockevent))
		panic("wasm: cannot request timer irq");

	wasm_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&wasm_clockevent, NSEC_PER_SEC,
					1000, LONG_MAX);
}

/*
 * Delays block the host thread instead of spinning: wait on a word that
 * nobody ever notifies, with a timeout. "loops" are nanoseconds.
 */
void __delay(unsigned long loops)
{
	static int never;

	__builtin_wasm_memory_atomic_wait32(&never, 0, loops);
}

void __udelay(unsigned long usecs)
{
	__delay(usecs * 1000);
}

void __ndelay(unsigned long nsecs)
{
	__delay(nsecs);
}

void __const_udelay(unsigned long xloops)
{
	/* xloops = usecs * 0x10C7 (2^32 / 10^6, rounded up) */
	__udelay(xloops / 0x10C7);
}

void calibrate_delay(void)
{
	loops_per_jiffy = NSEC_PER_SEC / HZ;
}
