/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Entry points called by the host. These are written in assembly because
 * they set up the two per-thread wasm globals, which must happen before
 * any compiled C code (which assumes a valid __stack_pointer) runs.
 */
#include <generated/asm-offsets.h>

	.globaltype	__stack_pointer, i32

	/* Definition of the per-thread "current" pointer, see asm/current.h. */
	.globl		__wasm_current
	.globaltype	__wasm_current, i32
__wasm_current:

	.functype	wasm_start_kernel () -> ()
	.functype	wasm_thread_start (i32) -> ()

/*
 * void _start(void) - boot the kernel on the calling host thread.
 * The boot thread becomes init_task (pid 0, later the idle task).
 */
	.globl	_start
	.type	_start,@function
_start:
	.functype	_start () -> ()
	i32.const	init_stack
	i32.const	WASM_THREAD_SIZE
	i32.add
	global.set	__stack_pointer
	i32.const	init_task
	global.set	__wasm_current
	call		wasm_start_kernel
	end_function

/*
 * void wasm_thread_entry(struct task_struct *task, void *sp)
 * First code run on a host thread created by host.spawn_thread().
 */
	.globl	wasm_thread_entry
	.type	wasm_thread_entry,@function
wasm_thread_entry:
	.functype	wasm_thread_entry (i32, i32) -> ()
	local.get	1
	global.set	__stack_pointer
	local.get	0
	global.set	__wasm_current
	local.get	0
	call		wasm_thread_start
	end_function
