// SPDX-License-Identifier: GPL-2.0
/*
 * /hello for the boot test: the program that init's child execs. Prints
 * its arguments and environment and exits with a recognisable status.
 */
#include "sys.h"

void _start(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	long i;

	puts("hello: pid ");
	putnum(sys0(__NR_getpid));
	puts(", argc ");
	putnum(argc);
	puts(", argv");
	for (i = 0; i < argc; i++) {
		puts(" ");
		puts(argv[i]);
	}
	puts(", env ");
	puts(envp[0] ? envp[0] : "(none)");
	puts("\n");

	sys1(__NR_exit, 42);
	for (;;)
		;
}
