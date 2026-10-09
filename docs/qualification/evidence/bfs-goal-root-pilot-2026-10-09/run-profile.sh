#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Fixed pilot: two attribution starts, eight paired production starts,
# four identical-baseline control starts. No retry or outlier filter.
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
export BFS_BENCH_FORMATTER_FILE=/home/fabian/.cache/bfs-performance/listing-upgrade-2026-10-08.29qKRc/build/inputs/bfs-formatter
export BFS_BENCH_BUFFERS=30
printf '%s  %s\n' \
    486e3bbbf5c09a2b7d5cae0e3afd02a3a0a57d76a892d0aca6dfec31dbdb21f4 "$workspace/inputs/base/bfshandler" \
    d0589bb99ee8d897449a1480dadfcda5a8d4956ccefd4575a794778b3b45cec7 "$workspace/inputs/candidate/bfshandler" \
    e2f99ecc68e411358f96e5f85a9d452518af0d8c127b4cec4a3d3319d191d39c "$workspace/inputs/base/bfshandler-probe" \
    656e17c292320b0a28deb7325d1ade8a4ff0feea806a815d2dc580e77dd8c1fe "$workspace/inputs/candidate/bfshandler-probe" \
    eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d "$workspace/inputs/fs-compare-production" \
    20e751e8c558af9ca80ed4c597162fad272326fb8cf0265c3f4a7ad8c2395bf2 "$workspace/inputs/fs-compare-write" \
    bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 "$BFS_PFS3_HANDLER" \
    68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c "$BFS_ROM_FILE" \
    f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55 "$BFS_BENCH_FORMATTER_FILE" \
    >"$result_root/input-identities.sha256"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.log"
sha256sum -c "$workspace/source-identities.sha256" >"$result_root/source-check.before.log"
printf 'sequence\tfamily\trepeat\torder\tvariant\trun_name\n' >"$result_root/schedule.tsv"
sequence=0
run_one() {
    local family=$1 repeat=$2 order=$3 variant=$4 mode handler guest run_name run_dir
    run_name="goal-root-$family-$variant-$repeat-$order"
    run_dir="$result_root/$run_name"
    handler=$variant
    case "$variant" in basea|baseb) handler=base ;; esac
    case "$family" in
        diag)
            mode=split-write-durable-compare
            guest="$workspace/inputs/fs-compare-write"
            export BFS_BENCH_HANDLER_FILE="$workspace/inputs/$handler/bfshandler-probe"
            ;;
        normal|aa-normal)
            mode=compare
            guest="$workspace/inputs/fs-compare-production"
            export BFS_BENCH_HANDLER_FILE="$workspace/inputs/$handler/bfshandler"
            ;;
        durable|aa-durable)
            mode=durable-compare
            guest="$workspace/inputs/fs-compare-production"
            export BFS_BENCH_HANDLER_FILE="$workspace/inputs/$handler/bfshandler"
            ;;
        *) return 2 ;;
    esac
    export BFS_BENCH_GUEST_FILE=$guest
    quiet_guard >>"$result_root/quiet-guards.log"
    sequence=$((sequence + 1))
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$sequence" "$family" "$repeat" "$order" "$variant" "$run_name" >>"$result_root/schedule.tsv"
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
run_one diag 1 bfs-first base
run_one diag 1 bfs-first candidate
run_one normal 1 bfs-first base
run_one normal 1 bfs-first candidate
run_one durable 1 bfs-first base
run_one durable 1 bfs-first candidate
run_one durable 2 pfs3-first candidate
run_one durable 2 pfs3-first base
run_one normal 2 pfs3-first candidate
run_one normal 2 pfs3-first base
run_one aa-normal 1 bfs-first basea
run_one aa-normal 1 bfs-first baseb
run_one aa-durable 1 pfs3-first basea
run_one aa-durable 1 pfs3-first baseb
[[ $sequence == 14 ]]
quiet_guard >"$result_root/quiet-after.log"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.post.log"
sha256sum -c "$workspace/source-identities.sha256" >"$result_root/source-check.post.log"
printf 'GOAL_ROOT_PILOT_COMPLETE 14 %s\n' "$(date -u +%FT%TZ)"
