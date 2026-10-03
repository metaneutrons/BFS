#!/usr/bin/env bash
# Interleave fresh compare runs of several BFS handlers against PFS3.
# Usage: bench-series.sh LABEL COUNT MODE NAME=HANDLER [NAME=HANDLER...]
# Run i of every handler starts with BFS when i is odd and with PFS3 when it
# is even, so order effects reach each handler equally. Results land in
# build/benchmark/LABEL-NAME-I-ORDER; summarize them with
# tools/bench-summary.py LABEL-NAME... . The HDFs are removed after each run.
set -euo pipefail

if [[ $# -lt 4 ]]; then
    printf 'Usage: %s LABEL COUNT MODE NAME=HANDLER [NAME=HANDLER...]\n' "$0" >&2
    exit 2
fi
label=$1
count=$2
mode=$3
shift 3
[[ "$label" =~ ^[A-Za-z0-9_.-]+$ ]] || { printf 'ERROR: invalid label\n' >&2; exit 2; }
[[ "$count" =~ ^[1-9][0-9]*$ ]] || { printf 'ERROR: COUNT must be positive\n' >&2; exit 2; }
for spec in "$@"; do
    name=${spec%%=*}
    handler=${spec#*=}
    [[ "$spec" == *=* && "$name" =~ ^[A-Za-z0-9_.-]+$ && -f "$handler" ]] || {
        printf 'ERROR: expected NAME=HANDLER with an existing handler: %s\n' "$spec" >&2
        exit 2
    }
done

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)
display=()
if [[ "$(uname -s)" == Linux ]]; then display=(xvfb-run -a); fi

for i in $(seq 1 "$count"); do
    if (( i % 2 )); then order=bfs-first; else order=pfs3-first; fi
    for spec in "$@"; do
        name=${spec%%=*}
        handler=${spec#*=}
        run_dir="$project_dir/build/benchmark/$label-$name-$i-$order"
        [[ ! -e "$run_dir" ]] || { printf 'ERROR: %s exists\n' "$run_dir" >&2; exit 1; }
        BFS_BENCH_MODE=$mode BFS_BENCH_HANDLER_FILE=$handler BFS_BENCH_RUN_DIR=$run_dir \
            BFS_BENCH_ORDER=$order "$script_dir/build-bench-image.sh" >/dev/null
        if ! BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir \
            "${display[@]}" "$script_dir/run-bench.sh" 600 >"$run_dir/run.log" 2>&1; then
            printf 'FAIL %s\n' "$run_dir"
            tail -n 3 "$run_dir/run.log"
        fi
        rm -f "$run_dir"/bench-*.hdf
    done
done
printf 'Series %s complete.\n' "$label"
