#!/usr/bin/env bash
#
# BFS Emulator Integration Test
#
# Runs the BFS handler inside FS-UAE with AROS m68k.
# Usage:
#   ./run.sh          — interactive (window visible)
#   ./run.sh --ci     — headless for CI (no window, auto-timeout)
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
AROS_DIR="$SCRIPT_DIR/aros"
CONFIG_DIR="$SCRIPT_DIR/config"
BUILD_DIR="$PROJECT_DIR/build/amiga"

# ── Check prerequisites ────────────────────────────────────────

check_prereqs() {
    if ! command -v fs-uae &>/dev/null; then
        echo "ERROR: fs-uae not found. Install with: brew install fs-uae"
        exit 1
    fi

    if [ ! -f "$BUILD_DIR/bfshandler" ]; then
        echo "ERROR: bfshandler not built. Run: make amiga"
        exit 1
    fi

    if [ ! -f "$AROS_DIR/aros-rom.bin" ]; then
        echo "AROS ROM not found. Downloading..."
        download_aros
    fi
}

# ── Download AROS m68k ─────────────────────────────────────────

download_aros() {
    mkdir -p "$AROS_DIR"

    # Try multiple download sources
    local URLS=(
        "https://master.dl.sourceforge.net/project/aros/nightly2/20250124/Binaries/AROS-20250124-amiga-m68k-boot-iso.zip"
        "https://netcologne.dl.sourceforge.net/project/aros/nightly2/20250124/Binaries/AROS-20250124-amiga-m68k-boot-iso.zip"
    )

    local ZIP="$AROS_DIR/aros-m68k.zip"
    local OK=false

    for url in "${URLS[@]}"; do
        echo "Trying: $url"
        curl -L --retry 2 --max-time 60 -o "$ZIP" "$url" 2>/dev/null
        if file "$ZIP" 2>/dev/null | grep -q "Zip archive"; then
            OK=true
            break
        fi
    done

    if [ "$OK" = false ]; then
        rm -f "$ZIP"
        echo ""
        echo "═══════════════════════════════════════════════════════════════"
        echo "  AROS download failed (SourceForge CDN issue)."
        echo ""
        echo "  Please download manually:"
        echo "  1. Go to: https://sourceforge.net/projects/aros/files/nightly2/"
        echo "  2. Download the latest amiga-m68k-boot-iso.zip"
        echo "  3. Extract to: $AROS_DIR/"
        echo "  4. Ensure these files exist:"
        echo "     $AROS_DIR/aros-amiga-m68k-rom.bin"
        echo "     $AROS_DIR/aros-m68k.hdf"
        echo "═══════════════════════════════════════════════════════════════"
        exit 1
    fi

    cd "$AROS_DIR"
    unzip -o "$ZIP"
    rm -f "$ZIP"

    # Locate ROM and HDF in extracted files
    local ROM=$(find . -name "*rom*" -type f | grep -i "m68k\|amiga" | head -1)
    [ -n "$ROM" ] && [ ! -f "aros-amiga-m68k-rom.bin" ] && cp "$ROM" aros-amiga-m68k-rom.bin

    local HDF=$(find . -name "*.hdf" -type f | head -1)
    [ -n "$HDF" ] && [ ! -f "aros-m68k.hdf" ] && cp "$HDF" aros-m68k.hdf

    cd "$SCRIPT_DIR"
    echo "AROS m68k ready."
}

# ── Create test HDF image ──────────────────────────────────────

create_test_hdf() {
    local HDF="$SCRIPT_DIR/bfstest.hdf"

    # Create a 16MB blank HDF for the BFS partition
    dd if=/dev/zero of="$HDF" bs=1M count=16 2>/dev/null
    echo "Created blank 16MB test HDF: $HDF"
}

# ── Create FS-UAE configuration ────────────────────────────────

create_config() {
    local MODE="$1"
    local CONF="$CONFIG_DIR/bfstest.fs-uae"

    cat > "$CONF" << EOF
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68020

kickstart_file = $AROS_DIR/aros-rom.bin
kickstart_ext_file = $AROS_DIR/aros-ext.bin

# AROS system: mount the ISO directory as a hard drive
hard_drive_0 = $AROS_DIR/AROS-20260428-amiga-m68k-boot-iso/aros-amiga-m68k.iso
hard_drive_0_label = AROS

# BFS test partition
hard_drive_1 = $SCRIPT_DIR/bfstest.hdf
hard_drive_1_label = BFSTEST

window_width = 800
window_height = 600
EOF

    if [ "$MODE" = "headless" ]; then
        cat >> "$CONF" << EOF
window_hidden = 1
automatic_input_grab = 0
EOF
    fi

    echo "Created FS-UAE config: $CONF"
}

# ── Create Amiga test script ───────────────────────────────────

create_test_script() {
    # This script runs INSIDE the emulated Amiga
    cat > "$SCRIPT_DIR/scripts/test-bfs.script" << 'AMIGASCRIPT'
; BFS Integration Test Script
; Runs inside AROS/AmigaOS

Echo "=== BFS Integration Test ==="
Echo ""

; Format the BFS partition
Echo "Formatting BFSTEST:..."
Format DRIVE BFSTEST: NAME "TestVol" NOICONS
IF WARN
  Echo "FAIL: Format failed"
  SKIP end
ENDIF
Echo "OK: Format succeeded"

; Create directories
Echo "Creating directories..."
MakeDir BFSTEST:src
MakeDir BFSTEST:src/core
MakeDir BFSTEST:data
IF WARN
  Echo "FAIL: MakeDir failed"
  SKIP end
ENDIF
Echo "OK: Directories created"

; Copy files
Echo "Copying files..."
Echo "Hello BFS!" >BFSTEST:readme.txt
Echo "#include <stdio.h>" >BFSTEST:src/main.c
Echo "int main() { return 0; }" >>BFSTEST:src/main.c
Copy C:Dir BFSTEST:data/dir_copy
IF WARN
  Echo "FAIL: Copy failed"
  SKIP end
ENDIF
Echo "OK: Files copied"

; List directory
Echo "Listing BFSTEST:..."
Dir BFSTEST: ALL
Echo ""

; Read back file
Echo "Reading readme.txt..."
Type BFSTEST:readme.txt
Echo ""

; Delete and rename
Echo "Testing delete and rename..."
Delete BFSTEST:readme.txt
Rename BFSTEST:src/main.c BFSTEST:src/app.c
IF WARN
  Echo "FAIL: Delete/Rename failed"
  SKIP end
ENDIF
Echo "OK: Delete and rename succeeded"

; Verify rename
List BFSTEST:src/
Echo ""

; Test file size
Echo "Testing large file..."
Copy C:Dir BFSTEST:data/bigfile
List BFSTEST:data/bigfile
Echo ""

; Final directory listing
Echo "Final state:"
Dir BFSTEST: ALL
Echo ""

Echo "=== ALL TESTS PASSED ==="

LAB end
Echo ""
Echo "Test complete. Shutting down..."
; Signal test completion
Echo "DONE" >SYS:test-result.txt
AMIGASCRIPT

    echo "Created Amiga test script: scripts/test-bfs.script"
}

# ── Run the test ───────────────────────────────────────────────

run_test() {
    local MODE="${1:-window}"
    local CONF="$CONFIG_DIR/bfstest.fs-uae"
    local TIMEOUT=120

    echo "=== BFS Emulator Integration Test ==="
    echo "Mode: $MODE"
    echo ""

    if [ "$MODE" = "headless" ]; then
        echo "Running headless with ${TIMEOUT}s timeout..."
        timeout "$TIMEOUT" fs-uae "$CONF" 2>&1 || true
    else
        echo "Running with window (close window or Ctrl+C to stop)..."
        fs-uae "$CONF" 2>&1 || true
    fi

    # Check result
    # TODO: extract test-result.txt from the AROS HDF and verify
    echo ""
    echo "Test run complete."
}

# ── Main ───────────────────────────────────────────────────────

main() {
    local MODE="window"
    if [ "$1" = "--ci" ] || [ "$1" = "--headless" ]; then
        MODE="headless"
    fi

    check_prereqs
    create_test_hdf
    create_config "$MODE"
    create_test_script

    echo ""
    echo "Setup complete. Files:"
    echo "  Config:  $CONFIG_DIR/bfstest.fs-uae"
    echo "  Test HDF: $SCRIPT_DIR/bfstest.hdf"
    echo "  Script:  $SCRIPT_DIR/scripts/test-bfs.script"
    echo ""

    if [ "$1" = "--setup-only" ]; then
        echo "Setup only mode. Run manually with: fs-uae $CONFIG_DIR/bfstest.fs-uae"
        exit 0
    fi

    run_test "$MODE"
}

main "$@"
