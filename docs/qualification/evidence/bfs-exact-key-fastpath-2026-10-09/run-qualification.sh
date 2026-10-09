#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Run in the isolated measurement checkout after builds and correctness tests.
# Licensed fixtures and executable inputs are private, not repository data.
set -euo pipefail
: "${BFS_AMIGA_ASSETS_DIR:?set Workbench command directory}"
: "${BFS_PFS3_HANDLER:?set PFS3 handler path}"
: "${BFS_ROM_FILE:?set Kickstart path}"
stage=${1:-pilot}
case "$stage" in pilot|full) ;; *) exit 2 ;; esac

check_digest() {
    local expected=$1 path=$2 observed
    observed=$(sha256sum "$path")
    [[ ${observed%% *} == "$expected" ]] || {
        printf 'ERROR: input identity mismatch: %s\n' "$path" >&2
        exit 1
    }
}
check_digest adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27 build/inputs/m3-handler
check_digest 79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4 build/inputs/range-identity-handler
check_digest 27447f991379ccc0eda02254bf5073dc0a11426f0321781cc1eea922f3f04f40 build/inputs/bfshandler
check_digest 557c6248e11c1d1985c8335c2d65eb92aa3c32607c021c0d5433d82faeb6719c build/inputs/bfshandler-probe
check_digest 798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52 build/inputs/compare-guest
check_digest 7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661 build/inputs/deep-guest
check_digest f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55 build/inputs/bfs-formatter
check_digest bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 "$BFS_PFS3_HANDLER"
check_digest 68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c "$BFS_ROM_FILE"
sha256sum build/inputs/* "$BFS_PFS3_HANDLER" "$BFS_ROM_FILE" > "input-binaries-$stage.txt"
export BFS_BENCH_FORMATTER_FILE=build/inputs/bfs-formatter
export BFS_BENCH_GUEST_FILE=build/inputs/compare-guest

if [[ $stage == pilot ]]; then
    count=2
    prefix=exact-key-pilot
    names=(range fast)
    handlers=(range=build/inputs/range-identity-handler@30 fast=build/inputs/bfshandler@30)
else
    count=8
    prefix=exact-key
    names=(m3 range fast)
    handlers=(m3=build/inputs/m3-handler@30 range=build/inputs/range-identity-handler@30 fast=build/inputs/bfshandler@30)
fi

record_and_verify() {
    local family=$1 mode=$2 repeat name order run_dir
    for name in "${names[@]}"; do
        for ((repeat=1; repeat<=count; repeat++)); do
            if ((repeat % 2)); then order=bfs-first; else order=pfs3-first; fi
            run_dir="build/benchmark/$prefix-$family-20261009-$name-$repeat-$order"
            # Digests of actual installed copies, recorded after measurement.
            (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
                system/C/fs-compare-bench > runtime-inputs.sha256)
            emulator-test/verify-bench-results.sh "$run_dir" "$mode"
        done
    done
}

for family in normal durable; do
    mode=compare
    [[ $family != durable ]] || mode=durable-compare
    emulator-test/bench-series.sh "$prefix-$family-20261009" "$count" "$mode" \
        "${handlers[@]}" > "$prefix-$family-series.log" 2>&1
    record_and_verify "$family" "$mode"
done

if [[ $stage == full ]]; then
    export BFS_BENCH_GUEST_FILE=build/inputs/deep-guest
    count=2
    names=(fast)
    emulator-test/bench-series.sh exact-key-deep-20261009 2 deep-compare \
        fast=build/inputs/bfshandler-probe@30 > exact-key-deep-series.log 2>&1
    record_and_verify deep deep-compare
fi
