// SPDX-License-Identifier: GPL-2.0
/* /proc/cpuinfo */
#include <linux/seq_file.h>
#include <linux/threads.h>

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? (void *)(unsigned long)(*pos + 1) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

static int c_show(struct seq_file *m, void *v)
{
	seq_printf(m, "processor\t: %lu\n", (unsigned long)v - 1);
	seq_puts(m, "model name\t: WebAssembly (wasm32)\n");
	seq_puts(m, "features\t: atomics bulk-memory mutable-globals sign-ext nontrapping-fptoint\n\n");
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show,
};
