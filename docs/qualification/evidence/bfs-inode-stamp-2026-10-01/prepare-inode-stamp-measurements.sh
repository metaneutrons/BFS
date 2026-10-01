#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
export BFS_AMIGA_ASSETS_DIR="$PWD/build/benchmark/retained-final-deep/system"
export BFS_ROM_FILE=/Users/fabian/Documents/fswb/kickstart/A1200.47.102.rom
export BFS_PFS3_HANDLER="$BFS_AMIGA_ASSETS_DIR/L/pfs3aio"
export BFS_BENCH_FORMATTER_FILE="$PWD/build/benchmark/noop-fixed-formatter"
export BFS_BENCH_MODE=deep-compare
export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-inode-stamp/bfshandler-probe"
for order in bfs-first pfs3-first; do
    label=inode-stamp-probe-$order
    export BFS_BENCH_ORDER="$order"
    export BFS_BENCH_RUN_DIR="$PWD/build/benchmark/$label"
    bash emulator-test/build-bench-image.sh > "build/$label-images.log" 2>&1
    rsync -az "$BFS_BENCH_RUN_DIR/" "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/"
    printf 'Prepared %s\n' "$label"
done
export BFS_BENCH_MODE=compare
labels=(baseline-bfs-first candidate-pfs3-first baseline-pfs3-first candidate-bfs-first repeat-candidate-bfs-first repeat-baseline-pfs3-first repeat-candidate-pfs3-first repeat-baseline-bfs-first)
for suffix in "${labels[@]}"; do
    label=inode-stamp-normal-$suffix
    if [[ $suffix == *candidate* ]]; then
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-inode-stamp/bfshandler"
    else
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-path-depth/bfshandler"
    fi
    if [[ $suffix == *pfs3-first ]]; then
        export BFS_BENCH_ORDER=pfs3-first
    else
        export BFS_BENCH_ORDER=bfs-first
    fi
    export BFS_BENCH_RUN_DIR="$PWD/build/benchmark/$label"
    bash emulator-test/build-bench-image.sh > "build/$label-images.log" 2>&1
    rsync -az "$BFS_BENCH_RUN_DIR/" "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/"
    printf 'Prepared %s\n' "$label"
done
