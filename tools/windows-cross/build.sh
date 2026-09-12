#!/usr/bin/env bash
set -euo pipefail
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
recipe="$repo/tools/windows-cross"
commit=$(git -C "$repo" rev-parse HEAD)
output="$repo/out/windows-cross/${commit:0:7}"
snapshot=$(mktemp -d)
trap 'rm -rf "$snapshot"' EXIT
mkdir -p "$output"
git -C "$repo" archive "$commit" | tar -x -C "$snapshot"
export TIC80_CROSS_SNAPSHOT="$snapshot"
# Archive the pinned commits, not submodule working-tree changes or build output.
if git -C "$repo" submodule status --recursive | grep -q '^-'; then
    echo 'Initialize submodules first: git submodule update --init --recursive' >&2
    exit 1
fi
git -C "$repo" submodule foreach --quiet --recursive '
    mkdir -p "$TIC80_CROSS_SNAPSHOT/$displaypath"
    git archive "$sha1" > "$TIC80_CROSS_SNAPSHOT/submodule.tar" &&
    tar -xf "$TIC80_CROSS_SNAPSHOT/submodule.tar" -C "$TIC80_CROSS_SNAPSHOT/$displaypath" &&
    rm "$TIC80_CROSS_SNAPSHOT/submodule.tar"
'
printf '%s\n' "$commit" > "$output/commit.txt"
git -C "$repo" ls-tree -r "$commit" | awk '$1 == "160000"' > "$output/submodules.txt"
docker run --rm \
    --mount "type=bind,src=$snapshot,dst=/input,readonly" \
    --mount "type=bind,src=$recipe,dst=/recipe,readonly" \
    --mount "type=bind,src=$output,dst=/output" \
    -e "HOST_UID=$(id -u)" -e "HOST_GID=$(id -g)" \
    debian:trixie bash /recipe/container.sh 2>&1 | tee "$output/build.log"
printf '\nWindows PRO build: %s/tic80-pro.exe\n' "$output"
