#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
prefix=crc-native-order
suffixes=(probe-bfs-first probe-pfs3-first normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first)
for suffix in "${suffixes[@]}"; do
    [[ ! -e build/benchmark/$prefix-$suffix && ! -e build/$prefix-$suffix-images.log ]]
done
export BFS_BENCH_GUEST_FILE="$PWD/build/amiga/fs-compare-bench"
bash build/prepare-metadata-batch-measurements.sh baseline "$prefix"
bash build/prepare-metadata-batch-measurements.sh candidate "$prefix"
export BFS_BENCH_GUEST_FILE="$PWD/build/amiga-crc-native-order/fs-compare-bench"
bash build/prepare-metadata-batch-measurements.sh probe "$prefix"
