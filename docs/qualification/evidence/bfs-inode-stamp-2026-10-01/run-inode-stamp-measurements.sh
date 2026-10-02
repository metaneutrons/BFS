#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
for order in bfs-first pfs3-first; do
    label=inode-stamp-probe-$order
    ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "xvfb-run -a env BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom BFS_BENCH_RUN_DIR=/tmp/bfs-perf-crc.UBHsLz/$label BFS_BENCH_MODE=deep-compare bash /tmp/bfs-perf-crc.UBHsLz/emulator-test/run-bench.sh 300" > "build/$label-run.log" 2>&1
    rsync -az --include='*/' --include='*.tsv' --include='*.txt' --include='bench.fs-uae' --include='fs-uae.log' --exclude='*' "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/" "build/benchmark/$label/"
    emulator-test/verify-bench-results.sh "build/benchmark/$label" deep-compare
    printf 'Verified %s\n' "$label"
done
labels=(baseline-bfs-first candidate-pfs3-first baseline-pfs3-first candidate-bfs-first repeat-candidate-bfs-first repeat-baseline-pfs3-first repeat-candidate-pfs3-first repeat-baseline-bfs-first)
for suffix in "${labels[@]}"; do
    label=inode-stamp-normal-$suffix
    ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "xvfb-run -a env BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom BFS_BENCH_RUN_DIR=/tmp/bfs-perf-crc.UBHsLz/$label BFS_BENCH_MODE=compare bash /tmp/bfs-perf-crc.UBHsLz/emulator-test/run-bench.sh 300" > "build/$label-run.log" 2>&1
    rsync -az --include='*/' --include='*.tsv' --include='*.txt' --include='bench.fs-uae' --include='fs-uae.log' --exclude='*' "cachy:/tmp/bfs-perf-crc.UBHsLz/$label/" "build/benchmark/$label/"
    emulator-test/verify-bench-results.sh "build/benchmark/$label" compare
    printf 'Verified %s\n' "$label"
done
