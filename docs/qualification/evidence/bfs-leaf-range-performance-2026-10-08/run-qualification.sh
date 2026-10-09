#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Run from the isolated source checkout on the measurement host, after tests.
# Licensed fixtures and pinned binaries are local inputs, never repository data.
set -euo pipefail
: "${BFS_AMIGA_ASSETS_DIR:?set the Workbench command directory}"
: "${BFS_PFS3_HANDLER:?set the PFS3 handler path}"
: "${BFS_ROM_FILE:?set the Kickstart path}"
[[ -f emulator-test/bench-series.sh && -d build/inputs ]] || exit 2

check_digest() {
    local digest=$1 path=$2 observed
    observed=$(sha256sum "$path")
    [[ ${observed%% *} == "$digest" ]] || {
        printf 'ERROR: input identity mismatch: %s\n' "$path" >&2
        exit 1
    }
}
check_digest adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27 build/inputs/m3-handler
check_digest 79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4 build/inputs/range-identity-handler
check_digest a46a153a387de6cac773aee8bc6b757ce0bccf7a6dbc03b2c6ec8f8f8cc7981c build/inputs/range-identity-probe
check_digest 798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52 build/inputs/compare-guest
check_digest 7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661 build/inputs/deep-guest
check_digest f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55 build/inputs/bfs-formatter
sha256sum build/inputs/* "$BFS_PFS3_HANDLER" "$BFS_ROM_FILE" > input-binaries-final.txt

export BFS_BENCH_FORMATTER_FILE=build/inputs/bfs-formatter
export BFS_BENCH_GUEST_FILE=build/inputs/compare-guest
emulator-test/bench-series.sh leaf-range-normal-20261008 8 compare \
    m3=build/inputs/m3-handler@30 range=build/inputs/range-identity-handler@30 \
    > leaf-range-normal-series.log 2>&1
emulator-test/bench-series.sh leaf-range-durable-20261008 8 durable-compare \
    m3=build/inputs/m3-handler@30 range=build/inputs/range-identity-handler@30 \
    > leaf-range-durable-series.log 2>&1
export BFS_BENCH_GUEST_FILE=build/inputs/deep-guest
emulator-test/bench-series.sh leaf-range-deep-20261008 2 deep-compare \
    range=build/inputs/range-identity-probe@30 > leaf-range-deep-series.log 2>&1

# bench-series retains failed runs; never filter its inventory by PASS.
for mode in normal durable deep; do
    case "$mode" in
        normal) names=(m3 range); count=8; verify_mode=compare ;;
        durable) names=(m3 range); count=8; verify_mode=durable-compare ;;
        deep) names=(range); count=2; verify_mode=deep-compare ;;
    esac
    for name in "${names[@]}"; do
        for ((i=1; i<=count; i++)); do
            if ((i % 2)); then order=bfs-first; else order=pfs3-first; fi
            run_dir="build/benchmark/leaf-range-$mode-20261008-$name-$i-$order"
            (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
                system/C/fs-compare-bench > runtime-inputs.sha256)
            emulator-test/verify-bench-results.sh "$run_dir" "$verify_mode"
        done
    done
done
