#!/bin/bash
# BFS Local CI Test — runs bfs-test on emulated 68020 with WB3.2 Kickstart
# Usage: ./emulator-test/ci-local.sh [timeout_seconds] [filter]
# Example: ./emulator-test/ci-local.sh 300
#          ./emulator-test/ci-local.sh 60 basic
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TIMEOUT="${1:-300}"
FILTER="${2:-}"

[[ "$TIMEOUT" =~ ^[1-9][0-9]*$ ]] || {
    echo "ERROR: timeout must be a positive integer" >&2
    exit 2
}
[[ -z "$FILTER" || "$FILTER" =~ ^[A-Za-z0-9_-]+$ ]] || {
    echo "ERROR: filter contains unsupported characters" >&2
    exit 2
}

ASSETS="${BFS_AMIGA_ASSETS_DIR:-$SCRIPT_DIR/.assets}"
ROM="${BFS_ROM_FILE:-$ASSETS/A1200.47.102.rom}"

# ── Verify prerequisites ──────────────────────────────────────
[ -f "$ROM" ] || { echo "ERROR: ROM not found: $ROM"; exit 1; }
command -v fs-uae >/dev/null || { echo "ERROR: fs-uae not found"; exit 1; }
[ -f "$PROJECT_DIR/build/amiga/bfshandler" ] || { echo "ERROR: run 'make amiga' first"; exit 1; }
[ -f "$PROJECT_DIR/build/amiga/bfs-test" ] || { echo "ERROR: run 'make amiga-test' first"; exit 1; }

# ── Setup WB directory (once) ─────────────────────────────────
WB="$SCRIPT_DIR/.wb32"
if [ ! -d "$WB/C" ]; then
    echo "Setting up minimal WB3.2 environment..."
    if [ -d "$ASSETS/C" ]; then
        mkdir -p "$WB/C" "$WB/L" "$WB/Libs" "$WB/S" "$WB/Devs"
        cp -R "$ASSETS/C/." "$WB/C/"
        if [ -d "$ASSETS/L" ]; then cp -R "$ASSETS/L/." "$WB/L/"; fi
        if [ -d "$ASSETS/Libs" ]; then cp -R "$ASSETS/Libs/." "$WB/Libs/"; fi
    else
        echo "ERROR: .assets/C/ not found. Run setup first."
        exit 1
    fi
fi

# ── Deploy handler + test binary ──────────────────────────────
cp "$PROJECT_DIR/build/amiga/bfshandler" "$WB/L/"
cp "$PROJECT_DIR/build/amiga/bfs-test" "$WB/C/"

# ── Create test HDF with pre-formatted BFS partition ──────────
HDF="$SCRIPT_DIR/ci-test.hdf"
rm -f "$HDF"
PART_FILE=$(mktemp)
PID=
TIMER_PID=
# shellcheck disable=SC2329
cleanup() {
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        if kill "$PID" 2>/dev/null; then :; fi
    fi
    if [ -n "$TIMER_PID" ] && kill "$TIMER_PID" 2>/dev/null; then
        if wait "$TIMER_PID" 2>/dev/null; then :; fi
    fi
    rm -f "$PART_FILE"
}
trap cleanup EXIT
rdbtool -f "$HDF" create size=128Mi cyls=256 heads=16 secs=32 \
    + init \
    + add name=BFS start=2 end=255 dostype=0x42465300 bootable=False \
    + fsadd "$PROJECT_DIR/build/amiga/bfshandler" version=1.0 dostype=0x42465300 >/dev/null 2>&1

# Format partition area with BFS
PART_BLOCKS=$(( (254 * 16 * 32 * 512) / 4096 ))
dd if=/dev/zero of="$PART_FILE" bs=4096 count="$PART_BLOCKS" status=none
"$PROJECT_DIR/build/host/mkbfs" "$PART_FILE" >/dev/null
dd if="$PART_FILE" of="$HDF" bs=512 seek=1024 conv=notrunc status=none
rm -f "$PART_FILE"

# ── Write Startup-Sequence ────────────────────────────────────
FILTER_ARG=""
[ -n "$FILTER" ] && FILTER_ARG=" $FILTER"
cat > "$WB/S/Startup-Sequence" << EOF
C:bfs-test BFS: LOG=SYS:result.txt${FILTER_ARG}
EOF

rm -f "$WB/result.txt"

# ── Generate FS-UAE config ────────────────────────────────────
CFG="$SCRIPT_DIR/config/ci-local.fs-uae"
mkdir -p "$(dirname "$CFG")"
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
hard_drive_1 = $HDF
floppy_speed = 0
audio_driver = null
end_config = shutdown
window_width = 800
window_height = 600
EOF

# ── Run FS-UAE ────────────────────────────────────────────────
echo "=== BFS m68k Integration Test ==="
echo "Timeout: ${TIMEOUT}s  Filter: ${FILTER:-all}"
echo "Starting FS-UAE..."

FSEMU_AUDIO_DRIVER=null fs-uae "$CFG" &
PID=$!
(
    sleep "$TIMEOUT"
    if kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null
    fi
) &
TIMER_PID=$!
if wait "$PID" 2>/dev/null; then :; fi
PID=
if kill "$TIMER_PID" 2>/dev/null; then
    if wait "$TIMER_PID" 2>/dev/null; then :; fi
fi
TIMER_PID=

# ── Evaluate results ──────────────────────────────────────────
RESULT="$WB/result.txt"
if [ ! -f "$RESULT" ]; then
    echo "ERROR: No result file (handler crash or timeout)"
    exit 1
fi

# Parse structured log: "# SUMMARY\tpass\trun\tfail"
SUMMARY=
if summary_lines=$(grep "^# SUMMARY" "$RESULT" 2>/dev/null); then
    SUMMARY=$(printf '%s\n' "$summary_lines" | tail -n 1)
fi
if [ -n "$SUMMARY" ]; then
    IFS=$'\t' read -r marker PASS RUN FAIL <<< "$SUMMARY"
    [[ "$marker" == "# SUMMARY" && "$PASS" =~ ^[0-9]+$ &&
       "$RUN" =~ ^[0-9]+$ && "$FAIL" =~ ^[0-9]+$ ]] || {
        echo "ERROR: malformed test summary" >&2
        exit 1
    }
else
    PASS=$(awk -F '\t' '$1 == "PASS" { count++ } END { print count + 0 }' "$RESULT")
    FAIL=$(awk -F '\t' '$1 == "FAIL" { count++ } END { print count + 0 }' "$RESULT")
    RUN=$((PASS + FAIL))
fi
PASS=${PASS:-0}
FAIL=${FAIL:-0}
RUN=${RUN:-0}

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
awk -F '\t' '$1 == "FAIL" { printf "  FAIL %s: %s\n", $2, $3 }' "$RESULT"

[ "$FAIL" -eq 0 ] && [ "$RUN" -gt 0 ] && [ "$PASS" -eq "$RUN" ] && \
    echo "ALL TESTS PASSED" && exit 0
[ "$FAIL" -gt 0 ] && exit 1
echo "WARNING: Tests may have been interrupted (timeout)"
exit 2
