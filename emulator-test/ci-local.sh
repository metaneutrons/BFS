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

ROM="${BFS_ROM_FILE:-$SCRIPT_DIR/.assets/A1200.47.102.rom}"
WB_ISO="${BFS_WB_ISO:-/Volumes/retro/Amiga/OS/3.2/AmigaOS3.2CD.iso}"

# ── Verify prerequisites ──────────────────────────────────────
[ -f "$ROM" ] || { echo "ERROR: ROM not found: $ROM"; exit 1; }
command -v fs-uae >/dev/null || { echo "ERROR: fs-uae not found"; exit 1; }
[ -f "$PROJECT_DIR/build/amiga/bfshandler" ] || { echo "ERROR: run 'make amiga' first"; exit 1; }
[ -f "$PROJECT_DIR/build/amiga/bfs-test" ] || { echo "ERROR: run 'make amiga-test' first"; exit 1; }

# ── Setup WB directory (once) ─────────────────────────────────
WB="$SCRIPT_DIR/.wb32"
if [ ! -d "$WB/C" ]; then
    echo "Setting up minimal WB3.2 environment..."
    ASSETS="$SCRIPT_DIR/.assets"
    if [ -d "$ASSETS/C" ]; then
        mkdir -p "$WB/C" "$WB/L" "$WB/Libs" "$WB/S" "$WB/Devs"
        cp "$ASSETS/C/"* "$WB/C/" 2>/dev/null || true
        cp "$ASSETS/L/"* "$WB/L/" 2>/dev/null || true
        cp "$ASSETS/Libs/"* "$WB/Libs/" 2>/dev/null || true
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
rdbtool -f "$HDF" create size=128Mi cyls=256 heads=16 secs=32 \
    + init \
    + add name=BFS start=2 end=255 dostype=0x42465300 bootable=False \
    + fsadd "$PROJECT_DIR/build/amiga/bfshandler" version=1.0 dostype=0x42465300 >/dev/null 2>&1

# Format partition area with BFS
PART_FILE=$(mktemp)
PART_BLOCKS=$(( (254 * 16 * 32 * 512) / 4096 ))
dd if=/dev/zero of="$PART_FILE" bs=4096 count="$PART_BLOCKS" 2>/dev/null
"$PROJECT_DIR/build/host/mkbfs" "$PART_FILE" >/dev/null
dd if="$PART_FILE" of="$HDF" bs=512 seek=1024 conv=notrunc 2>/dev/null
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
(sleep "$TIMEOUT" && kill $PID 2>/dev/null) &
TIMER_PID=$!
wait $PID 2>/dev/null || true
kill $TIMER_PID 2>/dev/null || true

# ── Evaluate results ──────────────────────────────────────────
RESULT="$WB/result.txt"
if [ ! -f "$RESULT" ]; then
    echo "ERROR: No result file (handler crash or timeout)"
    exit 1
fi

# Parse structured log: "# SUMMARY\tpass\trun\tfail"
SUMMARY=$(grep "^# SUMMARY" "$RESULT" 2>/dev/null || true)
if [ -n "$SUMMARY" ]; then
    PASS=$(echo "$SUMMARY" | cut -f2)
    RUN=$(echo "$SUMMARY" | cut -f3)
    FAIL=$(echo "$SUMMARY" | cut -f4)
else
    PASS=$(grep -c "^PASS" "$RESULT" 2>/dev/null || true)
    FAIL=$(grep -c "^FAIL" "$RESULT" 2>/dev/null || true)
fi
PASS=${PASS:-0}
FAIL=${FAIL:-0}

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
grep "^FAIL" "$RESULT" 2>/dev/null | while IFS='	' read -r _ name detail; do
    echo "  FAIL $name: $detail"
done

[ "$FAIL" -eq 0 ] && [ "$PASS" -gt 0 ] && echo "ALL TESTS PASSED" && exit 0
[ "$FAIL" -gt 0 ] && exit 1
echo "WARNING: Tests may have been interrupted (timeout)"
exit 2
