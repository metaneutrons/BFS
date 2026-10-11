#!/usr/bin/env bash
# Boot the images that the Linux FUSE qualification wrote with the freshly
# built Amiga handler on the AROS ROM, run the image test for each, and check
# the result on the host against the image as Linux left it: linuxread_56
# reads and changes the normal image, linuxreap_57 mounts the interrupted one,
# whose unlinked but open file the writable mount reclaims.
# Takes the environment of ci-test.sh; BFS_TEST_HDF and BFS_TEST_FILTER are set here.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    printf 'Usage: %s NORMAL_IMAGE INTERRUPTED_IMAGE\n' "$0" >&2
    exit 2
fi

script_dir=$(cd "$(dirname "$0")" && pwd)
log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_case() {
    local image=$1 test=$2 case=$3 run
    [[ -f "$image" ]] || {
        printf 'ERROR: image not found: %s\n' "$image" >&2
        exit 2
    }
    BFS_TEST_HDF="$image" BFS_TEST_FILTER="$test" bash "$script_dir/ci-test.sh" | tee "$log"
    run=$(sed -n 's/^Integration evidence directory: //p' "$log")
    [[ -n "$run" && -f "$run/test.hdf" ]] || {
        printf 'ERROR: %s left no image.\n' "$test" >&2
        exit 1
    }
    python3 "$script_dir/linux-exchange.py" --case "$case" --before "$image" \
        --after "$run/test.hdf" | tee "$run/linux-exchange.json"
}

run_case "$1" linuxread_56 read
run_case "$2" linuxreap_57 reap
