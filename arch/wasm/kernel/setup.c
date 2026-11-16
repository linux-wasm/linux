// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/start_kernel.h>
#include <linux/string.h>
#include <asm/host.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/thread_info.h>
#include "wasm.h"

#define WASM_PAGE_SIZE	65536UL

/* Provided by wasm-ld: end of the kernel image (data, bss, linker stack). */
extern char __heap_base[];

/* init_task's stack; _start (entry.S) points __stack_pointer at its top. */
unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

void __init wasm_start_kernel(void)
{
	start_kernel();
}

void __init setup_arch(char **cmdline_p)
{
	/*
	 * RAM is whatever linear memory the host instantiated us with.
	 * FIXME(wasm): fixed at boot; memory.grow based hotplug is not done.
	 */
	u64 mem_size = (u64)__builtin_wasm_memory_size(0) * WASM_PAGE_SIZE;
	phys_addr_t mem_end = min_t(u64, mem_size, 0xfffff000UL) & PAGE_MASK;
	size_t len;

	wasm_console_init();

	len = wasm_host_get_cmdline(boot_command_line, COMMAND_LINE_SIZE - 1);
	boot_command_line[min_t(size_t, len, COMMAND_LINE_SIZE - 1)] = '\0';
	if (!boot_command_line[0])
		strscpy(boot_command_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
	*cmdline_p = boot_command_line;
	parse_early_param();

	memblock_add(0, mem_end);
	/* Kernel image, and with it page 0 so that NULL is never handed out. */
	memblock_reserve(0, PFN_ALIGN(__heap_base));

	min_low_pfn = PFN_UP((unsigned long)__heap_base);
	max_low_pfn = PFN_DOWN(mem_end);
	max_pfn = max_low_pfn;

	pr_info("wasm: %llu KiB linear memory, %lu KiB kernel image\n",
		mem_size >> 10, (unsigned long)__heap_base >> 10);
}
