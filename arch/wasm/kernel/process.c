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

	while (!__atomic_load_n(&prev->thread.run, __ATOMIC_SEQ_CST))
		__builtin_wasm_memory_atomic_wait32(&prev->thread.run, 0, -1);

	return wasm_last_task;
}

/* First C code of a new task, called from wasm_thread_entry (entry.S). */
void wasm_thread_start(struct task_struct *task)
{
	int (*fn)(void *) = task->thread.fn;
	void *fn_arg = task->thread.fn_arg;

	schedule_tail(wasm_last_task);

	if (fn)
		fn(fn_arg);

	/*
	 * A kernel thread function only returns after a successful
	 * kernel_execve(), i.e. when it should enter user mode: the wasm
	 * binfmt has had the host instantiate the program on this thread.
	 */
	if (!task->thread.user)
		panic("wasm: kernel thread %s returned without exec", task->comm);

	local_irq_enable();
	wasm_host_user_run(task->thread.user_sp);

	/* The program returned from _start instead of calling exit(). */
	pr_warn("%s[%d]: returned from _start\n", task->comm, task_pid_nr(task));
	do_group_exit(SIGSEGV);
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	memset(&p->thread, 0, sizeof(p->thread));

	/*
	 * FIXME(wasm): fork/vfork/clone of user tasks is not implemented. It
	 * needs a second instance of the user program on a new host thread.
	 */
	if (!args->fn)
		return -ENOSYS;

	p->thread.fn = args->fn;
	p->thread.fn_arg = args->fn_arg;
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
