#!/bin/sh
# Boot test: run the built kernel headless under node and check that it
# gets as far as it is currently expected to.
#
#   tools/wasm/scripts/test-boot.sh [extra node-run.mjs options]
#
# Pass criteria are the lines in ../tests/boot-expect.txt (fixed strings,
# all must appear in the kernel log, in that order). Update that file when
# the kernel learns to boot further.
set -eu
. "$(dirname "$0")/common.sh"

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

status=0
node "$WASM_TOOLS/host/node-run.mjs" --kernel "$KOUT/vmlinux.wasm" \
	--timeout "${BOOT_TIMEOUT:-30}" "$@" > "$log" 2>&1 || status=$?

fail=0
line=0
while IFS= read -r expect; do
	case "$expect" in ''|'#'*) continue ;; esac
	next="$(tail -n "+$((line + 1))" "$log" | grep -n -F -m1 -- "$expect" | cut -d: -f1)"
	if [ -z "$next" ]; then
		echo "FAIL: missing (or out of order): $expect"
		fail=1
	else
		line=$((line + next))
	fi
done < "$WASM_TOOLS/tests/boot-expect.txt"

if [ "$fail" -ne 0 ]; then
	echo "--- kernel log (node-run exit status $status) ---"
	cat "$log"
	exit 1
fi
echo "PASS: boot test ($(grep -c . "$log") log lines, node-run exit status $status)"
