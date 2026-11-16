// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal /init for the boot test: no libc, only raw system calls.
 *
 * It proves that the kernel can load a wasm user program from the
 * initramfs, enter it, and serve system calls from it. It prints a few
 * lines to its stdout (/dev/console) and powers the machine off.
 *
 * Built by tools/wasm/scripts/build-test-init.sh; the program ABI is
 * described in arch/wasm/kernel/binfmt_wasm.c.
 */

#define __NR_write	64
#define __NR_reboot	142
#define __NR_uname	160
#define __NR_getpid	172

#define LINUX_REBOOT_MAGIC1		0xfee1dead
#define LINUX_REBOOT_MAGIC2		672274793
#define LINUX_REBOOT_CMD_POWER_OFF	0x4321fedc

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

__attribute__((import_module("linux"), import_name("syscall")))
long syscall(long nr, long a, long b, long c, long d, long e, long f);

static unsigned long strlen(const char *s)
{
	unsigned long n = 0;

	while (s[n])
		n++;
	return n;
}

static void puts(const char *s)
{
	syscall(__NR_write, 1, (long)s, strlen(s), 0, 0, 0);
}

static void putnum(long n)
{
	char buf[12];
	char *p = buf + sizeof(buf) - 1;

	*p = '\0';
	do {
		*--p = '0' + n % 10;
		n /= 10;
	} while (n);
	puts(p);
}

void _start(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct utsname uts;
	long ret;

	puts("init: hello from user space, pid ");
	putnum(syscall(__NR_getpid, 0, 0, 0, 0, 0, 0));
	puts(", argc ");
	putnum(argc);
	puts(", argv[0] ");
	puts(argc > 0 ? argv[0] : "(none)");
	puts("\n");

	ret = syscall(__NR_uname, (long)&uts, 0, 0, 0, 0, 0);
	if (ret == 0) {
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
	ret = syscall(100000, 0, 0, 0, 0, 0, 0);
	puts(ret == -38 ? "init: bad syscall number returns -ENOSYS\n"
			: "init: bad syscall number: unexpected result\n");

	puts("init: powering off\n");
	syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_POWER_OFF, 0, 0, 0);

	puts("init: reboot() returned\n");
	for (;;)
		;
}
