#!/bin/bash
# Build a benchmark HDF image for BFS vs PFS3 comparison.
#
# Layout:
#   DH0: Directory filesystem (boot) — WB3.2 + DiskSpeed + benchmark script
#   DH1: RDB HDF with BFS partition (256MB, pre-formatted)
#   DH2: RDB HDF with PFS3 partition (256MB, formatted on first boot)
#
# For real hardware (CF card), combine into single RDB image.
# For FS-UAE testing, use run-bench.sh which mounts them separately.
#
# Usage:
#   ./emulator-test/build-bench-image.sh
#
# Output:
#   emulator-test/bench-bfs.hdf   — BFS test partition
#   emulator-test/bench-pfs3.hdf  — PFS3 test partition
#   emulator-test/.bench-wb/      — Boot directory (WB + scripts)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ROM="$SCRIPT_DIR/.assets/A1200.47.102.rom"
PFS3="$SCRIPT_DIR/benchmark/wb/L/pfs3aio"
DISKSPEED="$SCRIPT_DIR/.cache/DiskSpeed"
ASSETS="$SCRIPT_DIR/.assets"

# ── Prerequisites ─────────────────────────────────────────────
[ -f "$ROM" ] || { echo "ERROR: ROM not found: $ROM"; exit 1; }
[ -f "$PFS3" ] || { echo "ERROR: pfs3aio not found: $PFS3"; exit 1; }
[ -f "$DISKSPEED" ] || { echo "ERROR: DiskSpeed not found: $DISKSPEED"; exit 1; }
[ -d "$ASSETS/C" ] || { echo "ERROR: .assets/C/ not found"; exit 1; }
command -v rdbtool >/dev/null || { echo "ERROR: rdbtool not found"; exit 1; }
[ -f "$PROJECT_DIR/build/amiga/bfshandler" ] || { echo "ERROR: run 'make amiga' first"; exit 1; }
[ -f "$PROJECT_DIR/build/host/mkbfs" ] || { echo "ERROR: run 'make build/host/mkbfs' first"; exit 1; }

echo "=== Building BFS vs PFS3 Benchmark ==="

# ── Setup boot directory ──────────────────────────────────────
WB="$SCRIPT_DIR/.bench-wb"
rm -rf "$WB"
mkdir -p "$WB/C" "$WB/L" "$WB/Libs" "$WB/S" "$WB/Devs" "$WB/Results"

cp "$ASSETS/C/"* "$WB/C/" 2>/dev/null || true
cp "$ASSETS/L/"* "$WB/L/" 2>/dev/null || true
cp "$ASSETS/Libs/"* "$WB/Libs/" 2>/dev/null || true
cp "$DISKSPEED" "$WB/C/DiskSpeed"
cp "$PROJECT_DIR/build/amiga/bfshandler" "$WB/L/"
cp "$PFS3" "$WB/L/pfs3aio"

# ── Startup-Sequence ──────────────────────────────────────────
cat > "$WB/S/Startup-Sequence" << 'AMIGA'
; BFS vs PFS3 DiskSpeed Benchmark
Wait 3

Echo ""
Echo "============================================"
Echo "  BFS vs PFS3 DiskSpeed Benchmark"
Echo "============================================"
Echo ""

; Machine info
Echo "# Machine Info" >SYS:Results/info.txt
Version >>SYS:Results/info.txt
CPU >>SYS:Results/info.txt
Avail >>SYS:Results/info.txt
Echo "" >>SYS:Results/info.txt
Info >>SYS:Results/info.txt

; Format PFS3 partition
Echo "Formatting DH2: (PFS3)..."
Format DRIVE DH2: NAME PFSTest NOICONS QUICK <NIL: >NIL:
Wait 2

Echo ""
Echo "--- BFS Benchmark (DH1:) ---"
Echo ""
C:DiskSpeed DRIVE=DH1: ALL >SYS:Results/bfs.txt
Type SYS:Results/bfs.txt

Echo ""
Echo "--- PFS3 Benchmark (DH2:) ---"
Echo ""
C:DiskSpeed DRIVE=DH2: ALL >SYS:Results/pfs3.txt
Type SYS:Results/pfs3.txt

Echo ""
Echo "============================================"
Echo "  Benchmark complete!"
Echo "  Results saved to SYS:Results/"
Echo "============================================"
AMIGA

echo "  Boot directory: $WB"

# ── Create BFS HDF (256MB) ────────────────────────────────────
echo "Creating BFS partition (256MB)..."
BFS_HDF="$SCRIPT_DIR/bench-bfs.hdf"
rm -f "$BFS_HDF"
rdbtool -f "$BFS_HDF" create size=256Mi cyls=512 heads=16 secs=32 \
    + init \
    + add name=DH1 start=2 end=511 dostype=0x42465300 bootable=False \
    + fsadd "$PROJECT_DIR/build/amiga/bfshandler" version=1.0 dostype=0x42465300 >/dev/null 2>&1

# Pre-format BFS
BFS_OFFSET=$(( 2 * 16 * 32 * 512 ))
BFS_BLOCKS=$(( (510 * 16 * 32 * 512) / 4096 ))
PART_FILE=$(mktemp)
dd if=/dev/zero of="$PART_FILE" bs=4096 count="$BFS_BLOCKS" 2>/dev/null
"$PROJECT_DIR/build/host/mkbfs" "$PART_FILE" >/dev/null
dd if="$PART_FILE" of="$BFS_HDF" bs=512 seek=$(( BFS_OFFSET / 512 )) conv=notrunc 2>/dev/null
rm -f "$PART_FILE"
echo "  BFS: $BFS_HDF ($(du -h "$BFS_HDF" | cut -f1))"

# ── Create PFS3 HDF (256MB) ──────────────────────────────────
echo "Creating PFS3 partition (256MB)..."
PFS_HDF="$SCRIPT_DIR/bench-pfs3.hdf"
rm -f "$PFS_HDF"
rdbtool -f "$PFS_HDF" create size=256Mi cyls=512 heads=16 secs=32 \
    + init \
    + add name=DH2 start=2 end=511 dostype=0x50465303 bootable=False \
    + fsadd "$PFS3" version=19.2 dostype=0x50465303 >/dev/null 2>&1
echo "  PFS3: $PFS_HDF ($(du -h "$PFS_HDF" | cut -f1))"

# ── Done ──────────────────────────────────────────────────────
echo ""
echo "=== Done ==="
echo ""
echo "Test in FS-UAE:  ./emulator-test/run-bench.sh"
echo ""
echo "For real hardware, create a single RDB image:"
echo "  Partition 1: FFS boot (copy $WB contents)"
echo "  Partition 2: BFS 256MB (DosType 0x42465300)"
echo "  Partition 3: PFS3 256MB (DosType 0x50445303)"
echo "  Add bfshandler + pfs3aio to RDB filesystem entries"
