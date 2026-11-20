// SPDX-License-Identifier: GPL-2.0
/*
 * Console backed by the host's console_write import: the kernel log
 * (printk) and, with CONFIG_TTY, a tty so that user programs get a
 * /dev/console to write to.
 *
 * Input: the host raises WASM_IRQ_CONSOLE when it has bytes; the handler
 * fetches them with console_read and feeds the tty. The same interrupt
 * announces a new size of the host's terminal (console_size), which becomes
 * the tty's window size.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/workqueue.h>
#include <asm/host.h>
#include <asm/irq.h>
#include "wasm.h"

/*
 * The host shows the console on a terminal, which wants "\r\n" at the end
 * of a line. The tty adds the "\r" for user programs (ONLCR); for the
 * kernel log it is added here, as serial consoles do.
 */
static void wasm_console_write(struct console *con, const char *s, unsigned int n)
{
	while (n) {
		const char *nl = memchr(s, '\n', n);
		unsigned int len = nl ? nl - s : n;

		if (len)
			wasm_host_console_write(s, len);
		if (!nl)
			break;
		wasm_host_console_write("\r\n", 2);
		s += len + 1;
		n -= len + 1;
	}
}

#ifdef CONFIG_TTY

static struct tty_driver *wasm_tty_driver;
static struct tty_port wasm_tty_port;
static const struct tty_port_operations wasm_tty_port_ops;

/* The host's terminal size as a winsize; false if the host has none. */
static bool wasm_tty_host_size(struct winsize *ws)
{
	u32 size = wasm_host_console_size();

	*ws = (struct winsize){ .ws_row = size >> 16, .ws_col = size & 0xffff };
	return ws->ws_row && ws->ws_col;
}

/* tty_do_resize() sleeps, so the interrupt handler leaves it to this. */
static void wasm_tty_resize(struct work_struct *work)
{
	struct tty_struct *tty = tty_port_tty_get(&wasm_tty_port);
	struct winsize ws;

	if (!tty)
		return;
	if (wasm_tty_host_size(&ws))
		tty_do_resize(tty, &ws);
	tty_kref_put(tty);
}
static DECLARE_WORK(wasm_tty_resize_work, wasm_tty_resize);

static int wasm_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	wasm_tty_host_size(&tty->winsize);
	return tty_port_install(&wasm_tty_port, driver, tty);
}

static int wasm_tty_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(&wasm_tty_port, tty, filp);
}

static void wasm_tty_close(struct tty_struct *tty, struct file *filp)
{
	tty_port_close(&wasm_tty_port, tty, filp);
}

static void wasm_tty_hangup(struct tty_struct *tty)
{
	tty_port_hangup(&wasm_tty_port);
}

static ssize_t wasm_tty_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
	wasm_host_console_write(buf, count);
	return count;
}

static unsigned int wasm_tty_write_room(struct tty_struct *tty)
{
	return 65536;
}

static const struct tty_operations wasm_tty_ops = {
	.install	= wasm_tty_install,
	.open		= wasm_tty_open,
	.close		= wasm_tty_close,
	.hangup		= wasm_tty_hangup,
	.write		= wasm_tty_write,
	.write_room	= wasm_tty_write_room,
};

static irqreturn_t wasm_tty_interrupt(int irq, void *dev)
{
	char buf[64];
	int n;

	while ((n = wasm_host_console_read(buf, sizeof(buf))) > 0)
		tty_insert_flip_string(&wasm_tty_port, buf, n);
	tty_flip_buffer_push(&wasm_tty_port);
	schedule_work(&wasm_tty_resize_work);
	return IRQ_HANDLED;
}

static struct tty_driver *wasm_console_device(struct console *con, int *index)
{
	*index = 0;
	return wasm_tty_driver;
}

static int __init wasm_tty_init(void)
{
	struct tty_driver *driver;
	int ret;

	driver = tty_alloc_driver(1, TTY_DRIVER_RESET_TERMIOS |
				     TTY_DRIVER_REAL_RAW |
				     TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(driver))
		return PTR_ERR(driver);

	tty_port_init(&wasm_tty_port);
	wasm_tty_port.ops = &wasm_tty_port_ops;

	driver->driver_name = "wasmcon";
	driver->name = "wasmcon";
	driver->type = TTY_DRIVER_TYPE_CONSOLE;
	driver->init_termios = tty_std_termios;
	tty_set_operations(driver, &wasm_tty_ops);
	tty_port_link_device(&wasm_tty_port, driver, 0);

	ret = tty_register_driver(driver);
	if (ret < 0) {
		tty_driver_kref_put(driver);
		tty_port_destroy(&wasm_tty_port);
		return ret;
	}

	wasm_tty_driver = driver;

	if (request_irq(WASM_IRQ_CONSOLE, wasm_tty_interrupt, 0, "console",
			&wasm_tty_port))
		pr_warn("wasm: console input disabled, cannot request irq\n");
	return 0;
}
device_initcall(wasm_tty_init);

#endif /* CONFIG_TTY */

static struct console wasm_console = {
	.name	= "wasmcon",
	.write	= wasm_console_write,
#ifdef CONFIG_TTY
	.device	= wasm_console_device,
#endif
	.flags	= CON_PRINTBUFFER | CON_ANYTIME,
	.index	= -1,
};

void __init wasm_console_init(void)
{
	register_console(&wasm_console);
}
