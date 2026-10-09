#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
bundle=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$bundle/../../../.." && pwd)
cd "$project"
shasum -a 256 build/read-flush-2026-10-09/{m5-default,sparse-default}/* \
    build/read-flush-2026-10-09/{m5-diagnostic,sparse-diagnostic}/bfshandler-probe \
    build/read-flush-2026-10-09/{guest,guest-static}/fs-compare-bench \
    build/host/bfs emulator-test/.assets/A1200.47.102.rom emulator-test/.cache/pfs3aio \
    >"$bundle/build-identities.sha256"
shasum -a 256 tools/fs-compare-bench.c tools/fs-compare-split.h tools/verify-split-bench.py \
    src/amiga/perf_probe.h tests/test_perf_probe.c tests/quality/test_split_bench_verifier.py \
    emulator-test/{build-bench-image,run-bench,verify-bench-results}.sh \
    >"$bundle/source-identities.sha256"
for entry in 'normal-m5 local-schema-static' 'durable-sparse local-durable-sparse' 'failed-stack-guest local-schema-smoke'; do
    read -r name original <<<"$entry"
    run_dir="$project/build/read-flush-2026-10-09/$original"
    mkdir -p "$bundle/$name/system/S"
    command cp "$run_dir/system/S/Startup-Sequence" "$bundle/$name/system/S/Startup-Sequence"
    (cd "$run_dir" && shasum -a 256 system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >"$bundle/$name/runtime-inputs.sha256")
    (cd "$run_dir/system" && find C L Libs -type f ! -path L/bfshandler -print0 | \
        LC_ALL=C sort -z | xargs -0 shasum -a 256 >"$bundle/$name/installed-assets.sha256")
done
python3 "$bundle/summarize-local.py" >"$bundle/summary.json"
