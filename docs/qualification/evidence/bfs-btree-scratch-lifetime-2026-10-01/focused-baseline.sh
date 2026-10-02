#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
expected=2322570d8fc5103aadd2c5d638e0eef098b5071ecf581df4970b712f0b0e6b23
actual=$(shasum -a 256 build/host-memory-copy-short/libbfs.a | awk '{print $1}')
[[ $actual == "$expected" ]]
shasum -a 256 tests/test_btree_scratch_lifetime.c build/host-memory-copy-short/libbfs.a
set -x
cc -std=c99 -Wall -Wextra -Werror -g -O2 -pthread -Iinclude -Itests \
    -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L tests/test_btree_scratch_lifetime.c \
    build/host-memory-copy-short/libbfs.a -o build/test_btree_scratch_lifetime_baseline
build/test_btree_scratch_lifetime_baseline
