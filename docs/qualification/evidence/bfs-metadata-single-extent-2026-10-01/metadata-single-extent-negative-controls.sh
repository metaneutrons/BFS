#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
core_sources=()
for source_file in src/core/*.c; do
    [[ $source_file == src/core/alloc.c ]] || core_sources+=("$source_file")
done
for guard in published headroom; do
    build_dir=build/host-metadata-single-negative-$guard
    mkdir -p "$build_dir"
    cc -std=c99 -Wall -Wextra -Werror -g -O2 -pthread \
        -Iinclude -Itests -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L \
        -o "$build_dir/test_metadata_withdrawal_batch" \
        docs/qualification/evidence/bfs-metadata-batch-2026-10-01/test_metadata_withdrawal_batch.c \
        "build/metadata-single-extent-without-$guard-guard.c" \
        "${core_sources[@]}" tests/block_device_emu.c \
        > "build/metadata-single-extent-negative-$guard-build.log" 2>&1
    set +e
    (cd "$build_dir" && ./test_metadata_withdrawal_batch) \
        > "build/metadata-single-extent-negative-$guard-run.log" 2>&1
    result=$?
    set -e
    [[ $result -eq 1 ]]
    grep -F '15 passed, 1 failed' "build/metadata-single-extent-negative-$guard-run.log"
    printf 'V2 %s guard mutant failed the preserved V1 functional oracle as required\n' "$guard"
done
