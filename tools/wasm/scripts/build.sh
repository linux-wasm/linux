#!/bin/sh
# Configure and build the kernel the way CI and the boot test expect it:
# tinyconfig plus the test /init embedded as initramfs.
#
#   tools/wasm/scripts/build.sh [extra make arguments]
#
# To build without the test init, use kmake.sh directly:
#   kmake.sh tinyconfig && kmake.sh -j"$(nproc)"
set -eu
. "$(dirname "$0")/common.sh"
S="$WASM_TOOLS/scripts"

"$S/kmake.sh" tinyconfig
"$S/build-test-init.sh"
container_run scripts/config --file /out/.config \
	--set-str INITRAMFS_SOURCE /out/test-init/initramfs.list
"$S/kmake.sh" olddefconfig
"$S/kmake.sh" -j"$(nproc)" "$@"
