#!/bin/sh
# Run kbuild for ARCH=wasm inside the build container.
#
#   tools/wasm/scripts/kmake.sh tinyconfig
#   tools/wasm/scripts/kmake.sh -j"$(nproc)"
#
# Output goes to $KOUT (default build/wasm/ in the repository), so the
# source tree stays clean. Set KMAKE_SHELL=1 for an interactive shell.
set -eu
. "$(dirname "$0")/common.sh"

mkdir -p "$KOUT"

# label=disable: let the container read the bind mounts on SELinux hosts
# without relabelling the user's files.
run() {
	"$CONTAINER" run --rm ${KMAKE_TTY:+-it} \
		--security-opt label=disable \
		--user "$(id -u):$(id -g)" \
		-e HOME=/tmp \
		-e KBUILD_BUILD_USER="${KBUILD_BUILD_USER:-wasm}" \
		-e KBUILD_BUILD_HOST="${KBUILD_BUILD_HOST:-builder}" \
		${KBUILD_BUILD_TIMESTAMP:+-e KBUILD_BUILD_TIMESTAMP="$KBUILD_BUILD_TIMESTAMP"} \
		-v "$REPO:/linux" -v "$KOUT:/out" -w /linux \
		"$IMAGE" "$@"
}

if [ "${KMAKE_SHELL:-}" = 1 ]; then
	KMAKE_TTY=1 run bash
	exit
fi

# LLVM=<dir>/ makes kbuild use clang, llvm-ar, ... from that directory (and
# clang + lld for host tools); arch/wasm/Makefile picks wasm-ld from there.
run make -C /linux O=/out ARCH=wasm LLVM=/usr/lib/llvm-19/bin/ "$@"
