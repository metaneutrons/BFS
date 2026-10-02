#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
prefix=reserve-pair-validation
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    [[ ! -e build/benchmark/$prefix-$suffix && ! -e build/$prefix-$suffix-images.log ]]
done
export BFS_AMIGA_ASSETS_DIR="$PWD/build/benchmark/retained-final-deep/system"
export BFS_ROM_FILE=/Users/fabian/Documents/fswb/kickstart/A1200.47.102.rom
export BFS_PFS3_HANDLER="$BFS_AMIGA_ASSETS_DIR/L/pfs3aio"
export BFS_BENCH_FORMATTER_FILE="$PWD/build/benchmark/noop-fixed-formatter"
for suffix in "${suffixes[@]}"; do
    label=$prefix-$suffix
    if [[ $suffix == probe-* ]]; then
        export BFS_BENCH_MODE=deep-compare
        export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-$prefix/bfshandler-probe"
        export BFS_BENCH_GUEST_FILE="$PWD/build/amiga-$prefix/fs-compare-bench"
    else
        export BFS_BENCH_MODE=compare
        export BFS_BENCH_GUEST_FILE="$PWD/build/amiga/fs-compare-bench"
        if [[ $suffix == *candidate* ]]; then
            export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-$prefix/bfshandler"
        else
            export BFS_BENCH_HANDLER_FILE="$PWD/build/amiga-single-block-absence/bfshandler"
        fi
    fi
    if [[ $suffix == *pfs3-first ]]; then export BFS_BENCH_ORDER=pfs3-first
    else export BFS_BENCH_ORDER=bfs-first; fi
    export BFS_BENCH_RUN_DIR="$PWD/build/benchmark/$label"
    bash emulator-test/build-bench-image.sh > "build/$label-images.log" 2>&1
    rsync -az "$BFS_BENCH_RUN_DIR/" "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/"
    printf 'Prepared %s\n' "$label"
done
