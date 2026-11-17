/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The little bit of "libc" the boot test programs share: raw system calls
 * and string output. The program ABI is described in
 * arch/wasm/kernel/binfmt_wasm.c.
 */
#ifndef TEST_SYS_H
#define TEST_SYS_H

#define __NR_read	63
#define __NR_write	64
#define __NR_exit	93
#define __NR_waitid	95
#define __NR_kill	129
#define __NR_rt_sigaction	134
#define __NR_rt_sigprocmask	135
#define __NR_reboot	142
#define __NR_uname	160
#define __NR_getpid	172
#define __NR_munmap	215
#define __NR_clone	220
#define __NR_execve	221
#define __NR_mmap2	222
#define __NR_wasm_vfork	244

#define ENOSYS		38

#define PROT_READ	1
#define PROT_WRITE	2
#define MAP_PRIVATE	0x02
#define MAP_ANONYMOUS	0x20

#define SIGUSR1		10
#define SIGUSR2		12
#define SIGCHLD		17
#define SA_SIGINFO	4
#define SIG_BLOCK	0
#define SIG_UNBLOCK	1
#define P_PID		1
#define WEXITED		4
#define CLD_EXITED	1
#define CLONE_VM	0x00000100
#define CLONE_VFORK	0x00004000

#define LINUX_REBOOT_MAGIC1		0xfee1dead
#define LINUX_REBOOT_MAGIC2		672274793
#define LINUX_REBOOT_CMD_POWER_OFF	0x4321fedc

__attribute__((import_module("linux"), import_name("syscall")))
long syscall(long nr, long a, long b, long c, long d, long e, long f);

#define sys0(nr)		syscall(nr, 0, 0, 0, 0, 0, 0)
#define sys1(nr, a)		syscall(nr, (long)(a), 0, 0, 0, 0, 0)
#define sys3(nr, a, b, c)	syscall(nr, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define sys4(nr, a, b, c, d) \
	syscall(nr, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define sys6(nr, a, b, c, d, e, f) \
	syscall(nr, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f))

static inline unsigned long strlen(const char *s)
{
	unsigned long n = 0;

	while (s[n])
		n++;
	return n;
}

static inline void puts(const char *s)
{
	sys3(__NR_write, 1, s, strlen(s));
}

static inline void putnum(long n)
{
	char buf[12];
	char *p = buf + sizeof(buf) - 1;

	unsigned long u = n < 0 ? -(unsigned long)n : n;

	*p = '\0';
	do {
		*--p = '0' + u % 10;
		u /= 10;
	} while (u);
	if (n < 0)
		*--p = '-';
	puts(p);
}

#endif
