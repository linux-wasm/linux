// SPDX-License-Identifier: GPL-2.0
#include <linux/kbuild.h>
#include <asm/thread_info.h>

int main(void)
{
	/* The wasm assembler does not evaluate C constant expressions. */
	DEFINE(WASM_THREAD_SIZE, THREAD_SIZE);
	return 0;
}
