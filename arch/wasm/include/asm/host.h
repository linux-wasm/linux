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
 *   console_write(ptr, len)   Write console output bytes. Must not block.
 *   console_read(ptr, len)    Copy up to len bytes of pending console input,
 *     -> i32                  return how many (0 if none). Must not block.
 *                             The host announces new input by raising
 *                             interrupt 1 (wasm_raise_irq).
 *   console_size() -> i32     Size of the terminal that shows the console:
 *                             rows << 16 | columns, 0 if unknown. Must
 *                             not block. The host announces a change by
 *                             raising interrupt 1 as well.
 *   clock_ns() -> i64         Monotonic clock in nanoseconds.
 *   clock_realtime_ns()       Wall-clock time in nanoseconds since the Unix
 *     -> i64                  epoch. Read once at boot to set the time.
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
 *   user_exec(image, len,     Compile the wasm program at image/len and
 *       memory_base,          instantiate it for the calling thread,
 *       table_size, sp)       replacing any program it had. Imports to
 *     -> i32                  provide: env.memory (the kernel's), env.
 *                             __memory_base, env.__table_base and env.
 *                             __indirect_function_table (table_size
 *                             entries starting at __table_base), env.
 *                             __stack_pointer (initially sp), and linux.
 *                             syscall, linked to this instance's
 *                             wasm_syscall export. Then call the program's
 *                             __wasm_apply_data_relocs export if it has
 *                             one. Returns 0 or a negative errno.
 *   user_clone(task)          The thread that will later be spawned for
 *                             task (spawn_thread) must get its own
 *                             instance of the calling thread's program:
 *                             same module, memory_base and table_size.
 *   user_run(arg, entry)      Enter the thread's program: call its export
 *     -> i32                  _start(arg) for entry WASM_USER_START, or
 *                             for WASM_USER_CLONE instantiate the program
 *                             announced by user_clone, with arg as
 *                             __stack_pointer, and call its export
 *                             __clone_entry(arg). Returns 0 if that call
 *                             returns, 1 if it was ended by user_unwind.
 *   user_unwind()             Abandon the running program: unwind the host
 *                             stack up to the user_run call that entered
 *                             it and make that return 1. Does not return.
 *   user_signal(handler, sig, Call entry `handler` of the function table
 *               info, uc)     of the thread's program with the arguments
 *     -> i32                  (sig, info, uc); a handler declared with
 *                             fewer parameters gets the leading ones. The
 *                             result is an enum wasm_signal: RETURNED if
 *                             the handler returned, THREW if it ended with
 *                             a wasm exception, which the host keeps for
 *                             user_rethrow, NOT_CALLABLE if the table has
 *                             no function there. Called from inside a
 *                             system call of the program; user_unwind
 *                             passes through it.
 *   user_rethrow()            Throw the exception kept by the last
 *                             user_signal. Does not return.
 *
 * Devices are virtio devices (CONFIG_VIRTIO_WASM, kernel/virtio.c). The
 * host numbers them from 0; device n signals used buffers by raising
 * interrupt WASM_IRQ_VIRTIO0 + n. Virtqueues are split rings (virtio 1.0,
 * little endian) in the kernel's memory; addresses in them are addresses
 * in that memory. The host must offer VIRTIO_F_VERSION_1 and must not
 * offer VIRTIO_F_ACCESS_PLATFORM.
 *
 *   virtio_probe(n, info)     Describe device n in *info (struct
 *     -> i32                  wasm_virtio_info) and return 0, or return
 *                             nonzero if there is no device n.
 *   virtio_config_read(n,     Copy len bytes of the device's configuration
 *       offset, ptr, len)     space, starting at offset.
 *   virtio_set_features(n,    The feature bits the driver accepted.
 *       lo, hi)
 *   virtio_set_status(n,      The device status byte. 0 resets the device:
 *       status)               when the call returns, the host has
 *                             forgotten the queues and no longer touches
 *                             any buffer.
 *   virtio_queue(n, q, num,   Queue q of device n has num entries and its
 *       desc, avail, used)    three parts at these addresses; num 0 means
 *                             the queue is gone.
 *   virtio_notify(n, q)       Buffers were added to queue q. Must not
 *                             block.
 *
 * Exports, called by the host:
 *
 *   _start()                  Boot. Call once, on a thread that may block.
 *   wasm_thread_entry(a, b)   See spawn_thread.
 *   wasm_raise_irq(irq)       Mark an interrupt pending and wake the CPU.
 *                             Callable from any thread/instance.
 *   wasm_syscall(nr, a0..a5)  System call entry for the user program of
 *                             the thread this instance belongs to.
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

__wasm_host(clock_realtime_ns)
u64 wasm_host_clock_realtime_ns(void);

__wasm_host(spawn_thread)
void wasm_host_spawn_thread(struct task_struct *task, void *sp);

__wasm_host(thread_exit)
void __noreturn wasm_host_thread_exit(void);

__wasm_host(halt)
void __noreturn wasm_host_halt(int reason);

__wasm_host(get_cmdline)
size_t wasm_host_get_cmdline(char *buf, size_t len);

__wasm_host(user_exec)
int wasm_host_user_exec(const void *image, size_t len,
			unsigned long memory_base, unsigned long table_size,
			unsigned long sp);

__wasm_host(console_read)
int wasm_host_console_read(char *buf, size_t len);

__wasm_host(console_size)
u32 wasm_host_console_size(void);

enum wasm_user_entry {
	WASM_USER_START = 0,
	WASM_USER_CLONE = 1,
};

__wasm_host(user_clone)
void wasm_host_user_clone(struct task_struct *task);

__wasm_host(user_run)
int wasm_host_user_run(unsigned long arg, int entry);

__wasm_host(user_unwind)
void __noreturn wasm_host_user_unwind(void);

enum wasm_signal {
	WASM_SIGNAL_RETURNED = 0,
	WASM_SIGNAL_THREW = 1,
	WASM_SIGNAL_NOT_CALLABLE = 2,
};

__wasm_host(user_signal)
int wasm_host_user_signal(unsigned long handler, int sig, void *info, void *uc);

/* Filled in by virtio_probe. All fields little endian (as wasm is). */
struct wasm_virtio_info {
	u32 device_id;		/* VIRTIO_ID_* */
	u32 vendor_id;
	u32 features_lo;	/* feature bits 0-31 */
	u32 features_hi;	/* feature bits 32-63 */
	u32 config_len;		/* size of the configuration space */
};

__wasm_host(virtio_probe)
int wasm_host_virtio_probe(u32 index, struct wasm_virtio_info *info);

__wasm_host(virtio_config_read)
void wasm_host_virtio_config_read(u32 index, u32 offset, void *buf, u32 len);

__wasm_host(virtio_set_features)
void wasm_host_virtio_set_features(u32 index, u32 lo, u32 hi);

__wasm_host(virtio_set_status)
void wasm_host_virtio_set_status(u32 index, u32 status);

__wasm_host(virtio_queue)
void wasm_host_virtio_queue(u32 index, u32 queue, u32 num, u32 desc,
			    u32 avail, u32 used);

__wasm_host(virtio_notify)
void wasm_host_virtio_notify(u32 index, u32 queue);

#endif /* __ASSEMBLY__ */
#endif
