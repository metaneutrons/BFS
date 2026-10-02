#!/bin/bash
set -euo pipefail
root=/Users/fabian/.codex/worktrees/bfs-append-performance/BFS
cd "$root"
export BFS_AMIGA_ASSETS_DIR="$root/build/benchmark/retained-final-deep/system"
export BFS_ROM_FILE=/Users/fabian/Documents/fswb/kickstart/A1200.47.102.rom
export BFS_PFS3_HANDLER="$BFS_AMIGA_ASSETS_DIR/L/pfs3aio"
export BFS_BENCH_FORMATTER_FILE="$root/build/benchmark/noop-fixed-formatter"
export BFS_BENCH_MODE=deep-compare
export BFS_BENCH_HANDLER_FILE="$root/build/amiga-split-coalesce-attribution-first/bfshandler-probe"
export BFS_BENCH_GUEST_FILE="$root/build/amiga-split-coalesce-attribution-first/fs-compare-bench"
for order in bfs-first pfs3-first; do
    label=split-coalesce-attribution-probe-$order
    [[ ! -e build/benchmark/$label && ! -e build/$label-images.log ]]
    export BFS_BENCH_ORDER=$order
    export BFS_BENCH_RUN_DIR="$root/build/benchmark/$label"
    bash emulator-test/build-bench-image.sh > "build/$label-images.log" 2>&1
    rsync -az "$BFS_BENCH_RUN_DIR/" "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/"
    printf 'Prepared fresh %s\n' "$label"
done
