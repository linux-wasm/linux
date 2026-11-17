// SPDX-License-Identifier: GPL-2.0
/*
 * Tasks and context switching.
 *
 * wasm cannot switch stacks: the call stack is owned by the engine. So,
 * as in the prior ports (tombl/linux, joelseverin/linux), every task gets
 * its own host thread, each running its own instance of the kernel module
 * on the shared linear memory. A context switch wakes the next task's
 * thread and parks the current one; exactly one thread runs at a time.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/ptrace.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/signal.h>
#include <asm/host.h>
#include <asm/switch_to.h>
#include "wasm.h"

__asm__(".globaltype __stack_pointer, i32\n");

static __always_inline unsigned long wasm_get_stack_pointer(void)
{
	unsigned long sp;

	__asm__ volatile("global.get __stack_pointer\n"
			 "local.set %0"
			 : "=r"(sp));
	return sp;
}

static __always_inline void wasm_set_stack_pointer(unsigned long sp)
{
	__asm__ volatile("local.get %0\n"
			 "global.set __stack_pointer"
			 : : "r"(sp));
}

static __always_inline void wasm_set_current(struct task_struct *task)
{
	__asm__ volatile("local.get %0\n"
			 "global.set __wasm_current"
			 : : "r"(task));
}

/*
 * Normally a host thread belongs to one task for life. The vfork emulation
 * (vfork.c) moves a thread between a parent and its child: the task named
 * here takes over the calling thread at the next switch away from it.
 */
struct task_struct *wasm_thread_handoff;

/* The task we most recently switched away from. Protected by being UP. */
static struct task_struct *wasm_last_task;

struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next)
{
	/*
	 * Sample this before waking @next: once it runs it may free a dead
	 * @prev (including its stack) at any time.
	 */
	bool dead = READ_ONCE(prev->__state) == TASK_DEAD;
	/* The task this host thread carries once it runs again. */
	struct task_struct *self = wasm_thread_handoff ?: prev;

	wasm_thread_handoff = NULL;

	wasm_last_task = prev;
	__atomic_store_n(&prev->thread.run, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n(&next->thread.run, 1, __ATOMIC_SEQ_CST);

	if (!next->thread.spawned) {
		/* First switch to a new task: only now create its thread. */
		void *sp = (void *)((unsigned long)task_pt_regs(next) & ~15UL);

		next->thread.spawned = 1;
		wasm_host_spawn_thread(next, sp);
	} else {
		__builtin_wasm_memory_atomic_notify(&next->thread.run, 1);
	}

	/* From here on @prev's memory must not be touched if it is dead. */
	if (dead)
		wasm_host_thread_exit();

	while (!__atomic_load_n(&self->thread.run, __ATOMIC_SEQ_CST))
		__builtin_wasm_memory_atomic_wait32(&self->thread.run, 0, -1);

	wasm_set_current(self);
	return wasm_last_task;
}

/* First C code of a new task, called from wasm_thread_entry (entry.S). */
void wasm_thread_start(struct task_struct *task)
{
	int (*fn)(void *) = task->thread.fn;
	void *fn_arg = task->thread.fn_arg;
	int entry = WASM_USER_CLONE;

	schedule_tail(wasm_last_task);

	if (fn) {
		fn(fn_arg);
		/*
		 * A kernel thread function only returns after a successful
		 * kernel_execve(), i.e. when it should enter user mode: the
		 * wasm binfmt has had the host instantiate the program on
		 * this thread.
		 */
		if (!task->thread.user)
			panic("wasm: kernel thread %s returned without exec",
			      task->comm);
		entry = WASM_USER_START;
	}

	local_irq_enable();

	/* A vfork child that called exit() on its parent's thread. */
	if (task->thread.exit_pending)
		do_group_exit((task->thread.exit_code & 0xff) << 8);

	/* A vfork child that exec'd there starts its program here. */
	if (task->thread.exec_image)
		entry = WASM_USER_START;

	/*
	 * Run the program. If it execs, wasm_syscall() has the host unwind
	 * everything above this frame, including the kernel frames of that
	 * system call, and user_run() returns 1: start over with the new
	 * program. The unwinding does not restore the kernel stack pointer,
	 * so do that here.
	 */
	for (;;) {
		unsigned long sp = wasm_get_stack_pointer();

		task->thread.exec_pending = 0;
		if (task->thread.exec_image && wasm_user_instantiate())
			do_group_exit(SIGKILL);
		if (!wasm_host_user_run(task->thread.user_sp, entry))
			break;
		wasm_set_stack_pointer(sp);
		entry = WASM_USER_START;
	}

	/* The program returned instead of calling exit(). */
	pr_warn("%s[%d]: returned from its entry point\n", task->comm,
		task_pid_nr(task));
	do_group_exit(SIGSEGV);
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	memset(&p->thread, 0, sizeof(p->thread));

	if (args->fn) {
		p->thread.fn = args->fn;
		p->thread.fn_arg = args->fn_arg;
		return 0;
	}

	/*
	 * clone() from a user program. A new host thread cannot continue in
	 * the middle of the parent's wasm call stack, so the child does not
	 * "return from clone": it gets its own instance of the program and
	 * starts at the program's __clone_entry(stack) export. That requires
	 * a stack of its own and the parent's memory (there is no MMU to
	 * copy an address space anyway).
	 *
	 * fork() is impossible. vfork() is provided separately by the
	 * wasm_vfork system call, which lends the child the parent's thread
	 * (vfork.c).
	 */
	if (!current->thread.user || !(args->flags & CLONE_VM))
		return -EINVAL;

	if (current->thread.vfork_request) {
		/* wasm_vfork(): the child runs on the parent's thread. */
		p->thread.user = 1;
		p->thread.spawned = 1;
		p->thread.vfork_parent = current;
		current->thread.vfork_child = p;
		return 0;
	}

	if (!args->stack)
		return -EINVAL;

	p->thread.user = 1;
	p->thread.user_sp = args->stack;
	wasm_host_user_clone(p);
	return 0;
}

void flush_thread(void)
{
}

void show_regs(struct pt_regs *regs)
{
	pr_info("wasm: no register state to show\n");
}

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	/* FIXME(wasm): the wasm call stack cannot be walked from inside. */
	printk("%swasm: no stack trace available\n", loglvl);
}

/* FIXME(wasm): ptrace is not implemented. */
long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	return -EIO;
}

void ptrace_disable(struct task_struct *child)
{
}

void machine_restart(char *cmd)
{
	wasm_host_halt(WASM_HALT_RESTART);
}

void machine_halt(void)
{
	wasm_host_halt(WASM_HALT_POWEROFF);
}

void machine_power_off(void)
{
	wasm_host_halt(WASM_HALT_POWEROFF);
}

/* Non-NULL so that the kernel knows the machine can power off. */
void (*pm_power_off)(void) = machine_power_off;
