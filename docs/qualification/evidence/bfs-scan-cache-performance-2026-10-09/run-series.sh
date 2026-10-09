#!/usr/bin/env bash
set -euo pipefail
cd /home/fabian/.cache/bfs-performance/scan-policy-2026-10-09.YjMjvA/candidate
export BFS_AMIGA_ASSETS_DIR=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.assets
export BFS_PFS3_HANDLER=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.cache/pfs3aio
export BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom
export BFS_BENCH_FORMATTER_FILE=/home/fabian/.cache/bfs-performance/listing-upgrade-2026-10-08.29qKRc/build/inputs/bfs-formatter
export BFS_BENCH_GUEST_FILE=$PWD/build/scan-baseline/fs-compare-bench
if pgrep -x fs-uae >/dev/null; then
    printf 'Another emulator is active; refusing timed runs.\n' >&2
    exit 1
fi
if systemctl is-active --quiet snapdog-runner.service; then
    printf 'The CI runner is active; refusing timed runs.\n' >&2
    exit 1
fi
bash emulator-test/bench-series.sh scan-pilot-normal-20261009 2 compare \
    "m5=$PWD/build/scan-baseline/bfshandler@30" \
    "cold=$PWD/build/scan-candidate/bfshandler@30"
bash emulator-test/bench-series.sh scan-deep-20261009 2 deep-compare \
    "m5=$PWD/build/scan-baseline/bfshandler-probe@30" \
    "cold=$PWD/build/scan-candidate/bfshandler-probe@30"
for run_dir in build/benchmark/scan-{pilot-normal,deep}-20261009-*; do
    (cd "$run_dir" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench) > "$run_dir/runtime-inputs.sha256"
done
