#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Listing cache and inode batch pilot: two deep-compare starts of the candidate
# probe handler, BFS first and PFS3 first, with the formatter of the directory
# layout pilot. No retry or outlier filter. Scope and retention rule:
# PILOT_SCOPE.md.
set -euo pipefail
workspace=${1:?Usage: run-profile.sh WORKSPACE}
cd "$workspace/source"
result_root="$workspace/results"
[[ ! -e "$result_root" ]]
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
export BFS_BENCH_FORMATTER_FILE="$workspace/inputs/bfs-formatter"
export BFS_BENCH_BUFFERS=30
printf '%s  %s\n' \
    d370e972b62fea7bd734dbccac770d8cc1c07c5a7605ff0c7322c7bf9d6b6515 "$workspace/inputs/bfshandler-probe" \
    eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d "$workspace/inputs/fs-compare-production" \
    bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 "$BFS_PFS3_HANDLER" \
    68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c "$BFS_ROM_FILE" \
    a34639e012c72246fa98158165ed4c7a81a0b0b7aaa229fa31eed65737f434a1 "$BFS_BENCH_FORMATTER_FILE" \
    >"$result_root/input-identities.sha256"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.log"
sha256sum -c "$workspace/source-identities.sha256" >"$result_root/source-check.before.log"
printf 'sequence\trepeat\torder\trun_name\n' >"$result_root/schedule.tsv"
sequence=0
run_one() {
    local repeat=$1 order=$2 mode run_name run_dir
    run_name="cachebatch-pilot-$repeat-$order"
    run_dir="$result_root/$run_name"
    mode=deep-compare
    export BFS_BENCH_HANDLER_FILE="$workspace/inputs/bfshandler-probe"
    local guest="$workspace/inputs/fs-compare-production"
    export BFS_BENCH_GUEST_FILE=$guest
    quiet_guard >>"$result_root/quiet-guards.log"
    sequence=$((sequence + 1))
    printf '%s\t%s\t%s\t%s\n' "$sequence" "$repeat" "$order" "$run_name" >>"$result_root/schedule.tsv"
    printf 'START %s %s %s\n' "$sequence" "$run_name" "$(date -u +%FT%TZ)"
    [[ ! -e "$run_dir" ]]
    mkdir -p "$run_dir"
    BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir BFS_BENCH_ORDER=$order \
        bash emulator-test/build-bench-image.sh >"$run_dir/build.log" 2>&1
    (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >runtime-inputs.sha256)
    [[ $(sha256sum "$run_dir/system/L/bfshandler" | cut -d' ' -f1) == \
       $(sha256sum "$BFS_BENCH_HANDLER_FILE" | cut -d' ' -f1) ]]
    [[ $(sha256sum "$run_dir/system/C/fs-compare-bench" | cut -d' ' -f1) == \
       $(sha256sum "$BFS_BENCH_GUEST_FILE" | cut -d' ' -f1) ]]
    [[ $(sha256sum "$run_dir/system/L/pfs3aio" | cut -d' ' -f1) == \
       $(sha256sum "$BFS_PFS3_HANDLER" | cut -d' ' -f1) ]]
    python3 - "$run_dir/bfs-rdb.json" <<'PY'
import json
import sys
with open(sys.argv[1], encoding='ascii') as stream:
    partitions = json.load(stream)['rdb']['partitions']
assert len(partitions) == 1
assert partitions[0]['dos_env']['num_buffer'] == 30
PY
    (cd "$run_dir/system" && find C L Libs -type f ! -path L/bfshandler -print0 | LC_ALL=C sort -z | xargs -0 sha256sum >"$run_dir/installed-assets.sha256")
    BFS_BENCH_MODE=$mode BFS_BENCH_RUN_DIR=$run_dir \
        xvfb-run -a bash emulator-test/run-bench.sh 600 >"$run_dir/run.log" 2>&1
    (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >runtime-inputs.post.sha256 &&
        cmp runtime-inputs.sha256 runtime-inputs.post.sha256)
    printf 'PASS %s %s %s\n' "$sequence" "$run_name" "$(date -u +%FT%TZ)"
}
run_one 1 bfs-first
run_one 2 pfs3-first
[[ $sequence == 2 ]]
quiet_guard >"$result_root/quiet-after.log"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.post.log"
sha256sum -c "$workspace/source-identities.sha256" >"$result_root/source-check.post.log"
printf 'CACHEBATCH_PILOT_COMPLETE 2 %s\n' "$(date -u +%FT%TZ)"
