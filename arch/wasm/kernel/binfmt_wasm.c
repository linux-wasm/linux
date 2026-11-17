// SPDX-License-Identifier: GPL-2.0
/*
 * binfmt_wasm: run WebAssembly modules as user programs.
 *
 * User-space ABI
 * --------------
 * There is no MMU, so a program lives inside the kernel's linear memory
 * and must be position independent. A program is a wasm module that
 *
 *  - has a "dylink.0" custom section (clang -fPIC, wasm-ld -pie or -shared
 *    with --experimental-pic). Its mem_size/mem_align say how much memory
 *    the data segments need and its table_size how many table slots.
 *  - imports env.memory (shared; declare a maximum of 4 GiB so that it
 *    fits any machine), env.__memory_base, env.__table_base,
 *    env.__indirect_function_table and env.__stack_pointer as needed.
 *  - imports linux.syscall, type (i32 nr, i32 x 6) -> i32, the only way
 *    into the kernel. Numbers are the generic 32-bit ones
 *    (asm/unistd_32.h); errors are returned as -errno.
 *  - exports _start, type (i32 sp) -> (). sp points at the usual Linux
 *    process start-up block:
 *
 *        long argc; char *argv[argc]; NULL; char *envp[]; NULL;
 *        auxv pairs ..., AT_NULL
 *
 *    __stack_pointer starts at the same address and grows down from it.
 *    _start must not return.
 *  - may export __clone_entry, type (i32 stack) -> (), to be able to call
 *    clone(). The child cannot continue in the parent's call stack, so
 *    clone() only returns in the parent; the child runs on a new instance
 *    of the module (same memory, its own globals and table) and starts at
 *    __clone_entry(stack), with stack being clone()'s stack argument and
 *    the initial __stack_pointer. CLONE_VM and a stack are mandatory. What
 *    the child finds on that stack (function, argument, TLS) is between
 *    the parent and __clone_entry, i.e. libc's business. It must not
 *    return either.
 *
 * The kernel reads the whole file, allocates one anonymous mapping holding
 * the stack followed by the module's data, and asks the host to compile and
 * instantiate the module on the thread that will run it (asm/host.h).
 *
 * vfork() exists as the system call wasm_vfork (asm/unistd.h): it returns
 * 0 and the calling thread continues as the child; the child's execve() or
 * exit() returns the child's pid, in the parent. See vfork.c.
 *
 * execve() from a running program replaces the calling thread's instance;
 * the old program's frames are discarded when the system call returns.
 *
 * FIXME(wasm): fork() is impossible (see copy_thread), signal handlers are not run, the stack size is fixed and has no guard.
 *
 * The idea of a wasm binfmt comes from tombl/linux and joelseverin/linux;
 * this implementation and its ABI are specific to this port.
 */
#define pr_fmt(fmt) "binfmt_wasm: " fmt

#include <linux/auxvec.h>
#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/personality.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/host.h>
#include <asm/processor.h>
#include "wasm.h"

#define WASM_USER_STACK_SIZE	(256 * 1024)
#define WASM_MAX_IMAGE_SIZE	(64 * 1024 * 1024)

#define WASM_SECTION_CUSTOM		0
#define WASM_DYLINK_MEM_INFO		1

static const u8 wasm_magic[8] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };

struct wasm_dylink {
	u32 mem_size;
	u32 mem_align;		/* log2 */
	u32 table_size;
	u32 table_align;	/* log2 */
};

/* Read one unsigned LEB128 u32; returns false on overrun or overflow. */
static bool wasm_read_u32(const u8 **p, const u8 *end, u32 *val)
{
	u32 result = 0;
	unsigned int shift;

	for (shift = 0; shift < 35; shift += 7) {
		u8 byte;

		if (*p >= end)
			return false;
		byte = *(*p)++;
		result |= (u32)(byte & 0x7f) << shift;
		if (!(byte & 0x80)) {
			*val = result;
			return true;
		}
	}
	return false;
}

/*
 * Find the "dylink.0" section. The tool conventions require it to be the
 * first section of the module.
 */
static int wasm_parse_dylink(const u8 *image, size_t size,
			     struct wasm_dylink *info)
{
	static const char name[] = "dylink.0";
	const u8 *p = image + sizeof(wasm_magic);
	const u8 *end = image + size;
	const u8 *section_end;
	u32 id, len, name_len;

	if (!wasm_read_u32(&p, end, &id) || id != WASM_SECTION_CUSTOM)
		return -ENOEXEC;
	if (!wasm_read_u32(&p, end, &len) || len > end - p)
		return -ENOEXEC;
	section_end = p + len;

	if (!wasm_read_u32(&p, section_end, &name_len) ||
	    name_len != sizeof(name) - 1 || name_len > section_end - p ||
	    memcmp(p, name, name_len))
		return -ENOEXEC;
	p += name_len;

	while (p < section_end) {
		const u8 *sub_end;
		u32 type;

		if (!wasm_read_u32(&p, section_end, &type) ||
		    !wasm_read_u32(&p, section_end, &len) ||
		    len > section_end - p)
			return -ENOEXEC;
		sub_end = p + len;

		if (type == WASM_DYLINK_MEM_INFO) {
			if (!wasm_read_u32(&p, sub_end, &info->mem_size) ||
			    !wasm_read_u32(&p, sub_end, &info->mem_align) ||
			    !wasm_read_u32(&p, sub_end, &info->table_size) ||
			    !wasm_read_u32(&p, sub_end, &info->table_align))
				return -ENOEXEC;
			return 0;
		}
		p = sub_end;
	}

	/* No memory info: the module has no data and no table entries. */
	memset(info, 0, sizeof(*info));
	return 0;
}

/*
 * Build the start-up block below the argument strings that
 * transfer_args_to_stack() copied to the top of the stack.
 */
static int wasm_create_tables(struct linux_binprm *bprm)
{
	struct mm_struct *mm = current->mm;
	char __user *str = (char __user *)mm->start_stack;
	unsigned long words = 1 + bprm->argc + 1 + bprm->envc + 1 + 2 * 2;
	unsigned long __user *sp;
	int i;

	sp = (unsigned long __user *)
		round_down(mm->start_stack - words * sizeof(*sp), 16);
	mm->start_stack = (unsigned long)sp;

	if (put_user(bprm->argc, sp++))
		return -EFAULT;

	mm->arg_start = (unsigned long)str;
	for (i = 0; i < bprm->argc; i++) {
		size_t len = strnlen_user(str, MAX_ARG_STRLEN);

		if (!len || len > MAX_ARG_STRLEN || put_user((unsigned long)str, sp++))
			return -EFAULT;
		str += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;
	mm->arg_end = (unsigned long)str;

	mm->env_start = (unsigned long)str;
	for (i = 0; i < bprm->envc; i++) {
		size_t len = strnlen_user(str, MAX_ARG_STRLEN);

		if (!len || len > MAX_ARG_STRLEN || put_user((unsigned long)str, sp++))
			return -EFAULT;
		str += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;
	mm->env_end = (unsigned long)str;

	if (put_user(AT_PAGESZ, sp++) || put_user(PAGE_SIZE, sp++) ||
	    put_user(AT_NULL, sp++) || put_user(0, sp++))
		return -EFAULT;
	return 0;
}

static struct linux_binfmt wasm_format;

static int load_wasm_binary(struct linux_binprm *bprm)
{
	struct wasm_dylink dylink;
	unsigned long base, data, total;
	struct mm_struct *mm;
	loff_t size, pos = 0;
	ssize_t nread;
	u8 *image;
	int ret;

	if (memcmp(bprm->buf, wasm_magic, sizeof(wasm_magic)))
		return -ENOEXEC;

	size = i_size_read(file_inode(bprm->file));
	if (size > WASM_MAX_IMAGE_SIZE)
		return -ENOEXEC;

	image = kvmalloc(size, GFP_KERNEL);
	if (!image)
		return -ENOMEM;

	nread = kernel_read(bprm->file, image, size, &pos);
	if (nread != size) {
		ret = nread < 0 ? nread : -EIO;
		goto out;
	}

	ret = wasm_parse_dylink(image, size, &dylink);
	if (ret) {
		pr_warn_once("%s: not a position-independent module (no dylink.0 section)\n",
			     bprm->filename);
		goto out;
	}
	if (dylink.mem_align > PAGE_SHIFT) {
		ret = -ENOEXEC;
		goto out;
	}

	ret = begin_new_exec(bprm);
	if (ret)
		goto out;

	/* Point of no return: errors from here on kill the task. */
	set_personality(PER_LINUX);
	setup_new_exec(bprm);
	mm = current->mm;

	total = WASM_USER_STACK_SIZE + PAGE_ALIGN(dylink.mem_size);
	base = vm_mmap(NULL, 0, total, PROT_READ | PROT_WRITE | PROT_EXEC,
		       MAP_PRIVATE | MAP_ANONYMOUS, 0);
	if (IS_ERR_VALUE(base)) {
		ret = base;
		goto out;
	}
	data = base + WASM_USER_STACK_SIZE;

	/* Code is not in memory; there is no text range. */
	mm->start_code = mm->end_code = data;
	mm->start_data = data;
	mm->end_data = data + dylink.mem_size;
	/* No room for brk(): programs must allocate with mmap(). */
	mm->start_brk = mm->brk = mm->context.end_brk = base + total;

	set_binfmt(&wasm_format);

	mm->start_stack = data;
	ret = transfer_args_to_stack(bprm, &mm->start_stack);
	if (!ret)
		ret = wasm_create_tables(bprm);
	if (ret)
		goto out;

	finalize_exec(bprm);

	/*
	 * The host instantiates the program on the thread that will run it,
	 * which is not necessarily this one (a vfork child execs on its
	 * parent's thread), so only record it here.
	 * FIXME(wasm): the image is leaked if the task dies before it runs.
	 */
	kvfree(current->thread.exec_image);
	current->thread.exec_image = image;
	current->thread.exec_len = size;
	current->thread.exec_base = data;
	current->thread.exec_table = dylink.table_size;
	current->thread.exec_pending = 1;
	current->thread.user = 1;
	current->thread.user_sp = mm->start_stack;
	return 0;
out:
	kvfree(image);
	return ret;
}

/*
 * Have the host compile and instantiate the program recorded by exec, on
 * the calling thread. An error means the module cannot be compiled or
 * linked; exec has long succeeded by then, so the caller kills the task.
 */
int wasm_user_instantiate(void)
{
	struct thread_struct *t = &current->thread;
	int ret;

	ret = wasm_host_user_exec(t->exec_image, t->exec_len, t->exec_base,
				  t->exec_table, t->user_sp);
	kvfree(t->exec_image);
	t->exec_image = NULL;
	return ret;
}

static struct linux_binfmt wasm_format = {
	.load_binary = load_wasm_binary,
};

static int __init init_wasm_binfmt(void)
{
	register_binfmt(&wasm_format);
	return 0;
}
core_initcall(init_wasm_binfmt);
