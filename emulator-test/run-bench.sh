#!/bin/bash
# Run the BFS vs PFS3 benchmark in FS-UAE
# Usage: ./emulator-test/run-bench.sh [timeout_seconds]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMEOUT="${1:-600}"

WB="$SCRIPT_DIR/.bench-wb"
BFS_HDF="$SCRIPT_DIR/bench-bfs.hdf"
PFS_HDF="$SCRIPT_DIR/bench-pfs3.hdf"
ROM="$SCRIPT_DIR/.assets/A1200.47.102.rom"

[ -d "$WB" ] || { echo "ERROR: Run build-bench-image.sh first"; exit 1; }
[ -f "$BFS_HDF" ] || { echo "ERROR: bench-bfs.hdf not found"; exit 1; }
[ -f "$PFS_HDF" ] || { echo "ERROR: bench-pfs3.hdf not found"; exit 1; }
[ -f "$ROM" ] || { echo "ERROR: ROM not found"; exit 1; }
command -v fs-uae >/dev/null || { echo "ERROR: fs-uae not found"; exit 1; }

# Clean previous results
rm -f "$WB/Results/bfs.txt" "$WB/Results/pfs3.txt" "$WB/Results/info.txt"
rm -f "$WB/Results/"*.uaem

CFG=$(mktemp)
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
EOF

echo "=== BFS vs PFS3 Benchmark (FS-UAE 68040) ==="
echo "Timeout: ${TIMEOUT}s"
echo ""

FSEMU_AUDIO_DRIVER=null fs-uae "$CFG" &
PID=$!
(sleep "$TIMEOUT" && kill $PID 2>/dev/null) &
TIMER_PID=$!

# Wait for completion (check for pfs3.txt = last result written)
for i in $(seq 1 "$TIMEOUT"); do
    sleep 1
    if [ -f "$WB/Results/pfs3.txt" ] && [ -s "$WB/Results/pfs3.txt" ]; then
        sleep 2  # let it finish writing
        kill $PID 2>/dev/null
        break
    fi
    if ! kill -0 $PID 2>/dev/null; then break; fi
done
kill $TIMER_PID 2>/dev/null || true
wait $PID 2>/dev/null || true
rm -f "$CFG"

# ── Show results ──────────────────────────────────────────────
echo ""
if [ -f "$WB/Results/info.txt" ] && [ -s "$WB/Results/info.txt" ]; then
    echo "=== Machine Info ==="
    cat "$WB/Results/info.txt"
fi
echo ""
if [ -f "$WB/Results/bfs.txt" ] && [ -s "$WB/Results/bfs.txt" ]; then
    echo "=== BFS Results ==="
    cat "$WB/Results/bfs.txt"
else
    echo "ERROR: BFS benchmark did not complete"
fi
echo ""
if [ -f "$WB/Results/pfs3.txt" ] && [ -s "$WB/Results/pfs3.txt" ]; then
    echo "=== PFS3 Results ==="
    cat "$WB/Results/pfs3.txt"
else
    echo "ERROR: PFS3 benchmark did not complete"
fi
