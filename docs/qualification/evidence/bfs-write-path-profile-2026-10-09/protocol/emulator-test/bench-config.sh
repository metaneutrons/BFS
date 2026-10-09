#!/usr/bin/env bash
# Shared validation for benchmark cache capacities.

readonly BFS_BENCH_DEFAULT_BUFFERS=30

bfs_bench_validate_buffers() {
    local value=$1
    [[ "$value" =~ ^[0-9]+$ ]] || return 1
    while [[ ${#value} -gt 1 && ${value:0:1} == 0 ]]; do value=${value#0}; done
    [[ "$value" =~ ^([1-9]|[1-9][0-9]|1[01][0-9]|12[0-8])$ ]] || return 1
    printf '%s' "$value"
}

bfs_bench_resolve_buffers() {
    bfs_bench_validate_buffers "${BFS_BENCH_BUFFERS-$BFS_BENCH_DEFAULT_BUFFERS}"
}
