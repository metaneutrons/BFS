#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
phase=${1:?baseline, candidate or probe is required}
prefix=${2:-metadata-batch}
[[ $prefix =~ ^[a-z0-9-]+$ ]]
export BFS_AMIGA_ASSETS_DIR="$PWD/build/benchmark/retained-final-deep/system"
export BFS_ROM_FILE=/Users/fabian/Documents/fswb/kickstart/A1200.47.102.rom
export BFS_PFS3_HANDLER="$BFS_AMIGA_ASSETS_DIR/L/pfs3aio"
export BFS_BENCH_FORMATTER_FILE="$PWD/build/benchmark/noop-fixed-formatter"
case "$phase" in
    baseline)
        export BFS_BENCH_MODE=compare
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-inode-stamp/bfshandler"
        suffixes=(normal-baseline-bfs-first normal-baseline-pfs3-first normal-repeat-baseline-pfs3-first normal-repeat-baseline-bfs-first)
        ;;
    candidate)
        export BFS_BENCH_MODE=compare
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-$prefix/bfshandler"
        suffixes=(normal-candidate-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-candidate-pfs3-first)
        ;;
    probe)
        export BFS_BENCH_MODE=deep-compare
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-$prefix/bfshandler-probe"
        suffixes=(probe-bfs-first probe-pfs3-first)
        ;;
    *) printf 'Invalid preparation phase: %s\n' "$phase" >&2; exit 2 ;;
esac
for suffix in "${suffixes[@]}"; do
    label=$prefix-$suffix
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
