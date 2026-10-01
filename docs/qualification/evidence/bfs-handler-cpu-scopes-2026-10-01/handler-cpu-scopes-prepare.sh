#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
export BFS_AMIGA_ASSETS_DIR="$PWD/build/benchmark/retained-final-deep/system"
export BFS_ROM_FILE=/Users/fabian/Documents/fswb/kickstart/A1200.47.102.rom
export BFS_PFS3_HANDLER="$BFS_AMIGA_ASSETS_DIR/L/pfs3aio"
export BFS_BENCH_FORMATTER_FILE="$PWD/build/benchmark/noop-fixed-formatter"
export BFS_BENCH_MODE=deep-compare
labels=(control-bfs-first extended-pfs3-first control-pfs3-first extended-bfs-first)
for suffix in "${labels[@]}"; do
    label=handler-cpu-scopes-$suffix
    [[ ! -e build/$label-images.log && ! -e build/benchmark/$label ]]
    if [[ $suffix == control-* ]]; then
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-inode-stamp/bfshandler-probe"
        export BFS_BENCH_GUEST_FILE="$PWD/build/amiga/fs-compare-bench"
    else
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-handler-cpu-scopes/bfshandler-probe"
        export BFS_BENCH_GUEST_FILE="$PWD/build/amiga-handler-cpu-scopes/fs-compare-bench"
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
