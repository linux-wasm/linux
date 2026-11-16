# SPDX-License-Identifier: GPL-2.0
#
# WebAssembly (wasm32) architecture.
#
# Build with a stock LLVM (>= 19):
#   make ARCH=wasm LLVM=1 tinyconfig
#   make ARCH=wasm LLVM=1
#

# wasm objects are linked by wasm-ld, not by the ELF ld.lld that LLVM=1
# selects by default. Kbuild's link steps assume an ELF linker with linker
# script support, so wasm-ld (and objcopy) are run through small wrappers
# instead of patching the generic link scripts. See arch/wasm/tools/.
WASM_LD		:= $(LLVM_PREFIX)wasm-ld$(LLVM_SUFFIX)
LD		:= $(CONFIG_SHELL) $(srctree)/arch/wasm/tools/wasm-ld.sh $(WASM_LD)
OBJCOPY		:= $(CONFIG_SHELL) $(srctree)/arch/wasm/tools/wasm-objcopy.sh \
		   $(LLVM_PREFIX)llvm-objcopy$(LLVM_SUFFIX)

# Kernel tasks run on separate host threads sharing one linear memory, so
# the kernel needs the threads (atomics) and bulk-memory features. Keep the
# feature set small so that every current browser/runtime can load it.
KBUILD_CFLAGS	+= -matomics -mbulk-memory -mmutable-globals -msign-ext \
		   -mnontrapping-fptoint
KBUILD_AFLAGS	+= -matomics -mbulk-memory

# EXPORT_SYMBOL() emits ELF-only inline assembly, and there are no modules
# that could use the exports anyway.
KBUILD_CFLAGS	+= -D__DISABLE_EXPORTS
KBUILD_AFLAGS	+= -D__DISABLE_EXPORTS

# Flags for the final link only (wasm-ld.sh adds them when not linking -r).
#
#  --import-memory/--shared-memory: the host creates one shared memory and
#      instantiates the module once per kernel task.
#  --max-memory: upper bound the host may pick for that memory (1 GiB).
#  --no-merge-data-segments, --no-gc-sections: required by the
#      sections_head.o/sections_tail.o scheme, see kernel/sections.h.
#  -z stack-size: the linker-provided stack is never used; every task
#      (including init_task) has its own stack.
WASM_LDFLAGS_FINAL := --no-entry --no-gc-sections --no-merge-data-segments \
		      --import-memory --shared-memory --max-memory=1073741824 \
		      -z stack-size=4096 --error-limit=0 -Map=vmlinux.map \
		      --export=_start --export=wasm_thread_entry \
		      --export=wasm_raise_irq
WASM_LD_HEAD	:= arch/wasm/kernel/sections_head.o
WASM_LD_TAIL	:= arch/wasm/kernel/sections_tail.o
export WASM_LDFLAGS_FINAL WASM_LD_HEAD WASM_LD_TAIL

libs-y += arch/wasm/lib/

KBUILD_DEFCONFIG := tinyconfig

# modpost only understands ELF and silently skips the wasm vmlinux.o, so it
# never writes the (for us empty) list of exported symbols that
# scripts/Makefile.vmlinux wants to compile. Provide it.
archprepare: .vmlinux.export.c

.vmlinux.export.c:
	$(Q)echo '/* wasm: EXPORT_SYMBOL is disabled, nothing to export */' > $@

CLEAN_FILES += .vmlinux.export.c

# The linked vmlinux already is a wasm module; give it a recognisable name.
all: vmlinux.wasm

quiet_cmd_wasm_copy = COPY    $@
      cmd_wasm_copy = cp $< $@

vmlinux.wasm: vmlinux FORCE
	$(call if_changed,wasm_copy)

targets += vmlinux.wasm
CLEAN_FILES += vmlinux.wasm vmlinux.map

define archhelp
  echo  '* vmlinux.wasm    - Kernel as a WebAssembly module (copy of vmlinux)'
endef
