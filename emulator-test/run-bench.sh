#!/bin/bash
# Run the BFS vs PFS3 benchmark in FS-UAE
# Usage: ./emulator-test/run-bench.sh [timeout_seconds]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCH_DIR="${BFS_BENCH_RUN_DIR:-$PROJECT_DIR/build/benchmark}"
BENCH_MODE="${BFS_BENCH_MODE:-compare}"
TIMEOUT="${1:-600}"
case "$BENCH_MODE" in
    compare)
        RESULT_SUFFIX=tsv
        ;;
    profile)
        RESULT_SUFFIX=profile.tsv
        ;;
    *) echo "ERROR: BFS_BENCH_MODE must be compare or profile" >&2; exit 2 ;;
esac

[[ "$TIMEOUT" =~ ^[1-9][0-9]*$ ]] || {
    echo "ERROR: timeout must be a positive integer" >&2
    exit 2
}

WB="$BENCH_DIR/system"
BFS_HDF="$BENCH_DIR/bench-bfs.hdf"
PFS_HDF="$BENCH_DIR/bench-pfs3.hdf"
ROM="${BFS_ROM_FILE:-$SCRIPT_DIR/.assets/A1200.47.102.rom}"
RESULTS="$WB/Results"
COMPLETION="$RESULTS/complete.txt"
CFG="$BENCH_DIR/bench.fs-uae"
EMULATOR_LOG="$BENCH_DIR/fs-uae.log"

[ -d "$WB" ] || { echo "ERROR: Run build-bench-image.sh first"; exit 1; }
[ -f "$BFS_HDF" ] || { echo "ERROR: bench-bfs.hdf not found"; exit 1; }
[ -f "$PFS_HDF" ] || { echo "ERROR: bench-pfs3.hdf not found"; exit 1; }
[ -f "$ROM" ] || { echo "ERROR: ROM not found"; exit 1; }
command -v fs-uae >/dev/null || { echo "ERROR: fs-uae not found"; exit 1; }
[ ! -e "$COMPLETION" ] && [ ! -e "$CFG" ] && [ ! -e "$EMULATOR_LOG" ] || {
    echo "ERROR: refusing to overwrite existing benchmark evidence in $BENCH_DIR" >&2
    exit 2
}

PID=
TIMER_PID=
# Called indirectly by the EXIT trap below (SC2317 on older ShellCheck).
# shellcheck disable=SC2329,SC2317
cleanup() {
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null
    fi
    if [ -n "$TIMER_PID" ] && kill "$TIMER_PID" 2>/dev/null; then
        if wait "$TIMER_PID" 2>/dev/null; then :; fi
    fi
}
trap cleanup EXIT
cat > "$CFG" << EOF
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
kickstart_file = $ROM
hard_drive_0 = $WB
hard_drive_0_label = System
hard_drive_0_priority = 0
hard_drive_1 = $BFS_HDF
hard_drive_2 = $PFS_HDF
floppy_speed = 0
window_width = 800
window_height = 600
window_hidden = 1
automatic_input_grab = 0
audio_driver = null
EOF

echo "=== BFS vs PFS3 Benchmark (FS-UAE 68040) ==="
echo "Evidence directory: $BENCH_DIR"
echo "Mode: $BENCH_MODE"
echo "Timeout: ${TIMEOUT}s"
echo ""

FSEMU_AUDIO_DRIVER=null fs-uae "$CFG" >"$EMULATOR_LOG" 2>&1 &
PID=$!
(
    sleep "$TIMEOUT"
    if kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null
    fi
) &
TIMER_PID=$!

# Wait for a marker written only after both checked workload runs completed.
for _ in $(seq 1 "$TIMEOUT"); do
    sleep 1
    if [ -f "$COMPLETION" ]; then
        sleep 1
        if kill "$PID" 2>/dev/null; then :; fi
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then break; fi
done
if kill "$TIMER_PID" 2>/dev/null; then
    if wait "$TIMER_PID" 2>/dev/null; then :; fi
fi
TIMER_PID=
if wait "$PID" 2>/dev/null; then :; fi
PID=
trap - EXIT

# ── Show results ──────────────────────────────────────────────
echo ""
if [ -f "$RESULTS/info.txt" ] && [ -s "$RESULTS/info.txt" ]; then
    echo "=== Machine Info ==="
    cat "$RESULTS/info.txt"
fi
echo ""
if [ -f "$RESULTS/bfs.$RESULT_SUFFIX" ] && [ -s "$RESULTS/bfs.$RESULT_SUFFIX" ]; then
    echo "=== BFS Results ==="
    cat "$RESULTS/bfs.$RESULT_SUFFIX"
else
    echo "ERROR: BFS benchmark did not complete" >&2
fi
echo ""
if [ -f "$RESULTS/pfs3.$RESULT_SUFFIX" ] && [ -s "$RESULTS/pfs3.$RESULT_SUFFIX" ]; then
    echo "=== PFS3 Results ==="
    cat "$RESULTS/pfs3.$RESULT_SUFFIX"
else
    echo "ERROR: PFS3 benchmark did not complete" >&2
fi

"$SCRIPT_DIR/verify-bench-results.sh" "$BENCH_DIR" "$BENCH_MODE"
