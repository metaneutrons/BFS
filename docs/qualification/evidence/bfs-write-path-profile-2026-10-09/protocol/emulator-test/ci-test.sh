#!/usr/bin/env bash
# Run the Amiga integration suite against a minimal, checksum-verified AROS system.
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)
build_dir=${BFS_BUILD_DIR:-"$project_dir/build"}
runtime_dir=${BFS_EMULATOR_RUNTIME:-"$build_dir/emulator"}
aros_dir=${BFS_AROS_DIR:-"$runtime_dir/aros"}
handler=${BFS_HANDLER:-"$build_dir/amiga/bfshandler"}
test_binary=${BFS_TEST_BINARY:-"$build_dir/amiga/bfs-test"}
bfs=${BFS_CLI:-"$build_dir/host/bfs"}
fixture=${BFS_TEST_HDF:-}
timeout_seconds=${BFS_TEST_TIMEOUT:-1800}
profile=${BFS_TEST_PROFILE:-full}
filter=${BFS_TEST_FILTER:-}

case "$profile" in
    full|quick) ;;
    *) printf 'ERROR: BFS_TEST_PROFILE must be full or quick.\n' >&2; exit 2 ;;
esac

[[ -z "$filter" || "$filter" =~ ^[A-Za-z0-9_-]+(\+[A-Za-z0-9_-]+)*$ ]] || {
    printf 'ERROR: BFS_TEST_FILTER contains unsupported characters.\n' >&2
    exit 2
}

[[ "$timeout_seconds" =~ ^[1-9][0-9]*$ ]] || {
    printf 'ERROR: BFS_TEST_TIMEOUT must be a positive integer.\n' >&2
    exit 2
}

mkdir -p "$aros_dir"
(cd "$project_dir" && tools/install-aros-rom.sh "$aros_dir")

fsuae=
if command -v fs-uae >/dev/null 2>&1; then
    fsuae=$(command -v fs-uae)
fi
[[ -n "$fsuae" ]] || { printf 'ERROR: fs-uae is required.\n' >&2; exit 2; }
[[ -f "$handler" ]] || { printf 'ERROR: handler not found: %s\n' "$handler" >&2; exit 2; }
[[ -f "$test_binary" ]] || {
    printf 'ERROR: integration test binary not found: %s\n' "$test_binary" >&2
    exit 2
}
if [[ -n "$fixture" ]]; then
    [[ -f "$fixture" ]] || { printf 'ERROR: HDF fixture not found: %s\n' "$fixture" >&2; exit 2; }
else
    [[ -x "$bfs" ]] || { printf 'ERROR: bfs not found: %s\n' "$bfs" >&2; exit 2; }
fi

mkdir -p "$runtime_dir"
run_dir=$(mktemp -d "$runtime_dir/run.XXXXXX")
system_dir="$run_dir/system"
hdf="$run_dir/test.hdf"
config="$run_dir/ci-test.fs-uae"
result="$system_dir/bfs-test.result"
emulator_log="$run_dir/fs-uae.log"
printf 'Integration evidence directory: %s\n' "$run_dir"
mkdir -p "$system_dir/C" "$system_dir/L" "$system_dir/S"
cp "$handler" "$system_dir/L/bfshandler"
cp "$test_binary" "$system_dir/C/bfs-test"
cp "$project_dir/tools/bfs-test-cases.def" "$run_dir/test-cases.def"
if [[ -n "$filter" ]]; then
    printf 'C:bfs-test DH1: LOG=SYS:bfs-test.result %s\n' "$filter" \
        > "$system_dir/S/Startup-Sequence"
else
    cp "$script_dir/startup/ci-test" "$system_dir/S/Startup-Sequence"
fi
if [[ "$profile" == quick ]]; then
    if [[ -n "$filter" ]]; then
        printf 'C:bfs-test DH1: LOG=SYS:bfs-test.result %s QUICK\n' "$filter" \
            > "$system_dir/S/Startup-Sequence"
    else
        printf 'C:bfs-test DH1: LOG=SYS:bfs-test.result QUICK\n' \
            > "$system_dir/S/Startup-Sequence"
    fi
fi

if [[ -n "$fixture" ]]; then
    cp "$fixture" "$hdf"
else
    dd if=/dev/zero of="$hdf" bs=1M count=32 status=none
    "$bfs" format "$hdf" --label BFSTest --block-size 4096
fi

cat > "$config" <<EOF
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
kickstart_file = $aros_dir/aros-amiga-m68k-rom.bin
kickstart_ext_file = $aros_dir/aros-amiga-m68k-ext.bin
hard_drive_0 = $system_dir
hard_drive_0_label = System
hard_drive_0_priority = 0
hard_drive_1 = $hdf
hard_drive_1_file_system = $system_dir/L/bfshandler
floppy_speed = 0
audio_driver = null
window_hidden = 1
automatic_input_grab = 0
EOF

digest=(shasum -a 256)
if command -v sha256sum >/dev/null 2>&1; then digest=(sha256sum); fi
"${digest[@]}" "$system_dir/L/bfshandler" "$system_dir/C/bfs-test" \
    "$aros_dir/aros-amiga-m68k-rom.bin" "$aros_dir/aros-amiga-m68k-ext.bin"

printf 'Starting FS-UAE integration test with a %ss timeout.\n' "$timeout_seconds"
command=("$fsuae" "$config")
if [[ "$(uname -s)" == Linux ]]; then
    command -v xvfb-run >/dev/null 2>&1 || {
        printf 'ERROR: xvfb-run is required on Linux.\n' >&2
        exit 2
    }
    command=(xvfb-run -a "${command[@]}")
fi
FSEMU_AUDIO_DRIVER=null python3 "$script_dir/ci_runner.py" \
    --result "$result" --log "$emulator_log" --profile "$profile" \
    --filter "$filter" \
    --timeout "$timeout_seconds" --inventory "$run_dir/test-cases.def" \
    -- "${command[@]}"
