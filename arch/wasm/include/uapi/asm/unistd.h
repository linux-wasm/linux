/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#include <asm/unistd_32.h>

/*
 * vfork() for a machine where a child cannot resume inside its parent's
 * call stack on a thread of its own: the child borrows the parent's
 * thread instead. Returns 0 in the child. When the child ends the borrowing
 * with a successful execve() or with exit()/exit_group(), that system call
 * returns the child's pid, and the caller is the parent again. See
 * arch/wasm/kernel/vfork.c.
 */
#define __NR_wasm_vfork 244
