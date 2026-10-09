#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Fixed balanced crossover confirmation using existing, unchanged binaries.
set -euo pipefail
[[ $# == 1 ]] || { printf 'Usage: %s FROZEN_WORKSPACE\n' "$0" >&2; exit 2; }
workspace=$1
cd "$workspace/candidate"
if pgrep -x fs-uae >/dev/null; then
    printf 'ERROR: another emulator is running\n' >&2
    exit 1
fi
if systemctl is-active --quiet snapdog-runner.service; then
    printf 'ERROR: CI runner is active\n' >&2
    exit 1
fi
export BFS_AMIGA_ASSETS_DIR=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.assets
export BFS_PFS3_HANDLER=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.cache/pfs3aio
export BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom
export BFS_BENCH_FORMATTER_FILE=/home/fabian/.cache/bfs-performance/listing-upgrade-2026-10-08.29qKRc/build/inputs/bfs-formatter
export BFS_BENCH_GUEST_FILE="$workspace/build/inputs/m5/fs-compare-bench"
export BFS_BENCH_BUFFERS=30
result_root="$workspace/candidate/build/benchmark/sparse-confirmation-20261009"
[[ ! -e "$result_root" ]] || { printf 'ERROR: result directory exists\n' >&2; exit 2; }
mkdir -p "$result_root"
sha256sum "$workspace/build/inputs/m5/bfshandler" \
    "$workspace/build/inputs/sparse/bfshandler" "$BFS_BENCH_GUEST_FILE" \
    "$BFS_PFS3_HANDLER" "$BFS_ROM_FILE" "$BFS_BENCH_FORMATTER_FILE" \
    >"$result_root/input-identities.sha256"
# Check identities before executing any timing; an old executable cannot qualify.
printf '%s  %s\n' \
    79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4 "$workspace/build/inputs/m5/bfshandler" \
    486e3bbbf5c09a2b7d5cae0e3afd02a3a0a57d76a892d0aca6dfec31dbdb21f4 "$workspace/build/inputs/sparse/bfshandler" \
    eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d "$BFS_BENCH_GUEST_FILE" \
    bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 "$BFS_PFS3_HANDLER" \
    68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c "$BFS_ROM_FILE" \
    f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55 "$BFS_BENCH_FORMATTER_FILE" \
    | sha256sum -c - >"$result_root/input-check.log"
printf 'sequence\tfamily\trepeat\torder\tvariant\trun_name\n' >"$result_root/schedule.tsv"
sequence=0
run_one() {
    local family=$1 repeat=$2 order=$3 variant=$4 mode handler run_name run_dir
    case "$family" in normal|aa-normal) mode=compare ;; durable|aa-durable) mode=durable-compare ;; esac
    case "$variant" in m5|m5a|m5b) handler=m5 ;; sparse) handler=sparse ;; esac
    run_name="confirm-$family-$variant-$repeat-$order"
    run_dir="$result_root/$run_name"
    [[ ! -e "$run_dir" ]] || { printf 'ERROR: run exists\n' >&2; exit 2; }
    sequence=$((sequence + 1))
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$sequence" "$family" "$repeat" "$order" "$variant" "$run_name" >>"$result_root/schedule.tsv"
    printf 'START %s %s %s\n' "$sequence" "$run_name" "$(date -u +%FT%TZ)"
    mkdir -p "$run_dir"
    BFS_BENCH_MODE=$mode BFS_BENCH_HANDLER_FILE="$workspace/build/inputs/$handler/bfshandler" \
        BFS_BENCH_RUN_DIR=$run_dir BFS_BENCH_ORDER=$order \
        bash emulator-test/build-bench-image.sh >"$run_dir/build.log" 2>&1
    (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >runtime-inputs.sha256)
    BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir \
        xvfb-run -a bash emulator-test/run-bench.sh 600 >"$run_dir/run.log" 2>&1
    command rm -f "$run_dir/bench-bfs.hdf" "$run_dir/bench-pfs3.hdf"
    printf 'PASS %s %s %s\n' "$sequence" "$run_name" "$(date -u +%FT%TZ)"
}
for repeat in {1..8}; do
    if ((repeat % 2)); then order=bfs-first; families=(normal durable); else order=pfs3-first; families=(durable normal); fi
    # Both filesystem orders receive both handler orders, twice each per mode.
    if (((repeat - 1) % 4 < 2)); then variants=(m5 sparse); else variants=(sparse m5); fi
    for family in "${families[@]}"; do
        for variant in "${variants[@]}"; do run_one "$family" "$repeat" "$order" "$variant"; done
    done
    # Two identical-handler pairs per mode, one per filesystem order.
    if ((repeat == 4 || repeat == 8)); then
        aa_repeat=$((repeat / 4))
        if ((aa_repeat == 1)); then aa_order=bfs-first; else aa_order=pfs3-first; fi
        for family in aa-normal aa-durable; do
            run_one "$family" "$aa_repeat" "$aa_order" m5a
            run_one "$family" "$aa_repeat" "$aa_order" m5b
        done
    fi
done
[[ $sequence == 40 ]] || { printf 'ERROR: incomplete inventory\n' >&2; exit 1; }
printf 'CONFIRMATION_COMPLETE 40 %s\n' "$(date -u +%FT%TZ)"
