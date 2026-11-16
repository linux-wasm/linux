/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The complete interface between the kernel and whatever runs the wasm
 * module (a browser page, node, a server-side runtime, ...).
 *
 * Nothing in here is browser specific. Keep it small: devices should be
 * added as virtio transports later, not as more imports.
 *
 * Imports (wasm module "host"), called by the kernel:
 *
 *   console_write(ptr, len)   Write kernel log bytes. Must not block.
 *   clock_ns() -> i64         Monotonic clock in nanoseconds.
 *   spawn_thread(task, sp)    Start a new host thread on a fresh instance
 *                             of this module sharing the same memory, and
 *                             call the export wasm_thread_entry(task, sp)
 *                             on it. Both values are opaque to the host.
 *                             May return before the thread has started.
 *   thread_exit()             The calling host thread is finished and can
 *                             be destroyed. Does not return.
 *   halt(reason)              Stop the whole machine (enum wasm_halt).
 *                             Does not return.
 *   get_cmdline(ptr, len)     Copy up to len bytes of kernel command line
 *     -> i32                  (no NUL needed), return the full length.
 *
 * Exports, called by the host:
 *
 *   _start()                  Boot. Call once, on a thread that may block.
 *   wasm_thread_entry(a, b)   See spawn_thread.
 *   wasm_raise_irq(irq)       Mark an interrupt pending and wake the CPU.
 *                             Callable from any thread/instance.
 *
 * The kernel blocks with the wasm threads instructions
 * (memory.atomic.wait32/notify) on its own; timers need no host call.
 */
#ifndef _ASM_WASM_HOST_H
#define _ASM_WASM_HOST_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/types.h>

struct task_struct;

enum wasm_halt {
	WASM_HALT_POWEROFF = 0,
	WASM_HALT_RESTART = 1,
	WASM_HALT_PANIC = 2,
};

#define __wasm_host(name) \
	__attribute__((import_module("host"), import_name(#name)))

__wasm_host(console_write)
void wasm_host_console_write(const char *s, size_t len);

__wasm_host(clock_ns)
u64 wasm_host_clock_ns(void);

__wasm_host(spawn_thread)
void wasm_host_spawn_thread(struct task_struct *task, void *sp);

__wasm_host(thread_exit)
void __noreturn wasm_host_thread_exit(void);

__wasm_host(halt)
void __noreturn wasm_host_halt(int reason);

__wasm_host(get_cmdline)
size_t wasm_host_get_cmdline(char *buf, size_t len);

#endif /* __ASSEMBLY__ */
#endif
