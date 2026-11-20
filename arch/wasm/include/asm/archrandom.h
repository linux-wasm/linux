/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_ARCHRANDOM_H
#define _ASM_WASM_ARCHRANDOM_H

#include <linux/types.h>
#include <asm/host.h>

/*
 * The host has a cryptographic random number generator (in a browser,
 * crypto.getRandomValues), and it is the only source of entropy worth the
 * name here: there is no hardware to observe, and the kernel's fallback
 * of measuring timer jitter spins without ever letting the timer fire
 * (interrupts are only delivered when the kernel idles or polls).
 */
static inline size_t __must_check arch_get_random_longs(unsigned long *v,
							 size_t max_longs)
{
	wasm_host_random(v, max_longs * sizeof(*v));
	return max_longs;
}

static inline size_t __must_check arch_get_random_seed_longs(unsigned long *v,
							      size_t max_longs)
{
	wasm_host_random(v, max_longs * sizeof(*v));
	return max_longs;
}

#endif
