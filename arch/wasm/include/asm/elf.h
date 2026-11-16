/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WASM_ELF_H
#define _ASM_WASM_ELF_H

/* wasm is not ELF; this exists only because generic headers include it. */
#define ELF_CLASS	ELFCLASSNONE

typedef struct { } elf_gregset_t;
typedef struct { } elf_fpregset_t;

#endif
