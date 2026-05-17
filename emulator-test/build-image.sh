#!/bin/bash
# Build a complete RDB hard drive image for BFS testing
# DH0: FFS boot (Workbench + handlers + test binary)
# DH1: BFS test partition (formatted by Startup-Sequence)
# DH2: PFS3 bench partition (formatted by Startup-Sequence)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
WB_SRC="${BFS_WB_DIR:-$HOME/Documents/fswb/amigaos/amiga-os-3.2/Workbench3.2}"

IMAGE="$SCRIPT_DIR/test-drive.hdf"

BFS_HANDLER="$BUILD_DIR/amiga/bfshandler"
PFS3_HANDLER="${BFS_PFS3_HANDLER:-$HOME/Source/pfs3aio/pfs3aio}"
BFS_TEST="$BUILD_DIR/amiga/bfs-test"

# ── Prereqs ────────────────────────────────────────────────────
echo "=== Building Test Drive Image ==="
[ -f "$BFS_HANDLER" ] || { echo "ERROR: build bfshandler first (make amiga)"; exit 1; }
[ -f "$BFS_TEST" ] || { echo "ERROR: build bfs-test first (make amiga-test)"; exit 1; }
[ -f "$PFS3_HANDLER" ] || { echo "WARN: PFS3 handler not found at $PFS3_HANDLER"; PFS3_HANDLER=""; }

# ── Create RDB image with 3 partitions ─────────────────────────
rm -f "$IMAGE"
echo "Creating RDB image..."
rdbtool "$IMAGE" create size=256M heads=16 secs=63 + init + \
    add start=2 end=33 name=DH0 dostype=DOS3 bootable=True bootpri=0

# ── Format DH0 and populate ───────────────────────────────────
echo "Formatting DH0 (FFS)..."
xdftool "$IMAGE" open part=0 + format System

echo "Populating DH0..."
xdftool "$IMAGE" open part=0 + makedir S + makedir C + makedir L + makedir Devs + makedir Devs/DOSDrivers + makedir Libs

# System commands
for f in Mount Assign Dir Echo Type MakeDir Delete Rename Copy Wait Info \
         Format List Execute Run SetPatch Version Resident; do
    [ -f "$WB_SRC/C/$f" ] && xdftool "$IMAGE" open part=0 + write "$WB_SRC/C/$f" C/$f 2>/dev/null || true
done

# Libs
for f in version.library diskfont.library mathieeedoubbas.library; do
    [ -f "$WB_SRC/Libs/$f" ] && xdftool "$IMAGE" open part=0 + write "$WB_SRC/Libs/$f" Libs/$f 2>/dev/null || true
done

# Handlers
echo "Installing handlers..."
xdftool "$IMAGE" open part=0 + write "$BFS_HANDLER" L/bfshandler
[ -n "$PFS3_HANDLER" ] && xdftool "$IMAGE" open part=0 + write "$PFS3_HANDLER" L/pfs3handler || true

# Test binary
echo "Installing bfs-test..."
xdftool "$IMAGE" open part=0 + write "$BFS_TEST" C/bfs-test

# ── Mountlist entries for DH1 (BFS) and DH2 (PFS3) ────────────
cat > /tmp/mountlist-dh1 << 'EOF'
Handler = L:bfshandler
Stacksize = 32768
Priority = 5
GlobVec = -1
Device = uaehf.device
Unit = 0
Surfaces = 16
BlocksPerTrack = 63
LowCyl = 34
HighCyl = 255
Buffers = 50
BufMemType = 0
DosType = 0x42465300
EOF

cat > /tmp/mountlist-dh2 << 'EOF'
Handler = L:pfs3handler
Stacksize = 16384
Priority = 5
GlobVec = -1
Device = uaehf.device
Unit = 0
Surfaces = 16
BlocksPerTrack = 63
LowCyl = 256
HighCyl = 479
Buffers = 50
BufMemType = 0
DosType = 0x50465303
EOF

xdftool "$IMAGE" open part=0 + write /tmp/mountlist-dh1 Devs/DOSDrivers/DH1
[ -n "$PFS3_HANDLER" ] && xdftool "$IMAGE" open part=0 + write /tmp/mountlist-dh2 Devs/DOSDrivers/DH2 || true

# ── Startup-Sequence ───────────────────────────────────────────
cat > /tmp/startup-seq << 'EOF'
C:SetPatch >NIL: QUIET
C:Version >NIL:
Echo "=== BFS CI Test ==="
Wait 2
Echo "Formatting DH1:..."
Format DRIVE DH1: NAME BFSTest NOICONS <NIL:
Wait 1
Echo "Running integrity tests..."
C:bfs-test DH1:
Echo "=== Complete ==="
EOF
xdftool "$IMAGE" open part=0 + write /tmp/startup-seq S/Startup-Sequence

# ── Summary ────────────────────────────────────────────────────
echo ""
echo "=== Image built: $IMAGE ($(du -h "$IMAGE" | cut -f1)) ==="
rdbtool "$IMAGE" info
echo ""
echo "Use with FS-UAE:"
echo "  hard_drive_0 = $IMAGE"
echo "  hard_drive_0_type = rdb"
