// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel log console backed by the host's console_write import.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <asm/host.h>
#include "wasm.h"

static void wasm_console_write(struct console *con, const char *s, unsigned int n)
{
	wasm_host_console_write(s, n);
}

static struct console wasm_console = {
	.name	= "wasmcon",
	.write	= wasm_console_write,
	.flags	= CON_PRINTBUFFER | CON_ANYTIME,
	.index	= -1,
};

void __init wasm_console_init(void)
{
	register_console(&wasm_console);
}
