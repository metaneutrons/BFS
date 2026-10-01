#!/bin/bash
set -euo pipefail
root=/Users/fabian/.codex/worktrees/bfs-append-performance/BFS
cd "$root"
remote=/tmp/bfs-perf-crc.UBHsLz
for order in bfs-first pfs3-first; do
    label=split-coalesce-attribution-probe-$order
    [[ ! -e build/$label-run.log ]]
    run_exit=0
    ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "BFS_BENCH_RUN_DIR='$remote/$label' BFS_BENCH_MODE=deep-compare BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom xvfb-run -a bash '$remote/source-split-coalesce-attribution/emulator-test/run-bench.sh' 300" > "build/$label-run.log" 2>&1 || run_exit=$?
    rsync -az --include='/bench.fs-uae' --include='/fs-uae.log' --include='/system/' --include='/system/Results/' --include='/system/Results/***' --exclude='*' "cachy:$remote/$label/" "build/benchmark/$label/"
    if [[ $run_exit -ne 0 ]]; then
        printf 'Diagnostic runner failed for %s: exit %s; raw outputs copied\n' "$label" "$run_exit" >&2
        exit "$run_exit"
    fi
    python3 build/split-coalesce-attribution-consumer/consume.py "build/benchmark/$label" --header build/split-coalesce-attribution-source/src/amiga/perf_probe.h --verifier emulator-test/verify-bench-results.sh > "build/$label-consumed.json"
    printf 'Verified %s\n' "$label"
done
