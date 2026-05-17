#!/bin/bash
# BFS vs PFS3 benchmark
set -e
cd "$(dirname "$0")"

WB="$HOME/Documents/fswb/amigaos/amiga-os-3.2/Workbench3.2"
BFS_DIR="$(cd ../.. && pwd)"

# Build handler
(cd "$BFS_DIR" && make amiga 2>/dev/null)

# Create/format HDFs
dd if=/dev/zero of=bfs.hdf bs=4096 count=8192 2>/dev/null
"$BFS_DIR/build/host/mkbfs" bfs.hdf
dd if=/dev/zero of=pfs3.hdf bs=512 count=65536 2>/dev/null

# Deploy DiskSpeed
cp ~/Source/DiskSpeed/DiskSpeed "$WB/C/" 2>/dev/null || true

# Swap Startup-Sequence for benchmark
cp "$WB/S/Startup-Sequence" "$WB/S/Startup-Sequence.test"
cat > "$WB/S/Startup-Sequence" << 'EOF'
Echo "=== BFS vs PFS3 Benchmark ==="
Echo ""
Echo "--- BFS (DH1:) ---"
C:DiskSpeed DRIVE DH1: FAST NOCPU MINTIME 1
Echo ""
Echo "--- PFS3 (DH2:) ---"
C:DiskSpeed DRIVE DH2: FAST NOCPU MINTIME 1
Echo ""
Echo "=== DONE ==="
EOF

echo "Startup-Sequence set to benchmark mode."
echo "Run: ~/Source/fs-uae/fs-uae $(pwd)/bench.fs-uae"
echo ""
echo "To restore test mode: cp '$WB/S/Startup-Sequence.test' '$WB/S/Startup-Sequence'"
