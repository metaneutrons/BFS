#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
labels=(control-bfs-first extended-pfs3-first control-pfs3-first extended-bfs-first)
for suffix in "${labels[@]}"; do
    label=handler-cpu-scopes-$suffix
    [[ ! -e build/$label-run.log && ! -e build/benchmark/$label/system/Results/complete.txt ]]
    ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "xvfb-run -a env BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom BFS_BENCH_RUN_DIR=/tmp/bfs-perf-crc.UBHsLz/$label BFS_BENCH_MODE=deep-compare bash /tmp/bfs-perf-crc.UBHsLz/emulator-test/run-bench.sh 300" > "build/$label-run.log" 2>&1
    rsync -az --include='*/' --include='*.tsv' --include='*.txt' --include='bench.fs-uae' --include='fs-uae.log' --exclude='*' "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/" "build/benchmark/$label/"
    emulator-test/verify-bench-results.sh "build/benchmark/$label" deep-compare
    printf 'Verified %s\n' "$label"
done
