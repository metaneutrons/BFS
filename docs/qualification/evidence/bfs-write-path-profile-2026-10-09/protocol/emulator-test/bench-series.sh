#!/usr/bin/env bash
# Interleave fresh compare runs of several BFS handlers against PFS3.
# Usage: bench-series.sh LABEL COUNT MODE NAME=HANDLER[@BUFFERS] [...]
# An explicit @BUFFERS sets the BFS DosEnvec de_NumBuffers for that handler.
# If the literal handler path, including an @ suffix, exists, it is used as-is.
# BFS_BENCH_BUFFERS supplies the common value when a spec omits @BUFFERS; its
# default is 30.
# Run i of every handler starts with BFS when i is odd and with PFS3 when it
# is even, so order effects reach each handler equally. Results land in
# build/benchmark/LABEL-NAME-I-ORDER; summarize them with
# tools/bench-summary.py LABEL-NAME... . The HDFs are removed after each run.
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=emulator-test/bench-config.sh
source "$script_dir/bench-config.sh"

if [[ $# -lt 4 ]]; then
    printf 'Usage: %s LABEL COUNT MODE NAME=HANDLER[@BUFFERS] [...]\n' "$0" >&2
    exit 2
fi
label=$1
count=$2
mode=$3
shift 3
[[ "$label" =~ ^[A-Za-z0-9_.-]+$ ]] || { printf 'ERROR: invalid label\n' >&2; exit 2; }
[[ "$count" =~ ^[1-9][0-9]*$ ]] || { printf 'ERROR: COUNT must be positive\n' >&2; exit 2; }

common_buffers=$(bfs_bench_resolve_buffers) || {
    printf 'ERROR: BFS_BENCH_BUFFERS must be a decimal integer from 1 to 128\n' >&2
    exit 2
}

spec_names=()
spec_handlers=()
spec_buffers=()
for spec in "$@"; do
    name=${spec%%=*}
    handler_spec=${spec#*=}
    [[ "$spec" == *=* && "$name" =~ ^[A-Za-z0-9_.-]+$ ]] || {
        printf 'ERROR: expected NAME=HANDLER[@BUFFERS]: %s\n' "$spec" >&2
        exit 2
    }
    if ((${#spec_names[@]})); then
        for existing_name in "${spec_names[@]}"; do
            if [[ "$existing_name" == "$name" ]]; then
                printf 'ERROR: duplicate benchmark name: %s\n' "$name" >&2
                exit 2
            fi
        done
    fi

    handler=$handler_spec
    buffers=
    if [[ ! -f "$handler_spec" && "$handler_spec" == *@* ]]; then
        handler=${handler_spec%@*}
        requested_buffers=${handler_spec##*@}
        buffers=$(bfs_bench_validate_buffers "$requested_buffers") || {
            printf 'ERROR: invalid BFS buffer capacity in spec: %s\n' "$spec" >&2
            exit 2
        }
    fi
    [[ -f "$handler" ]] || {
        printf 'ERROR: expected NAME=HANDLER[@BUFFERS] with an existing handler: %s\n' "$spec" >&2
        exit 2
    }
    spec_names+=("$name")
    spec_handlers+=("$handler")
    spec_buffers+=("$buffers")
done

project_dir=$(cd "$script_dir/.." && pwd)
display=()
if [[ "$(uname -s)" == Linux ]]; then display=(xvfb-run -a); fi

run_benchmark() {
    if ((${#display[@]})); then
        BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir \
            "${display[@]}" "$script_dir/run-bench.sh" 600
    else
        BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir \
            "$script_dir/run-bench.sh" 600
    fi
}

for i in $(seq 1 "$count"); do
    if (( i % 2 )); then order=bfs-first; else order=pfs3-first; fi
    for index in "${!spec_names[@]}"; do
        name=${spec_names[$index]}
        handler=${spec_handlers[$index]}
        buffers=${spec_buffers[$index]:-$common_buffers}
        run_dir="$project_dir/build/benchmark/$label-$name-$i-$order"
        [[ ! -e "$run_dir" ]] || { printf 'ERROR: %s exists\n' "$run_dir" >&2; exit 1; }
        BFS_BENCH_MODE=$mode BFS_BENCH_BUFFERS=$buffers BFS_BENCH_HANDLER_FILE=$handler BFS_BENCH_RUN_DIR=$run_dir \
            BFS_BENCH_ORDER=$order "$script_dir/build-bench-image.sh" >/dev/null
        if ! run_benchmark >"$run_dir/run.log" 2>&1; then
            printf 'FAIL %s\n' "$run_dir"
            tail -n 3 "$run_dir/run.log"
        fi
        rm -f "$run_dir"/bench-*.hdf
    done
done
printf 'Series %s complete.\n' "$label"
