#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Bounded diagnostics only; inclusive probe times are not production timings.
set -euo pipefail
[[ $# == 1 ]] || { printf 'Usage: %s WORKSPACE\n' "$0" >&2; exit 2; }
workspace=$1
cd "$workspace/source"
result_root="$workspace/results"
[[ ! -e "$result_root" ]] || { printf 'ERROR: results already exist\n' >&2; exit 2; }
mkdir -p "$result_root"
quiet_guard() {
    local unit state
    for unit in actions.runner.metaneutrons-snapdog-os.cachy.service gitlab-runner.service; do
        state=$(systemctl show "$unit" -p LoadState -p ActiveState -p SubState -p MainPID)
        printf '%s\n%s\n' "$unit" "$state"
        [[ "$state" == *'LoadState=loaded'* && "$state" == *'ActiveState=inactive'* &&
           "$state" == *'SubState=dead'* && "$state" == *'MainPID=0'* ]] || return 1
    done
    [[ $(docker inspect -f '{{.State.Running}}' snapdog-runner) == false ]] || return 1
    if pgrep -x fs-uae >/dev/null || pgrep -x fs-uae-tap >/dev/null ||
       pgrep -f '^/opt/runner/bin/Runner\.(Listener|Worker)' >/dev/null; then return 1; fi
}
quiet_guard >"$result_root/quiet-before.log"
export BFS_AMIGA_ASSETS_DIR=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.assets
export BFS_PFS3_HANDLER=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.cache/pfs3aio
export BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom
export BFS_BENCH_FORMATTER_FILE=/home/fabian/.cache/bfs-performance/listing-upgrade-2026-10-08.29qKRc/build/inputs/bfs-formatter
export BFS_BENCH_GUEST_FILE="$workspace/inputs/guest/fs-compare-bench"
export BFS_BENCH_BUFFERS=30
printf '%s  %s\n' \
    4818ea526841a3a253cb437f925f949fcd14b9a5d780169b93a31bd8958c9277 "$workspace/inputs/m5-diagnostic/bfshandler-probe" \
    33a77819f0f7a5e6a3b4dbe9fd4d2ae2367f51f03ce910bf5be0957ccf4f7141 "$workspace/inputs/sparse-diagnostic/bfshandler-probe" \
    81c98af386792325a6e85f5debc6450895a48c76910816edae8716e1053d6bb4 "$BFS_BENCH_GUEST_FILE" \
    bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 "$BFS_PFS3_HANDLER" \
    68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c "$BFS_ROM_FILE" \
    f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55 "$BFS_BENCH_FORMATTER_FILE" \
    >"$result_root/input-identities.sha256"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.log"
printf 'sequence\tfamily\trepeat\torder\tvariant\trun_name\n' >"$result_root/schedule.tsv"
sequence=0
run_one() {
    local family=$1 repeat=$2 order=$3 variant=$4 mode handler run_name run_dir
    case "$family" in normal|aa-normal) mode=split-compare ;; durable|aa-durable) mode=split-durable-compare ;; esac
    case "$variant" in m5|m5a|m5b) handler=m5 ;; sparse) handler=sparse ;; esac
    quiet_guard >>"$result_root/quiet-guards.log"
    run_name="split-$family-$variant-$repeat-$order"
    run_dir="$result_root/$run_name"
    sequence=$((sequence + 1))
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$sequence" "$family" "$repeat" "$order" "$variant" "$run_name" >>"$result_root/schedule.tsv"
    printf 'START %s %s %s\n' "$sequence" "$run_name" "$(date -u +%FT%TZ)"
    mkdir -p "$run_dir"
    BFS_BENCH_MODE=$mode BFS_BENCH_HANDLER_FILE="$workspace/inputs/$handler-diagnostic/bfshandler-probe" \
        BFS_BENCH_RUN_DIR=$run_dir BFS_BENCH_ORDER=$order \
        bash emulator-test/build-bench-image.sh >"$run_dir/build.log" 2>&1
    (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >runtime-inputs.sha256)
    [[ $(sha256sum "$run_dir/system/L/bfshandler" | cut -d' ' -f1) == \
       $(sha256sum "$workspace/inputs/$handler-diagnostic/bfshandler-probe" | cut -d' ' -f1) ]]
    [[ $(sha256sum "$run_dir/system/C/fs-compare-bench" | cut -d' ' -f1) == \
       $(sha256sum "$BFS_BENCH_GUEST_FILE" | cut -d' ' -f1) ]]
    [[ $(sha256sum "$run_dir/system/L/pfs3aio" | cut -d' ' -f1) == \
       $(sha256sum "$BFS_PFS3_HANDLER" | cut -d' ' -f1) ]]
    python3 - "$run_dir/bfs-rdb.json" <<'PY'
import json
import sys
with open(sys.argv[1], encoding='ascii') as stream:
    rdb = json.load(stream)
partitions = rdb['rdb']['partitions']
assert len(partitions) == 1
assert partitions[0]['dos_env']['num_buffer'] == 30
PY
    (cd "$run_dir/system" && find C L Libs -type f ! -path L/bfshandler -print0 | sort -z | xargs -0 sha256sum >"$run_dir/installed-assets.sha256")
    BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir \
        xvfb-run -a bash emulator-test/run-bench.sh 600 >"$run_dir/run.log" 2>&1
    (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >runtime-inputs.post.sha256 &&
        cmp runtime-inputs.sha256 runtime-inputs.post.sha256)
    printf 'PASS %s %s %s\n' "$sequence" "$run_name" "$(date -u +%FT%TZ)"
}
# Two primary pairs per mode, opposite filesystem and first-handler orders.
for repeat in 1 2; do
    if [[ $repeat == 1 ]]; then order=bfs-first; variants=(m5 sparse); families=(normal durable);
    else order=pfs3-first; variants=(sparse m5); families=(durable normal); fi
    for family in "${families[@]}"; do
        for variant in "${variants[@]}"; do run_one "$family" "$repeat" "$order" "$variant"; done
    done
done
# Same-handler controls, not enough to prove statistical equivalence.
for family in aa-normal aa-durable; do
    if [[ $family == aa-normal ]]; then order=bfs-first; else order=pfs3-first; fi
    run_one "$family" 1 "$order" m5a
    run_one "$family" 1 "$order" m5b
done
[[ $sequence == 12 ]]
quiet_guard >"$result_root/quiet-after.log"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.post.log"
printf 'SPLIT_COMPLETE 12 %s\n' "$(date -u +%FT%TZ)"
