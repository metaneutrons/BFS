#!/usr/bin/env bash
# Run only the isolated CRC probe, sequentially, with a bounded timeout.
set -euo pipefail
root=$(cd "$(dirname "$0")" && pwd)
assets=/home/fabian/.cache/bfs-performance/scan-admission-2026-10-09.VxOoB5/candidate/build/benchmark/admission-deep-20261009-m5-1-bfs-first/system
if pgrep -x fs-uae >/dev/null; then
    printf 'ERROR: another emulator is running\n' >&2
    exit 1
fi
for sample in 1 2; do
    run="$root/run-$sample"
    [[ ! -e "$run" ]] || { printf 'ERROR: run already exists: %s\n' "$run" >&2; exit 1; }
    mkdir -p "$run/system/S" "$run/system/Results"
    for directory in C L Libs; do cp -a "$assets/$directory" "$run/system/"; done
    cp "$root/crc32-sparse-probe" "$run/system/C/"
    cp "$root/Startup-Sequence" "$run/system/S/"
    cp "$root/micro-$sample.fs-uae" "$run/crc32.fs-uae"
    sha256sum "$run/system/C/crc32-sparse-probe" "$run/crc32.fs-uae" \
        /home/fabian/Amiga/kick.a1200.47.102.rom >"$run/inputs.sha256"
    xvfb-run -a bash -c '
        set -euo pipefail
        run=$1
        fs-uae "$run/crc32.fs-uae" >"$run/emulator.log" 2>&1 &
        emulator=$!
        trap '\''kill "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
        for ((second=0; second<180; second++)); do
            if [[ -f "$run/system/Results/crc32-sparse-probe.done" ]]; then
                printf "MICRO_COMPLETE %s\n" "$run"
                exit 0
            fi
            kill -0 "$emulator" 2>/dev/null || { printf "ERROR: emulator exited\n" >&2; exit 1; }
            sleep 1
        done
        printf "ERROR: microprobe timeout\n" >&2
        exit 1
    ' bash "$run"
done
