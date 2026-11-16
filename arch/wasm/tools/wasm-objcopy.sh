#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# scripts/Makefile.vmlinux post-processes the linked image with ELF-only
# objcopy operations on the .modinfo section. In a wasm module .modinfo is
# just bytes inside the data section, and llvm-objcopy cannot address it.
#
#  --set-section-flags .modinfo=noload <in> <out>   -> plain copy
#  --remove-section=.modinfo ... <file>             -> nothing
#  -j .modinfo -O binary <in> <out>                 -> empty output
#
# FIXME(wasm): .modinfo stays in the image and modules.builtin.modinfo is
# empty. Harmless without modules.
#
# Usage: wasm-objcopy.sh <real llvm-objcopy> <objcopy args...>

real="$1"
shift

for last; do :; done

case " $* " in
*" --set-section-flags .modinfo=noload "*)
	n=$#
	while [ "$n" -gt 2 ]; do shift; n=$((n - 1)); done
	exec cp "$1" "$2"
	;;
*" --remove-section=.modinfo "*)
	exit 0
	;;
*" -j .modinfo "*)
	: > "$last"
	exit 0
	;;
esac

exec "$real" "$@"
