#!/usr/bin/env bash
# Reproduce one explicitly selected series in the frozen Cachy export.
set -euo pipefail
[[ $# == 2 ]] || { printf 'Usage: %s WORKSPACE pilot|deep|normal|durable\n' "$0" >&2; exit 2; }
workspace=$1
family=$2
cd "$workspace/candidate"
if pgrep -x fs-uae >/dev/null; then
    printf 'ERROR: another emulator is running\n' >&2
    exit 1
fi
export BFS_AMIGA_ASSETS_DIR=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.assets
export BFS_PFS3_HANDLER=/home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.cache/pfs3aio
export BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom
export BFS_BENCH_FORMATTER_FILE=/home/fabian/.cache/bfs-performance/listing-upgrade-2026-10-08.29qKRc/build/inputs/bfs-formatter
export BFS_BENCH_GUEST_FILE="$workspace/build/inputs/m5/fs-compare-bench"
export BFS_BENCH_BUFFERS=30
case "$family" in
    pilot) label=sparse-pilot-normal-20261009; count=2; mode=compare; handler=bfshandler ;;
    deep) label=sparse-deep-20261009; count=2; mode=deep-compare; handler=bfshandler-probe ;;
    normal) label=sparse-normal-20261009; count=8; mode=compare; handler=bfshandler ;;
    durable) label=sparse-durable-20261009; count=8; mode=durable-compare; handler=bfshandler ;;
    *) printf 'ERROR: unsupported family\n' >&2; exit 2 ;;
esac
bash emulator-test/bench-series.sh "$label" "$count" "$mode" \
    "m5=$workspace/build/inputs/m5/$handler@30" \
    "sparse=$workspace/build/inputs/sparse/$handler@30"
for run in build/benchmark/"$label"-*; do
    (cd "$run" && sha256sum system/L/bfshandler system/L/pfs3aio \
        system/C/fs-compare-bench >runtime-inputs.sha256)
done
