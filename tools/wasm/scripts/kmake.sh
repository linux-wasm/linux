#!/bin/sh
# Run kbuild for ARCH=wasm inside the build container.
#
#   tools/wasm/scripts/kmake.sh tinyconfig
#   tools/wasm/scripts/kmake.sh -j"$(nproc)"
#
# Output goes to $KOUT (default build/wasm/ in the repository), so the
# source tree stays clean. Set KMAKE_SHELL=1 for an interactive shell.
# For the usual "configure, add the test init, build" see build.sh.
set -eu
. "$(dirname "$0")/common.sh"

if [ "${KMAKE_SHELL:-}" = 1 ]; then
	CONTAINER_TTY=1 container_run bash
	exit
fi

# LLVM=<dir>/ makes kbuild use clang, llvm-ar, ... from that directory (and
# clang + lld for host tools); arch/wasm/Makefile picks wasm-ld from there.
container_run make -C /linux O=/out ARCH=wasm LLVM=/usr/lib/llvm-19/bin/ "$@"
