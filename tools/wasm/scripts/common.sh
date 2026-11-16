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

# Run a command in the build container with the repository at /linux and
# $KOUT at /out, as the calling user.
# label=disable: let the container read the bind mounts on SELinux hosts
# without relabelling the user's files.
container_run() {
	mkdir -p "$KOUT"
	"$CONTAINER" run --rm ${CONTAINER_TTY:+-it} \
		--security-opt label=disable \
		--user "$(id -u):$(id -g)" \
		-e HOME=/tmp \
		-e KBUILD_BUILD_USER="${KBUILD_BUILD_USER:-wasm}" \
		-e KBUILD_BUILD_HOST="${KBUILD_BUILD_HOST:-builder}" \
		${KBUILD_BUILD_TIMESTAMP:+-e KBUILD_BUILD_TIMESTAMP="$KBUILD_BUILD_TIMESTAMP"} \
		-v "$REPO:/linux" -v "$KOUT:/out" -w /linux \
		"$IMAGE" "$@"
}
