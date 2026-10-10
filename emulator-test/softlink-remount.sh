#!/usr/bin/env bash
# Check that a soft link made through dos.library resolves after a remount on
# the AROS ROM: the first boot runs softpersist_53 on a fresh image and leaves
# the link behind, the second boot runs softpersist_54 on that image.
# Takes the environment of ci-test.sh; BFS_TEST_HDF and BFS_TEST_FILTER are set here.
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
log=$(mktemp)
trap 'rm -f "$log"' EXIT

BFS_TEST_HDF='' BFS_TEST_FILTER=softpersist_53 bash "$script_dir/ci-test.sh" | tee "$log"
first=$(sed -n 's/^Integration evidence directory: //p' "$log")
[[ -n "$first" && -f "$first/test.hdf" ]] || {
    printf 'ERROR: the first boot left no image.\n' >&2
    exit 1
}
BFS_TEST_HDF="$first/test.hdf" BFS_TEST_FILTER=softpersist_54 bash "$script_dir/ci-test.sh"
