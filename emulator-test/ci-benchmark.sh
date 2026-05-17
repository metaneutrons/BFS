#!/bin/bash
# BFS Benchmark — runs DiskSpeed on emulated 68040 with BFS volume
# Usage: ./emulator-test/ci-benchmark.sh [timeout_seconds]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TIMEOUT="${1:-300}"

ROM="$SCRIPT_DIR/.assets/A1200.47.102.rom"

# ── Verify prerequisites ──────────────────────────────────────
[ -f "$ROM" ] || { echo "ERROR: ROM not found: $ROM"; exit 1; }
command -v fs-uae >/dev/null || { echo "ERROR: fs-uae not found"; exit 1; }
[ -f "$PROJECT_DIR/build/amiga/bfshandler" ] || { echo "ERROR: run 'make amiga' first"; exit 1; }
[ -f "$SCRIPT_DIR/.cache/DiskSpeed" ] || { echo "ERROR: DiskSpeed not found in .cache/"; exit 1; }

# ── Setup WB directory ────────────────────────────────────────
WB="$SCRIPT_DIR/.wb32"
if [ ! -d "$WB/C" ]; then
    ASSETS="$SCRIPT_DIR/.assets"
    [ -d "$ASSETS/C" ] || { echo "ERROR: .assets/C/ not found"; exit 1; }
    mkdir -p "$WB/C" "$WB/L" "$WB/Libs" "$WB/S" "$WB/Devs"
    cp "$ASSETS/C/"* "$WB/C/" 2>/dev/null || true
    cp "$ASSETS/L/"* "$WB/L/" 2>/dev/null || true
    cp "$ASSETS/Libs/"* "$WB/Libs/" 2>/dev/null || true
fi

# Deploy handler + DiskSpeed
cp "$PROJECT_DIR/build/amiga/bfshandler" "$WB/L/"
cp "$SCRIPT_DIR/.cache/DiskSpeed" "$WB/C/"

# ── Create test HDF (128MB, pre-formatted BFS) ────────────────
HDF="$SCRIPT_DIR/bench.hdf"
rm -f "$HDF"
rdbtool -f "$HDF" create size=128Mi cyls=256 heads=16 secs=32 \
    + init \
    + add name=BFS start=2 end=255 dostype=0x42465300 bootable=False \
    + fsadd "$PROJECT_DIR/build/amiga/bfshandler" version=1.0 dostype=0x42465300 >/dev/null 2>&1

PART_FILE=$(mktemp)
PART_BLOCKS=$(( (254 * 16 * 32 * 512) / 4096 ))
dd if=/dev/zero of="$PART_FILE" bs=4096 count="$PART_BLOCKS" 2>/dev/null
"$PROJECT_DIR/build/host/mkbfs" "$PART_FILE" >/dev/null
dd if="$PART_FILE" of="$HDF" bs=512 seek=1024 conv=notrunc 2>/dev/null
rm -f "$PART_FILE"

# ── Write Startup-Sequence ────────────────────────────────────
cat > "$WB/S/Startup-Sequence" << 'EOF'
Echo ""
Echo "=== BFS DiskSpeed Benchmark ==="
Echo ""
C:DiskSpeed DRIVE=BFS: ALL >SYS:bench.txt
Type SYS:bench.txt
EOF

rm -f "$WB/bench.txt"

# ── Generate FS-UAE config ────────────────────────────────────
CFG="$SCRIPT_DIR/config/ci-bench.fs-uae"
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
echo "=== BFS DiskSpeed Benchmark ==="
echo "Timeout: ${TIMEOUT}s"
echo "Starting FS-UAE..."

FSEMU_AUDIO_DRIVER=null fs-uae "$CFG" &
PID=$!
(sleep "$TIMEOUT" && kill $PID 2>/dev/null) &
TIMER_PID=$!
wait $PID 2>/dev/null || true
kill $TIMER_PID 2>/dev/null || true

# ── Show results ──────────────────────────────────────────────
RESULT="$WB/bench.txt"
if [ ! -f "$RESULT" ]; then
    echo "ERROR: No benchmark result (timeout or crash)"
    exit 1
fi

echo ""
echo "=== Results ==="
cat "$RESULT"

# Clean up HDF
rm -f "$HDF"
