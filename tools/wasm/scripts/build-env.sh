#!/bin/sh
# Build the container image with the wasm toolchain (see ../docker/Dockerfile).
# Works with docker or podman: CONTAINER=podman tools/wasm/scripts/build-env.sh
set -eu
. "$(dirname "$0")/common.sh"

"$CONTAINER" build -t "$IMAGE" "$WASM_TOOLS/docker"
"$CONTAINER" run --rm "$IMAGE" toolchain-versions
