#!/usr/bin/env bash
#
# BFS Stress Test — Full emulator automation
#
# 1. Build handler + stress test binary
# 2. Create 256MB HDF, pre-format with BFS
# 3. Copy stress test binary to WB/C/
# 4. Write Startup-Sequence that runs the test
# 5. Start FS-UAE with 120s timeout
# 6. Read test-report.txt from WB dir
# 7. Run verify, exit with result
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/amiga"
BUILD_HOST="$PROJECT_DIR/build/host"
CONFIG_DIR="$SCRIPT_DIR/config"
AROS_DIR="$SCRIPT_DIR/aros"
HDF="$SCRIPT_DIR/bfs-stress.hdf"
HDF_SIZE=256  # MB
TIMEOUT=120
REPORT="$SCRIPT_DIR/test-report.txt"

# ── Check prerequisites ────────────────────────────────────────

check_prereqs() {
    if ! command -v fs-uae &>/dev/null; then
        echo "ERROR: fs-uae not found. Install with: brew install fs-uae"
        exit 1
    fi
    if ! command -v m68k-amigaos-gcc &>/dev/null; then
        echo "ERROR: m68k-amigaos-gcc not found. Install amiga-gcc."
        exit 1
    fi
}

# ── Build ──────────────────────────────────────────────────────

build_all() {
    echo "=== Building handler + stress test ==="
    make -C "$PROJECT_DIR" amiga amiga-stresstest

    echo "=== Building verify tool ==="
    mkdir -p "$BUILD_HOST"
    cc -std=c99 -Wall -O2 -o "$BUILD_HOST/verify-stress" "$SCRIPT_DIR/verify-stress.c"
}

# ── Create and prepare HDF ─────────────────────────────────────

create_hdf() {
    echo "=== Creating ${HDF_SIZE}MB test HDF ==="
    dd if=/dev/zero of="$HDF" bs=1M count=$HDF_SIZE 2>/dev/null
    echo "Created: $HDF"
}

# ── Create FS-UAE config ───────────────────────────────────────

create_config() {
    local CONF="$CONFIG_DIR/bfs-stress.fs-uae"
    local KICKSTART="${AMIGA_KICKSTART:-${HOME}/Documents/amiga/rom/kick.a1200.47.102.rom}"
    local WB_DIR="${AMIGA_WORKBENCH:-${HOME}/Documents/fswb/amigaos/amiga-os-3.2/Workbench3.2}"

    cat > "$CONF" << EOF
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68020
floppy_speed = 0
uae_cpu_speed = max

kickstart_file = $KICKSTART

hard_drive_0 = $WB_DIR
hard_drive_0_label = System
hard_drive_0_priority = 0

hard_drive_1 = $HDF
hard_drive_1_label = Work
hard_drive_1_file_system = $BUILD_DIR/bfshandler

window_width = 800
window_height = 600
EOF

    if [ "${1:-}" = "--ci" ]; then
        cat >> "$CONF" << EOF
window_hidden = 1
automatic_input_grab = 0
EOF
    fi

    echo "Created config: $CONF"
    echo "$CONF"
}

# ── Install stress test binary into WB ─────────────────────────

install_stresstest() {
    local WB_DIR="${AMIGA_WORKBENCH:-${HOME}/Documents/fswb/amigaos/amiga-os-3.2/Workbench3.2}"
    local C_DIR="$WB_DIR/C"
    local S_DIR="$WB_DIR/S"

    echo "=== Installing stress test binary ==="
    cp "$BUILD_DIR/bfs-stresstest" "$C_DIR/bfs-stresstest"

    # Write Startup-Sequence that runs the stress test
    cat > "$S_DIR/Startup-Sequence" << 'EOF'
; BFS Stress Test Startup
Wait 3
C:bfs-stresstest Work: >SYS:test-report.txt
Wait 2
C:UAEQuit
EOF

    echo "Installed to $C_DIR and $S_DIR"
}

# ── Run emulator ──────────────────────────────────────────────

run_emulator() {
    local CONF="$1"
    echo "=== Running FS-UAE (timeout ${TIMEOUT}s) ==="

    if command -v gtimeout &>/dev/null; then
        gtimeout "$TIMEOUT" fs-uae "$CONF" 2>&1 || true
    elif command -v timeout &>/dev/null; then
        timeout "$TIMEOUT" fs-uae "$CONF" 2>&1 || true
    else
        # macOS fallback: background + sleep + kill
        fs-uae "$CONF" &
        local PID=$!
        sleep "$TIMEOUT" && kill "$PID" 2>/dev/null &
        wait "$PID" 2>/dev/null || true
    fi
}

# ── Extract results ────────────────────────────────────────────

extract_results() {
    local WB_DIR="${AMIGA_WORKBENCH:-${HOME}/Documents/fswb/amigaos/amiga-os-3.2/Workbench3.2}"
    local RESULT="$WB_DIR/test-report.txt"

    if [ -f "$RESULT" ]; then
        cp "$RESULT" "$REPORT"
        echo "=== Test Report ==="
        cat "$REPORT"
        echo ""
    else
        echo "ERROR: test-report.txt not found at $RESULT"
        exit 1
    fi
}

# ── Verify ─────────────────────────────────────────────────────

verify() {
    echo "=== Verifying results ==="
    "$BUILD_HOST/verify-stress" "$REPORT"
}

# ── Main ───────────────────────────────────────────────────────

main() {
    local MODE=""
    [ "${1:-}" = "--ci" ] && MODE="--ci"

    check_prereqs
    build_all
    create_hdf

    local CONF
    CONF=$(create_config "$MODE")

    install_stresstest
    run_emulator "$CONF"
    extract_results
    verify
}

main "$@"
