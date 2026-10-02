#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
prefix=single-block-absence
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=$prefix-$suffix
    if [[ $suffix == probe-* ]]; then mode=deep-compare; else mode=compare; fi
    [[ ! -e build/benchmark/$label/system/Results/complete.txt && ! -e build/$label-run.log ]]
    ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "xvfb-run -a env BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom BFS_BENCH_RUN_DIR=/tmp/bfs-perf-crc.UBHsLz/$label BFS_BENCH_MODE=$mode bash /tmp/bfs-perf-crc.UBHsLz/emulator-test/run-bench.sh 300" > "build/$label-run.log" 2>&1
    rsync -az --include='*/' --include='*.tsv' --include='*.txt' --include='bench.fs-uae' --include='fs-uae.log' --exclude='*' "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/" "build/benchmark/$label/"
    emulator-test/verify-bench-results.sh "build/benchmark/$label" "$mode"
    printf 'Verified %s\n' "$label"
done
