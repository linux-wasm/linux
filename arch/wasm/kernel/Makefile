# SPDX-License-Identifier: GPL-2.0

obj-y += console.o entry.o irq.o process.o sections.o setup.o syscall.o time.o vfork.o
obj-$(CONFIG_BINFMT_WASM) += binfmt_wasm.o
obj-$(CONFIG_PROC_FS) += cpuinfo.o

# Linked first and last by arch/wasm/tools/wasm-ld.sh to bracket the
# sections that a linker script would normally provide symbols for.
always-$(KBUILD_BUILTIN) += sections_head.o sections_tail.o vmlinux.lds
