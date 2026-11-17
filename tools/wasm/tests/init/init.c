// SPDX-License-Identifier: GPL-2.0
/*
 * /init for the boot test: no libc, only raw system calls.
 *
 * It exercises what the kernel offers user space so far and prints what
 * it sees to its stdout (/dev/console):
 *
 *   - being loaded from the initramfs and entered, with argc/argv
 *   - simple system calls, and an invalid one
 *   - mmap, clone with an entry function, execve in the child, waitid
 *   - wasm_vfork: a child on the parent's thread
 *   - signal handlers
 *   - reading a line from the console
 *   - powering the machine off
 *
 * Built by tools/wasm/scripts/build-test-init.sh.
 */
#include "sys.h"

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

#define CHILD_STACK_SIZE	(64 * 1024)

/*
 * Entry point of a clone() child. It runs on a fresh instance of this
 * module that shares memory with the parent, so it sees the parent's data
 * but starts with its own call stack. The stack argument is whatever the
 * parent passed to clone(); here it points at the program to exec.
 */
void __clone_entry(char **stack)
{
	char *argv[] = { stack[0], "from", "init", 0 };
	char *envp[] = { "TEST=1", 0 };

	puts("child: pid ");
	putnum(sys0(__NR_getpid));
	puts(", exec ");
	puts(stack[0]);
	puts("\n");

	sys3(__NR_execve, stack[0], argv, envp);

	puts("child: execve failed\n");
	sys1(__NR_exit, 127);
	for (;;)
		;
}

static void test_clone_exec(void)
{
	char **top;
	char *stack;
	/* siginfo_t for SIGCHLD: signo, errno, code, pid, uid, status. */
	int info[32] = { 0 };
	long pid, ret;

	stack = (char *)sys6(__NR_mmap2, 0, CHILD_STACK_SIZE,
			     PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((unsigned long)stack >= (unsigned long)-4095) {
		puts("init: mmap failed\n");
		return;
	}

	/* Leave the child one word at the top of its stack. */
	top = (char **)(stack + CHILD_STACK_SIZE - 16);
	top[0] = "/hello";

	/* CLONE_VFORK: we are suspended until the child has exec'd. */
	pid = sys3(__NR_clone, CLONE_VM | CLONE_VFORK | SIGCHLD, top, 0);
	if (pid < 0) {
		puts("init: clone failed\n");
		return;
	}

	/*
	 * The child is runnable again as soon as it has exec'd, so say
	 * nothing until it is gone: the order of the output stays fixed.
	 */
	ret = sys4(__NR_waitid, P_PID, pid, info, WEXITED);
	if (ret < 0) {
		puts("init: waitid failed: ");
		putnum(ret);
		puts("\n");
	} else if (info[3] != pid || info[2] != CLD_EXITED) {
		puts("init: waitid: unexpected child or cause\n");
	} else {
		puts("init: child ");
		putnum(pid);
		puts(" exited with status ");
		putnum(info[5]);
		puts("\n");
	}

	sys3(__NR_munmap, stack, CHILD_STACK_SIZE, 0);
}

/*
 * wasm_vfork: the system call returns 0 and this thread is the child. When
 * the child exits (or execs), that system call returns the child's pid and
 * the thread is the parent again. A libc hides this behind setjmp/longjmp;
 * here the code just carries on.
 */
static void test_vfork(void)
{
	int info[32] = { 0 };
	long self = sys0(__NR_getpid);
	long pid, child;

	pid = sys0(__NR_wasm_vfork);
	if (pid != 0) {
		puts("init: wasm_vfork failed: ");
		putnum(pid);
		puts("\n");
		return;
	}
	child = sys0(__NR_getpid);
	puts(child != self ? "vfork child: has its own pid\n"
			   : "vfork child: still has the parent's pid\n");

	pid = sys1(__NR_exit, 9);
	puts(pid == child && sys0(__NR_getpid) == self ?
	     "init: back in the parent, exit returned the child's pid\n" :
	     "init: unexpected state after the vfork child exited\n");

	if (sys4(__NR_waitid, P_PID, pid, info, WEXITED) < 0)
		puts("init: waitid for the vfork child failed\n");
	else {
		puts("init: vfork child exited with status ");
		putnum(info[5]);
		puts("\n");
	}
}

/*
 * Signal handlers. The kernel calls the handler when a system call is about
 * to return: here, the kill() that sent the signal. The handler makes
 * system calls of its own before the interrupted one returns.
 */
struct sigaction {
	void (*handler)(int, int *, void *);
	unsigned long flags;
	unsigned long mask[2];
};

static int handled, handled_signo, handled_blocked;

static void on_signal(int sig, int *info, void *uc)
{
	unsigned long blocked[2] = { 0, 0 };

	handled++;
	/* siginfo_t starts with si_signo. */
	handled_signo = uc ? info[0] : -1;
	/* The signal is blocked while its handler runs. */
	sys4(__NR_rt_sigprocmask, SIG_BLOCK, 0, blocked, 8);
	handled_blocked = (blocked[0] >> (sig - 1)) & 1;
	puts("handler: signal ");
	putnum(sig);
	puts(" in pid ");
	putnum(sys0(__NR_getpid));
	puts("\n");
}

static void test_signals(void)
{
	struct sigaction sa = { on_signal, SA_SIGINFO, { 0, 0 } };
	unsigned long usr2[2] = { 1UL << (SIGUSR2 - 1), 0 };
	unsigned long blocked[2] = { 0, 0 };
	long self = sys0(__NR_getpid);
	long ret;

	if (sys4(__NR_rt_sigaction, SIGUSR1, &sa, 0, 8) ||
	    sys4(__NR_rt_sigaction, SIGUSR2, &sa, 0, 8)) {
		puts("init: rt_sigaction failed\n");
		return;
	}

	/* The handler's own system calls must not leak into this result. */
	ret = sys3(__NR_kill, self, SIGUSR1, 0);
	sys4(__NR_rt_sigprocmask, SIG_BLOCK, 0, blocked, 8);
	puts(ret == 0 && handled == 1 && handled_signo == SIGUSR1 &&
	     handled_blocked && !(blocked[0] & (1UL << (SIGUSR1 - 1))) ?
	     "init: handler ran before kill returned 0, mask restored\n" :
	     "init: signal handler: unexpected state\n");

	/* A blocked signal waits; unblocking it runs the handler. */
	sys4(__NR_rt_sigprocmask, SIG_BLOCK, usr2, 0, 8);
	sys3(__NR_kill, self, SIGUSR2, 0);
	puts(handled == 1 ? "init: blocked signal is pending\n" :
			    "init: blocked signal was delivered\n");
	sys4(__NR_rt_sigprocmask, SIG_UNBLOCK, usr2, 0, 8);
	puts(handled == 2 && handled_signo == SIGUSR2 ?
	     "init: handler ran when the signal was unblocked\n" :
	     "init: unblocked signal: handler did not run\n");
}

static void test_console_input(void)
{
	char line[64];
	long n;

	puts("init: type a line: ");
	n = sys3(__NR_read, 0, line, sizeof(line) - 1);
	if (n < 0) {
		puts("init: read failed\n");
		return;
	}
	line[n] = '\0';
	puts("init: read ");
	putnum(n);
	puts(" bytes: ");
	puts(line);
}

void _start(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct utsname uts;

	puts("init: hello from user space, pid ");
	putnum(sys0(__NR_getpid));
	puts(", argc ");
	putnum(argc);
	puts(", argv[0] ");
	puts(argc > 0 ? argv[0] : "(none)");
	puts("\n");

	if (sys1(__NR_uname, &uts) == 0) {
		puts("init: uname: ");
		puts(uts.sysname);
		puts(" ");
		puts(uts.release);
		puts(" ");
		puts(uts.machine);
		puts("\n");
	} else {
		puts("init: uname failed\n");
	}

	/* An unknown system call must fail cleanly, not trap. */
	puts(sys0(100000) == -ENOSYS ?
	     "init: bad syscall number returns -ENOSYS\n" :
	     "init: bad syscall number: unexpected result\n");

	test_clone_exec();
	test_vfork();
	test_signals();
	test_console_input();

	puts("init: powering off\n");
	sys3(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
	     LINUX_REBOOT_CMD_POWER_OFF);

	puts("init: reboot() returned\n");
	for (;;)
		;
}
