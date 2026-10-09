#!/usr/bin/env bash
# The v0.1.3 Amiga handler implements format v2 only. It must refuse
# Linux-written v3 images with its own diagnosis and leave them unchanged.
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf 'Usage: %s NORMAL_IMAGE INTERRUPTED_IMAGE OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi

normal_image=$1
interrupted_image=$2
output_directory=$3
project_directory=$(cd "$(dirname "$0")/../.." && pwd)
baseline_commit=431ead6159e5d4217f029ac2b6dd02a51db7d8a2
baseline_directory="$output_directory/baseline-v0.1.3"
bfs="$project_directory/build/host/bfs"
oracle="$project_directory/tools/bfs-format-oracle.py"

[[ -f "$normal_image" && -f "$interrupted_image" ]] || {
    printf 'ERROR: both input images must exist.\n' >&2
    exit 2
}
[[ -x "$bfs" ]] || {
    printf 'ERROR: build bfs before baseline qualification.\n' >&2
    exit 2
}

mkdir -p "$output_directory"
git -C "$project_directory" fetch --depth=1 origin refs/tags/v0.1.3:refs/tags/v0.1.3
[[ $(git -C "$project_directory" rev-parse v0.1.3^{}) == "$baseline_commit" ]] || {
    printf 'ERROR: v0.1.3 does not resolve to the pinned baseline commit.\n' >&2
    exit 1
}
git -C "$project_directory" worktree add --detach "$baseline_directory" "$baseline_commit"
cleanup() {
    git -C "$project_directory" worktree remove --force "$baseline_directory" >/dev/null 2>&1 || true
}
trap cleanup EXIT

amiga_prefix=${AMIGA_PREFIX:?AMIGA_PREFIX is required}
make -C "$baseline_directory" amiga AMIGA_PREFIX="$amiga_prefix"
make -C "$project_directory" build/amiga/compatibility-probe AMIGA_PREFIX="$amiga_prefix"

for image in "$normal_image" "$interrupted_image"; do
    python3 "$oracle" "$image" >/dev/null
    "$bfs" check "$image" >/dev/null
done
python3 "$project_directory/emulator-test/compatibility-test.py" \
    --handler "$baseline_directory/build/amiga/bfshandler" --supported-version 2 \
    --refuse "$normal_image" "$interrupted_image"
