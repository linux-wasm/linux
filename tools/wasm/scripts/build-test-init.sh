#!/bin/sh
# Build the boot test's user programs (tools/wasm/tests/init/*.c) and the
# initramfs description that embeds them into the kernel image.
#
# Output in $KOUT/test-init/: init.wasm, hello.wasm, initramfs.list. The
# list is in usr/gen_init_cpio format and is used as
# CONFIG_INITRAMFS_SOURCE; its paths are the ones seen inside the build
# container.
set -eu
. "$(dirname "$0")/common.sh"

mkdir -p "$KOUT/test-init"

# A user program is a position-independent module that imports the kernel's
# shared memory; see arch/wasm/kernel/binfmt_wasm.c.
container_run sh -ec '
	llvm=/usr/lib/llvm-19/bin
	for prog in init hello; do
		$llvm/clang --target=wasm32-unknown-unknown -O2 -Wall -Werror \
			-fPIC -fvisibility=hidden -ffreestanding -nostdlib \
			-matomics -mbulk-memory \
			-c tools/wasm/tests/init/$prog.c -o /out/test-init/$prog.o
		$llvm/wasm-ld -pie --experimental-pic --no-entry \
			--export=_start --export-if-defined=__clone_entry \
			--import-memory --shared-memory --max-memory=4294967296 \
			/out/test-init/$prog.o -o /out/test-init/$prog.wasm
	done
'

cat > "$KOUT/test-init/initramfs.list" <<'LIST'
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
file /init /out/test-init/init.wasm 0755 0 0
file /hello /out/test-init/hello.wasm 0755 0 0
LIST

echo "test programs: $KOUT/test-init/{init,hello}.wasm"
