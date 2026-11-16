#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Kbuild drives the linker as if it were an ELF ld with a linker script.
# This wrapper adapts those command lines to wasm-ld so that the generic
# link scripts (scripts/Makefile.vmlinux_o, scripts/link-vmlinux.sh) can be
# used unmodified:
#
#  - drops options wasm-ld does not have (--script, -z, --build-id, groups)
#  - for the final (non -r) link adds the wasm specific flags and brackets
#    the inputs with sections_head.o / sections_tail.o, which stand in for
#    the linker script (see arch/wasm/kernel/sections.h)
#
# Usage: wasm-ld.sh <real wasm-ld> <ld args...>
# Environment (set by arch/wasm/Makefile): WASM_LDFLAGS_FINAL,
# WASM_LD_HEAD, WASM_LD_TAIL.

real="$1"
shift

relocatable=
for arg; do
	case "$arg" in
	-v|--version|--help) exec "$real" "$@" ;;
	-r|--relocatable) relocatable=1 ;;
	esac
done

n=$#
skip=
while [ "$n" -gt 0 ]; do
	arg="$1"
	shift
	n=$((n - 1))
	if [ -n "$skip" ]; then
		skip=
		continue
	fi
	case "$arg" in
	--script=*|--build-id|--build-id=*|--start-group|--end-group|-X) ;;
	-z|-T|--script) skip=1 ;;
	*) set -- "$@" "$arg" ;;
	esac
done

if [ -n "$relocatable" ]; then
	exec "$real" "$@"
fi

# shellcheck disable=SC2086
exec "$real" $WASM_LDFLAGS_FINAL "$WASM_LD_HEAD" "$@" "$WASM_LD_TAIL"
