/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_CURRENT_H
#define _ASM_WASM_CURRENT_H

#ifndef __ASSEMBLY__

struct task_struct;

/*
 * Every kernel task runs on its own host thread with its own instance of
 * the kernel module. Instances share linear memory but not wasm globals,
 * so a mutable global is a free thread-local: __wasm_current is set once
 * when the thread starts (arch/wasm/kernel/entry.S) and never changes.
 */
__asm__(".globaltype __wasm_current, i32\n");

static __always_inline struct task_struct *get_current(void)
{
	struct task_struct *task;

	__asm__("global.get __wasm_current\n"
		"local.set %0"
		: "=r"(task));
	return task;
}

#define current get_current()

#endif /* __ASSEMBLY__ */
#endif
