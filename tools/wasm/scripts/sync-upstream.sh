#!/bin/sh
# Bring the wasm port up to a newer upstream (stable) tag.
#
#   tools/wasm/scripts/sync-upstream.sh v7.3.5            # merge (default)
#   tools/wasm/scripts/sync-upstream.sh --rebase v7.4
#
# --merge   Merge the tag into the current branch. History is preserved, so
#           this is the mode for the published branch that pull requests
#           target. Conflicts can only come from the few generic files the
#           port touches (see tools/wasm/NOTES.md).
#
# --rebase  Replay every non-upstream commit on top of the tag, producing a
#           clean patch series. This rewrites history (the old tip is kept
#           as <branch>-on-<oldtag>) and needs a force push; use it for a
#           new branch per kernel release, not for a branch others pull.
#
# Tags are fetched from the "upstream" remote (kernel.org stable).
set -eu
. "$(dirname "$0")/common.sh"

mode=merge
case "${1:-}" in
--merge) shift ;;
--rebase) mode=rebase; shift ;;
esac
NEW_TAG="${1:?usage: $0 [--merge|--rebase] <upstream tag, e.g. v7.3.5>}"

cd "$REPO"
BRANCH="$(git symbolic-ref --short HEAD)"

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "error: uncommitted changes in $REPO" >&2
	exit 1
fi
if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
	echo "error: shallow clone; run 'git fetch --unshallow upstream' first" >&2
	exit 1
fi

# Newest upstream release tag contained in the branch.
OLD_TAG="$(git describe --tags --abbrev=0 --match 'v[0-9]*' HEAD)"
if [ "$OLD_TAG" = "$NEW_TAG" ]; then
	echo "$BRANCH is already based on $NEW_TAG"
	exit 0
fi

git fetch --no-tags upstream "refs/tags/$NEW_TAG:refs/tags/$NEW_TAG"

echo "$BRANCH: $OLD_TAG -> $NEW_TAG ($mode," \
	"$(git rev-list --count --no-merges "$OLD_TAG..HEAD") wasm commits)"

if [ "$mode" = merge ]; then
	git merge --no-ff -m "Merge tag '$NEW_TAG' into $BRANCH" "$NEW_TAG"
else
	git branch -f "$BRANCH-on-$OLD_TAG" HEAD
	git rebase --onto "$NEW_TAG" "$OLD_TAG" "$BRANCH"
	echo "previous tip saved as $BRANCH-on-$OLD_TAG"
fi

echo
echo "now rebuild and test:"
echo "  tools/wasm/scripts/kmake.sh tinyconfig"
echo "  tools/wasm/scripts/kmake.sh -j\$(nproc)"
echo "  tools/wasm/scripts/test-boot.sh"
echo "and update the versions in tools/wasm/NOTES.md"
