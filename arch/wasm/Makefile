# SPDX-License-Identifier: GPL-2.0
#
# WebAssembly (wasm32) architecture.
#
# Build with a stock LLVM (>= 19):
#   make ARCH=wasm LLVM=1 tinyconfig
#

# wasm objects are linked by wasm-ld, not by the ELF ld.lld that LLVM=1
# selects by default.
LD := $(LLVM_PREFIX)wasm-ld$(LLVM_SUFFIX)

# Kernel tasks run on separate host threads sharing one linear memory, so
# the kernel needs the threads (atomics) and bulk-memory features. Keep the
# feature set small so that every current browser/runtime can load it.
KBUILD_CFLAGS	+= -matomics -mbulk-memory -mmutable-globals -msign-ext \
		   -mnontrapping-fptoint
KBUILD_AFLAGS	+= -matomics -mbulk-memory

KBUILD_DEFCONFIG := tinyconfig
