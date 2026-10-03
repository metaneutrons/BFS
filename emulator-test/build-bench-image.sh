#!/bin/bash
# Build isolated HDF images for a checked BFS vs PFS3 comparison.
#
# Layout:
#   DH0: Directory filesystem (boot) — WB3.2 + neutral benchmark program
#   DH1: RDB HDF with BFS partition (255.5 MiB, pre-formatted)
#   DH2: RDB HDF with PFS3 partition (255.5 MiB, formatted on first boot)
#
# For real hardware (CF card), combine into single RDB image.
# For FS-UAE testing, use run-bench.sh which mounts them separately.
#
# Usage:
#   ./emulator-test/build-bench-image.sh
#
# Output: a private directory below build/benchmark, or BFS_BENCH_RUN_DIR.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCH_DIR="${BFS_BENCH_RUN_DIR:-$PROJECT_DIR/build/benchmark}"

ASSETS="${BFS_AMIGA_ASSETS_DIR:-$SCRIPT_DIR/.assets}"
ROM="${BFS_ROM_FILE:-$ASSETS/A1200.47.102.rom}"
PFS3="${BFS_PFS3_HANDLER:-$SCRIPT_DIR/.cache/pfs3aio}"
HANDLER="${BFS_BENCH_HANDLER_FILE:-$PROJECT_DIR/build/amiga/bfshandler}"
FORMATTER="${BFS_BENCH_FORMATTER_FILE:-$PROJECT_DIR/build/host/bfs}"
BENCH_ORDER="${BFS_BENCH_ORDER:-bfs-first}"
BENCH_MODE="${BFS_BENCH_MODE:-compare}"
case "$BENCH_ORDER" in
    bfs-first|pfs3-first) ;;
    *) echo "ERROR: BFS_BENCH_ORDER must be bfs-first or pfs3-first" >&2; exit 2 ;;
esac
case "$BENCH_MODE" in
    compare|durable-compare|profile|internal|deep|deep-compare) ;;
    *) echo "ERROR: BFS_BENCH_MODE must be compare, durable-compare, profile, internal, deep, or deep-compare" >&2; exit 2 ;;
esac
if [ "$BENCH_MODE" = internal ]; then
    GUEST_TOOL=fs-profile-bench
    GUEST_ARGS=internal
    RESULT_SUFFIX=internal.tsv
    COMPLETION_MARKER=BFS-INTERNAL-COMPLETE
elif [ "$BENCH_MODE" = profile ]; then
    GUEST_TOOL=fs-profile-bench
    GUEST_ARGS=
    RESULT_SUFFIX=profile.tsv
    COMPLETION_MARKER=BFS-PFS3-PROFILE-COMPLETE
elif [ "$BENCH_MODE" = deep ]; then
    GUEST_TOOL=fs-profile-bench
    GUEST_ARGS=deep
    RESULT_SUFFIX=deep.tsv
    COMPLETION_MARKER=BFS-PFS3-DEEP-COMPLETE
    HANDLER="${BFS_BENCH_HANDLER_FILE:-$PROJECT_DIR/build/amiga/bfshandler-probe}"
elif [ "$BENCH_MODE" = durable-compare ]; then
    GUEST_TOOL=fs-compare-bench
    GUEST_ARGS=durable
    RESULT_SUFFIX=durable.tsv
    COMPLETION_MARKER=BFS-PFS3-DURABLE-COMPLETE
elif [ "$BENCH_MODE" = deep-compare ]; then
    GUEST_TOOL=fs-compare-bench
    GUEST_ARGS=deep
    RESULT_SUFFIX=deep-compare.tsv
    COMPLETION_MARKER=BFS-PFS3-DEEP-COMPARE-COMPLETE
    HANDLER="${BFS_BENCH_HANDLER_FILE:-$PROJECT_DIR/build/amiga/bfshandler-probe}"
else
    GUEST_TOOL=fs-compare-bench
    GUEST_ARGS=
    RESULT_SUFFIX=tsv
    COMPLETION_MARKER=BFS-PFS3-COMPLETE
fi

# ── Prerequisites ─────────────────────────────────────────────
[ -f "$ROM" ] || { echo "ERROR: ROM not found: $ROM (set BFS_ROM_FILE)"; exit 1; }
[ -f "$PFS3" ] || { echo "ERROR: pfs3aio not found: $PFS3 (set BFS_PFS3_HANDLER)"; exit 1; }
[ -d "$ASSETS/C" ] || { echo "ERROR: Workbench commands not found: $ASSETS/C (set BFS_AMIGA_ASSETS_DIR)"; exit 1; }
command -v rdbtool >/dev/null || { echo "ERROR: rdbtool not found"; exit 1; }
[ -f "$HANDLER" ] || {
    if [ "$BENCH_MODE" = deep ] || [ "$BENCH_MODE" = deep-compare ]; then
        echo "ERROR: BFS probe handler not found: $HANDLER (run 'make amiga-perf-probe-handler' or set BFS_BENCH_HANDLER_FILE)" >&2
    else
        echo "ERROR: handler not found: $HANDLER" >&2
    fi
    exit 1
}
GUEST_FILE="${BFS_BENCH_GUEST_FILE:-$PROJECT_DIR/build/amiga/$GUEST_TOOL}"
[ -f "$GUEST_FILE" ] || { echo "ERROR: run 'make amiga-$GUEST_TOOL' first or set BFS_BENCH_GUEST_FILE"; exit 1; }
[ -f "$FORMATTER" ] || { echo "ERROR: build the host bfs formatter or set BFS_BENCH_FORMATTER_FILE"; exit 1; }

echo "=== Building BFS vs PFS3 Benchmark ==="

# ── Setup boot directory ──────────────────────────────────────
WB="$BENCH_DIR/system"
[ ! -e "$WB" ] || { echo "ERROR: refusing to overwrite $WB" >&2; exit 2; }
mkdir -p "$BENCH_DIR"
mkdir -p "$WB/C" "$WB/L" "$WB/Libs" "$WB/S" "$WB/Devs" "$WB/Results"

cp -R "$ASSETS/C/." "$WB/C/"
if [ -d "$ASSETS/L" ]; then cp -R "$ASSETS/L/." "$WB/L/"; fi
if [ -d "$ASSETS/Libs" ]; then cp -R "$ASSETS/Libs/." "$WB/Libs/"; fi
cp "$GUEST_FILE" "$WB/C/$GUEST_TOOL"
cp "$HANDLER" "$WB/L/bfshandler"
cp "$PFS3" "$WB/L/pfs3aio"

# ── Startup-Sequence ──────────────────────────────────────────
cat > "$WB/S/Startup-Sequence" << 'AMIGA'
; BFS vs PFS3 checked AmigaDOS benchmark
FailAt 21
Wait 3

Echo ""
Echo "============================================"
Echo "  BFS vs PFS3 Filesystem Benchmark"
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
C:Format DRIVE DH2: NAME PFSTest NOICONS QUICK <NIL: >SYS:Results/format-pfs3.txt
Wait 2
Info >SYS:Results/info-after-format.txt

Echo ""
AMIGA

if [ "$BENCH_ORDER" = bfs-first ]; then
    order=(bfs pfs3)
else
    order=(pfs3 bfs)
fi
for filesystem in "${order[@]}"; do
    if [ "$BENCH_MODE" = internal ] && [ "$filesystem" != bfs ]; then continue; fi
    if [ "$filesystem" = bfs ]; then drive=DH1; else drive=DH2; fi
    printf 'Echo "--- %s Benchmark (%s:) ---"\nC:%s %s: %s >SYS:Results/%s.%s\nEcho "AFTER_%s" >SYS:Results/phase-%s.txt\n' \
        "$filesystem" "$drive" "$GUEST_TOOL" "$drive" "$GUEST_ARGS" "$filesystem" "$RESULT_SUFFIX" "$filesystem" "$filesystem" >>"$WB/S/Startup-Sequence"
done
printf 'Echo "%s" >SYS:Results/complete.txt\n' "$COMPLETION_MARKER" >>"$WB/S/Startup-Sequence"
cat >> "$WB/S/Startup-Sequence" <<'AMIGA'

Echo ""
Echo "============================================"
Echo "  Benchmark complete!"
Echo "  Results saved to SYS:Results/"
Echo "============================================"
AMIGA

echo "  Boot directory: $WB"
echo "  Order: $BENCH_ORDER"
echo "  Mode: $BENCH_MODE"

# ── Create BFS HDF (256MB) ────────────────────────────────────
echo "Creating BFS partition (255.5 MiB)..."
BFS_HDF="$BENCH_DIR/bench-bfs.hdf"
[ ! -e "$BFS_HDF" ] || { echo "ERROR: refusing to overwrite $BFS_HDF" >&2; exit 2; }
rdbtool -f "$BFS_HDF" create size=256Mi cyls=512 heads=16 secs=32 \
    + init \
    + add name=DH1 start=2 end=1023 dostype=0x42465300 bootable=False \
    + fsadd "$HANDLER" version=1.0 dostype=0x42465300 >/dev/null 2>&1

# Pre-format BFS
BFS_OFFSET=$(( 2 * 16 * 32 * 512 ))
BFS_BLOCKS=$(( (1022 * 16 * 32 * 512) / 4096 ))
PART_FILE=$(mktemp)
trap 'rm -f "$PART_FILE"' EXIT
dd if=/dev/zero of="$PART_FILE" bs=4096 count="$BFS_BLOCKS" status=none
"$FORMATTER" format "$PART_FILE" --label BFSTest --block-size 4096 >/dev/null
dd if="$PART_FILE" of="$BFS_HDF" bs=512 seek=$(( BFS_OFFSET / 512 )) conv=notrunc status=none
rm -f "$PART_FILE"
trap - EXIT
echo "  BFS: $BFS_HDF ($(du -h "$BFS_HDF" | cut -f1))"

# ── Create PFS3 HDF (256MB) ──────────────────────────────────
echo "Creating PFS3 partition (255.5 MiB)..."
PFS_HDF="$BENCH_DIR/bench-pfs3.hdf"
[ ! -e "$PFS_HDF" ] || { echo "ERROR: refusing to overwrite $PFS_HDF" >&2; exit 2; }
rdbtool -f "$PFS_HDF" create size=256Mi cyls=512 heads=16 secs=32 \
    + init \
    + add name=DH2 start=2 end=1023 dostype=0x50465303 bootable=False \
    + fsadd "$PFS3" version="${BFS_PFS3_VERSION:-20.0}" dostype=0x50465303 >/dev/null 2>&1
echo "  PFS3: $PFS_HDF ($(du -h "$PFS_HDF" | cut -f1))"

# ── Done ──────────────────────────────────────────────────────
echo ""
echo "=== Done ==="
echo ""
echo "Test in FS-UAE: BFS_BENCH_RUN_DIR=$BENCH_DIR ./emulator-test/run-bench.sh"
echo ""
echo "For real hardware, create a single RDB image:"
echo "  Partition 1: FFS boot (copy $WB contents)"
echo "  Partition 2: BFS 255.5 MiB (DosType 0x42465300)"
echo "  Partition 3: PFS3 255.5 MiB (DosType 0x50465303)"
echo "  Add bfshandler + pfs3aio to RDB filesystem entries"
