#!/usr/bin/env bash
# Qualify v0.1.3 Amiga handler interoperability with Linux-written v2 images.
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
fuse_binary="$project_directory/build/host/bfs-fuse"
checker="$project_directory/build/host/bfsfsck"
oracle="$project_directory/tools/bfs-format-oracle.py"

[[ -f "$normal_image" && -f "$interrupted_image" ]] || {
    printf 'ERROR: both input images must exist.\n' >&2
    exit 2
}
[[ -x "$fuse_binary" && -x "$checker" ]] || {
    printf 'ERROR: build bfs-fuse and bfsfsck before baseline qualification.\n' >&2
    exit 2
}
command -v fusermount3 >/dev/null 2>&1 || {
    printf 'ERROR: fusermount3 is required.\n' >&2
    exit 2
}
command -v mountpoint >/dev/null 2>&1 || {
    printf 'ERROR: mountpoint is required.\n' >&2
    exit 2
}

mkdir -p "$output_directory"
shopt -s nullglob
git -C "$project_directory" fetch --depth=1 origin "$baseline_commit"
git -C "$project_directory" worktree add --detach "$baseline_directory" "$baseline_commit"
cleanup() {
    git -C "$project_directory" worktree remove --force "$baseline_directory" >/dev/null 2>&1 || true
}
trap cleanup EXIT

make -C "$baseline_directory" amiga amiga-test AMIGA_PREFIX="${AMIGA_PREFIX:?AMIGA_PREFIX is required}"

mount_and_check() {
    local image=$1
    local writable=$2
    local mount_directory
    mount_directory=$(mktemp -d "$output_directory/fuse-mount.XXXXXX")
    local log="$mount_directory/fuse.log"
    local -a arguments=("$fuse_binary" --image "$image")
    if [[ "$writable" == true ]]; then
        arguments+=(--read-write)
    fi
    arguments+=("$mount_directory")
    "${arguments[@]}" >"$log" 2>&1 &
    local fuse_pid=$!
    local mounted=false
    for _ in $(seq 1 200); do
        if mountpoint -q "$mount_directory"; then
            mounted=true
            break
        fi
        if ! kill -0 "$fuse_pid" 2>/dev/null; then
            break
        fi
        sleep 0.05
    done
    if [[ "$mounted" != true ]]; then
        wait "$fuse_pid" || true
        cat "$log" >&2
        command rm -rf "$mount_directory"
        printf 'ERROR: FUSE did not mount %s.\n' "$image" >&2
        exit 1
    fi
    test -f "$mount_directory/oracle.txt"
    fusermount3 -u "$mount_directory"
    wait "$fuse_pid"
    command rm -rf "$mount_directory"
}

qualify_image() {
    local label=$1
    local input_image=$2
    local runtime_directory="$output_directory/baseline-$label-runtime"
    local output_image="$output_directory/baseline-$label.bfs"

    python3 "$oracle" "$input_image" >/dev/null
    "$checker" "$input_image" >/dev/null
    BFS_HANDLER="$baseline_directory/build/amiga/bfshandler" \
    BFS_TEST_BINARY="$baseline_directory/build/amiga/bfs-test" \
    BFS_TEST_HDF="$input_image" \
    BFS_TEST_FILTER=basic_01 \
    BFS_TEST_PROFILE=quick \
    BFS_TEST_TIMEOUT=300 \
    BFS_EMULATOR_RUNTIME="$runtime_directory" \
    "$project_directory/emulator-test/ci-test.sh"

    local -a images=("$runtime_directory"/run.*/test.hdf)
    [[ ${#images[@]} -eq 1 && -f ${images[0]} ]] || {
        printf 'ERROR: baseline %s run did not produce one image.\n' "$label" >&2
        exit 1
    }
    command cp -f "${images[0]}" "$output_image"
    python3 "$oracle" "$output_image" >/dev/null
    "$checker" "$output_image" >/dev/null
    mount_and_check "$output_image" false
    mount_and_check "$output_image" true
    python3 "$oracle" "$output_image" >/dev/null
    "$checker" "$output_image" >/dev/null
}

qualify_image normal "$normal_image"
qualify_image interrupted "$interrupted_image"
