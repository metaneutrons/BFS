#!/bin/bash
# BFS CI Integration Test Runner
# Formats HDF, starts FS-UAE, waits for test completion, evaluates results.
# Exit 0 = all pass, Exit 1 = failure
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
EMU_DIR="$SCRIPT_DIR"
WB_DIR="${BFS_WB_DIR:-$HOME/Documents/fswb/amigaos/amiga-os-3.2/Workbench3.2}"
ROM_FILE="${BFS_ROM_FILE:-$HOME/Documents/amiga/rom/kick.a1200.47.102.rom}"
TIMEOUT="${BFS_TEST_TIMEOUT:-120}"

HDF="$EMU_DIR/test.hdf"
CONFIG="$EMU_DIR/config/ci-test.fs-uae"
RESULT_FILE="$WB_DIR/bfs-test.result"

# ── Prerequisites ──────────────────────────────────────────────
check_prereqs() {
    local fail=0
    [ -f "$ROM_FILE" ] || { echo "ERROR: ROM not found: $ROM_FILE"; fail=1; }
    [ -d "$WB_DIR" ] || { echo "ERROR: Workbench dir not found: $WB_DIR"; fail=1; }
    [ -f "$BUILD_DIR/amiga/bfshandler" ] || { echo "ERROR: bfshandler not built"; fail=1; }
    [ -f "$BUILD_DIR/amiga/bfs-test" ] || { echo "ERROR: bfs-test not built"; fail=1; }
    command -v fs-uae >/dev/null 2>&1 || command -v /opt/homebrew/bin/fs-uae >/dev/null 2>&1 || { echo "ERROR: fs-uae not found"; fail=1; }
    [ $fail -eq 0 ] || exit 1
}

# ── Format HDF ─────────────────────────────────────────────────
format_hdf() {
    echo "Formatting test HDF (32MB, 4K blocks)..."
    "$BUILD_DIR/host/mkbfs" "$HDF"
}

# ── Deploy binaries ────────────────────────────────────────────
deploy() {
    cp "$BUILD_DIR/amiga/bfshandler" "$WB_DIR/L/bfshandler"
    cp "$BUILD_DIR/amiga/bfs-test" "$WB_DIR/C/bfs-test"
    cp "$EMU_DIR/startup/ci-test" "$WB_DIR/S/Startup-Sequence"
    rm -f "$RESULT_FILE"
}

# ── Generate FS-UAE config ─────────────────────────────────────
gen_config() {
    cat > "$CONFIG" << EOF
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max

kickstart_file = $ROM_FILE

hard_drive_0 = $WB_DIR
hard_drive_0_label = System
hard_drive_0_priority = 0

hard_drive_1 = $HDF
hard_drive_1_file_system = $BUILD_DIR/amiga/bfshandler
hard_drive_1_label = BFSTest

floppy_speed = 0
window_width = 800
window_height = 600
EOF
}

# ── Run FS-UAE ─────────────────────────────────────────────────
run_emulator() {
    local FSUAE
    FSUAE=$(command -v fs-uae 2>/dev/null || echo /opt/homebrew/bin/fs-uae)

    echo "Starting FS-UAE (timeout=${TIMEOUT}s)..."
    if [ "$(uname)" = "Linux" ]; then
        # CI: use xvfb for headless OpenGL
        xvfb-run -a timeout "$TIMEOUT" "$FSUAE" "$CONFIG" >/dev/null 2>&1 || true
    else
        # macOS: run with display (or headless env vars if supported)
        FSEMU_AUDIO_DRIVER=null \
            timeout "$TIMEOUT" "$FSUAE" "$CONFIG" >/dev/null 2>&1 || true
    fi

    # Wait for result file (WB dir is live-synced)
    if [ ! -f "$RESULT_FILE" ]; then
        echo "ERROR: Test did not produce result file (timeout or crash)"
        return 1
    fi
}

# ── Evaluate results ───────────────────────────────────────────
evaluate() {
    echo ""
    echo "=== Test Results ==="
    cat "$RESULT_FILE"
    echo ""

    if grep -q "FAIL" "$RESULT_FILE"; then
        echo "FAILED"
        return 1
    fi
    if grep -q "RESULTS:.*passed" "$RESULT_FILE"; then
        echo "ALL TESTS PASSED"
        return 0
    fi
    echo "ERROR: Could not parse results"
    return 1
}

# ── Main ───────────────────────────────────────────────────────
main() {
    echo "=== BFS CI Integration Test ==="
    check_prereqs
    format_hdf
    deploy
    gen_config
    run_emulator
    evaluate
}

main "$@"
