#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Append 4 KiB attribution, run 2: 24 compare starts of three production handlers
# (ref, mid, cand) and 6 split-write-compare starts of their write probes,
# alternating order and rotating handler order. No retry or outlier filter.
# Scope: PILOT_SCOPE.md.
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
    2c3645baf3bab1e60569f70f7b79abff37b706283671d72f63c8a2a3ec2560ec "$workspace/inputs/ref/bfshandler" \
    53acd50f246a27114c26f51e042cd549d523f4411537cd19c2d2faef5a524cb2 "$workspace/inputs/mid/bfshandler" \
    95a26862e161deb254f3a628c9ca801ab4f062d28ade5c017a417b92a4b95a71 "$workspace/inputs/cand/bfshandler" \
    d69ff1fd03b2b480f75a17d4737747ad317e322397c3093f2c93625a2fd34b88 "$workspace/inputs/ref/bfshandler-write-probe" \
    39e5cf90dd0aaa81d56fcb93ed72be32583e86610dc16cf0d9c66098031161bd "$workspace/inputs/mid/bfshandler-write-probe" \
    b991b0a2ced51e036798322845d00f12d4b7d539f0fb0f0d9ddaaf4777e5b79a "$workspace/inputs/cand/bfshandler-write-probe" \
    eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d "$workspace/inputs/fs-compare-production" \
    20e751e8c558af9ca80ed4c597162fad272326fb8cf0265c3f4a7ad8c2395bf2 "$workspace/inputs/fs-compare-write" \
    bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 "$BFS_PFS3_HANDLER" \
    68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c "$BFS_ROM_FILE" \
    a34639e012c72246fa98158165ed4c7a81a0b0b7aaa229fa31eed65737f434a1 "$BFS_BENCH_FORMATTER_FILE" \
    >"$result_root/input-identities.sha256"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.log"
sha256sum -c "$workspace/source-identities.sha256" >"$result_root/source-check.before.log"
printf 'sequence\tpart\tround\torder\tvariant\trun_name\n' >"$result_root/schedule.tsv"
sequence=0
run_one() {
    local part=$1 round=$2 order=$3 variant=$4 mode run_name run_dir guest
    run_name="append2-$part-$round-$order-$variant"
    run_dir="$result_root/$run_name"
    if [[ $part == prod ]]; then
        mode=compare
        export BFS_BENCH_HANDLER_FILE="$workspace/inputs/$variant/bfshandler"
        guest="$workspace/inputs/fs-compare-production"
    else
        mode=split-write-compare
        export BFS_BENCH_HANDLER_FILE="$workspace/inputs/$variant/bfshandler-write-probe"
        guest="$workspace/inputs/fs-compare-write"
    fi
    export BFS_BENCH_GUEST_FILE=$guest
    quiet_guard >>"$result_root/quiet-guards.log"
    sequence=$((sequence + 1))
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$sequence" "$part" "$round" "$order" "$variant" "$run_name" >>"$result_root/schedule.tsv"
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
variants=(ref mid cand)
for round in 1 2 3 4 5 6 7 8; do
    order=bfs-first
    if (( round % 2 == 0 )); then order=pfs3-first; fi
    for slot in 0 1 2; do
        run_one prod "$round" "$order" "${variants[$(( (round - 1 + slot) % 3 ))]}"
    done
done
for round in 1 2; do
    order=bfs-first
    if (( round % 2 == 0 )); then order=pfs3-first; fi
    for slot in 0 1 2; do
        run_one probe "$round" "$order" "${variants[$(( (round - 1 + slot) % 3 ))]}"
    done
done
[[ $sequence == 30 ]]
quiet_guard >"$result_root/quiet-after.log"
sha256sum -c "$result_root/input-identities.sha256" >"$result_root/input-check.post.log"
sha256sum -c "$workspace/source-identities.sha256" >"$result_root/source-check.post.log"
printf 'APPEND_ATTRIBUTION2_COMPLETE 30 %s\n' "$(date -u +%FT%TZ)"
