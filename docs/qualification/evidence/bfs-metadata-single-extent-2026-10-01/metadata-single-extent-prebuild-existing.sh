#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
mode=${1:?normal or sanitizer}
case $mode in
    normal) build_dir=build/host-metadata-single-final ;;
    sanitizer) build_dir=build/host-metadata-single-final-sanitizer ;;
    *) exit 2 ;;
esac
targets=()
for test_source in tests/test_*.c; do
    case $test_source in
        tests/test_metadata_withdrawal_batch.c|tests/test_metadata_withdrawal_batch_faults.c) continue ;;
    esac
    test_name=${test_source#tests/}
    targets+=("$build_dir/${test_name%.c}")
done
if [[ $mode == normal ]]; then
    make -j4 BUILD_HOST="$build_dir" "${targets[@]}"
else
    make -j4 BUILD_HOST="$build_dir" \
        HOST_CFLAGS='-std=c99 -Wall -Wextra -Werror -g -O1 -pthread -fno-omit-frame-pointer -fsanitize=address,undefined -Iinclude -Itests -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L' \
        "${targets[@]}"
fi
