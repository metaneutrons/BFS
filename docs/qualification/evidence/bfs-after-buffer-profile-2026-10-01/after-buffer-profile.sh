#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
export BFS_AMIGA_ASSETS_DIR="$PWD/build/benchmark/retained-final-deep/system"
export BFS_ROM_FILE=/Users/fabian/Documents/fswb/kickstart/A1200.47.102.rom
export BFS_PFS3_HANDLER="$BFS_AMIGA_ASSETS_DIR/L/pfs3aio"
export BFS_BENCH_FORMATTER_FILE="$PWD/build/benchmark/noop-fixed-formatter"
export BFS_BENCH_MODE=deep-compare
export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-path-depth/bfshandler-probe"
for order in bfs-first pfs3-first; do
    label=after-buffer-probe-$order
    export BFS_BENCH_ORDER="$order"
    export BFS_BENCH_RUN_DIR="$PWD/build/benchmark/$label"
    bash emulator-test/build-bench-image.sh > "build/$label-images.log" 2>&1
    rsync -az "$BFS_BENCH_RUN_DIR/" "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/"
done
for order in bfs-first pfs3-first; do
    label=after-buffer-probe-$order
    ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "xvfb-run -a env BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom BFS_BENCH_RUN_DIR=/tmp/bfs-perf-crc.UBHsLz/$label BFS_BENCH_MODE=deep-compare bash /tmp/bfs-perf-crc.UBHsLz/emulator-test/run-bench.sh 300" > "build/$label-run.log" 2>&1
    rsync -az --include='*/' --include='*.tsv' --include='*.txt' --include='bench.fs-uae' --include='fs-uae.log' --exclude='*' "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/" "build/benchmark/$label/"
    emulator-test/verify-bench-results.sh "build/benchmark/$label" deep-compare
    printf 'Verified %s\n' "$label"
done
