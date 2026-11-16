/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Syscall wrappers: every syscall is "long sys_x(const struct pt_regs *)".
 *
 * wasm functions are typed and an indirect call with the wrong type traps,
 * so the syscall table can only hold functions of one signature.
 *
 * Arguments arrive in six unsigned long slots, regs->syscall_args. Each
 * parameter takes one slot, except that a parameter wider than a long
 * (loff_t, u64) takes two consecutive slots, low word first, with no
 * alignment hole before the pair.
 *
 * Derived from arch/wasm/include/asm/syscall_wrapper.h in tombl/linux.
 */
#ifndef _ASM_WASM_SYSCALL_WRAPPER_H
#define _ASM_WASM_SYSCALL_WRAPPER_H

#include <asm/ptrace.h>

#define __WASM_SC_SLOTS(t) (sizeof(t) > sizeof(long) ? 2 : 1)

/* The parameter whose first slot is o, reassembled from its slots. */
#define __WASM_SC_ARG(o, t, a)						\
	(__WASM_SC_SLOTS(t) == 1 ?					\
		(unsigned long long)regs->syscall_args[o] :		\
		(unsigned long long)regs->syscall_args[o] |		\
		(unsigned long long)regs->syscall_args[(o) + __WASM_SC_SLOTS(t) - 1] << 32)

/* Like __MAP(), but passes m the first slot of each parameter. */
#define __WASM_SC_MAP0(o, m, ...)
#define __WASM_SC_MAP1(o, m, t, a, ...) m(o, t, a)
#define __WASM_SC_MAP2(o, m, t, a, ...) \
	m(o, t, a), __WASM_SC_MAP1(o + __WASM_SC_SLOTS(t), m, __VA_ARGS__)
#define __WASM_SC_MAP3(o, m, t, a, ...) \
	m(o, t, a), __WASM_SC_MAP2(o + __WASM_SC_SLOTS(t), m, __VA_ARGS__)
#define __WASM_SC_MAP4(o, m, t, a, ...) \
	m(o, t, a), __WASM_SC_MAP3(o + __WASM_SC_SLOTS(t), m, __VA_ARGS__)
#define __WASM_SC_MAP5(o, m, t, a, ...) \
	m(o, t, a), __WASM_SC_MAP4(o + __WASM_SC_SLOTS(t), m, __VA_ARGS__)
#define __WASM_SC_MAP6(o, m, t, a, ...) \
	m(o, t, a), __WASM_SC_MAP5(o + __WASM_SC_SLOTS(t), m, __VA_ARGS__)

/* The number of slots the parameters take. */
#define __WASM_SC_COUNT0(o, ...) (o)
#define __WASM_SC_COUNT1(o, t, a, ...) \
	__WASM_SC_COUNT0(o + __WASM_SC_SLOTS(t), __VA_ARGS__)
#define __WASM_SC_COUNT2(o, t, a, ...) \
	__WASM_SC_COUNT1(o + __WASM_SC_SLOTS(t), __VA_ARGS__)
#define __WASM_SC_COUNT3(o, t, a, ...) \
	__WASM_SC_COUNT2(o + __WASM_SC_SLOTS(t), __VA_ARGS__)
#define __WASM_SC_COUNT4(o, t, a, ...) \
	__WASM_SC_COUNT3(o + __WASM_SC_SLOTS(t), __VA_ARGS__)
#define __WASM_SC_COUNT5(o, t, a, ...) \
	__WASM_SC_COUNT4(o + __WASM_SC_SLOTS(t), __VA_ARGS__)
#define __WASM_SC_COUNT6(o, t, a, ...) \
	__WASM_SC_COUNT5(o + __WASM_SC_SLOTS(t), __VA_ARGS__)

#define __SYSCALL_DEFINEx(x, name, ...)					\
	asmlinkage long sys##name(const struct pt_regs *regs);		\
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);			\
	static long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__));	\
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__)); \
	asmlinkage long sys##name(const struct pt_regs *regs)		\
	{								\
		BUILD_BUG_ON(__WASM_SC_COUNT##x(0, __VA_ARGS__) >	\
			     ARRAY_SIZE(regs->syscall_args));		\
		return __se_sys##name(					\
			__WASM_SC_MAP##x(0, __WASM_SC_ARG, __VA_ARGS__)); \
	}								\
	static long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__))	\
	{								\
		long ret = __do_sys##name(__MAP(x, __SC_CAST, __VA_ARGS__)); \
		__MAP(x, __SC_TEST, __VA_ARGS__);			\
		__PROTECT(x, ret, __MAP(x, __SC_ARGS, __VA_ARGS__));	\
		return ret;						\
	}								\
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))

#define SYSCALL_DEFINE0(sname)						\
	SYSCALL_METADATA(_##sname, 0);					\
	asmlinkage long sys_##sname(const struct pt_regs *__unused);	\
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);			\
	asmlinkage long sys_##sname(const struct pt_regs *__unused)

/* Fallback for syscalls that are configured out: a typed weak function. */
#define COND_SYSCALL(name)						\
	asmlinkage long sys_##name(const struct pt_regs *regs);		\
	asmlinkage __weak long sys_##name(const struct pt_regs *regs)	\
	{								\
		return -ENOSYS;						\
	}

/* There is no compat (other-ABI) user space. */
#define COND_SYSCALL_COMPAT(name)

#endif
