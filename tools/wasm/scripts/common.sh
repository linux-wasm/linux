# Shared settings for the tools/wasm scripts. Sourced, not executed.
#
#   REPO    kernel source tree (this repository)
#   KOUT    kbuild output directory, default <repo>/build/wasm. Kbuild drops
#           a .gitignore into it, so it never shows up in git status.
#   IMAGE   build container image
#   CONTAINER  docker (default) or podman

WASM_TOOLS="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$WASM_TOOLS/../.." && pwd)"
KOUT="${KOUT:-$REPO/build/wasm}"
CONTAINER="${CONTAINER:-docker}"
IMAGE="${LINUX_WASM_IMAGE:-linux-wasm-build:llvm19}"
