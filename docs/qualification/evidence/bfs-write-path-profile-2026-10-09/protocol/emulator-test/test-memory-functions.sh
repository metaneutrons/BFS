#!/usr/bin/env bash
# Compile and run the 68020 memory-function probe in an isolated FS-UAE boot.
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)
cd "$project_dir"
run_root=${BFS_MEMORY_FUNCTIONS_RUN_ROOT:-"$project_dir/build/memory-functions"}
assets_system=${BFS_MEMORY_FUNCTIONS_SYSTEM_DIR:-"$project_dir/build/benchmark/retained-final-deep/system"}
retained_config="$project_dir/build/benchmark/retained-final-deep/bench.fs-uae"
timeout_seconds=${1:-60}
amiga_cc=${AMIGA_CC:-m68k-amigaos-gcc}

if ! [[ "$timeout_seconds" =~ ^[1-9][0-9]*$ ]] || ((timeout_seconds > 60)); then
    printf 'ERROR: timeout must be an integer from 1 to 60 seconds.\n' >&2
    exit 2
fi
command -v "$amiga_cc" >/dev/null 2>&1 || {
    printf 'ERROR: %s is required.\n' "$amiga_cc" >&2
    exit 2
}
command -v fs-uae >/dev/null 2>&1 || {
    printf 'ERROR: fs-uae is required.\n' >&2
    exit 2
}
[[ -d "$assets_system/C" ]] || {
    printf 'ERROR: retained Workbench assets not found: %s\n' "$assets_system" >&2
    exit 2
}

rom_file=${BFS_ROM_FILE:-}
if [[ -z "$rom_file" && -f "$retained_config" ]]; then
    rom_file=$(sed -n 's/^[[:space:]]*kickstart_file[[:space:]]*=[[:space:]]*//p' \
        "$retained_config" | head -n 1)
fi
[[ -n "$rom_file" && -f "$rom_file" ]] || {
    printf 'ERROR: Kickstart ROM not found; set BFS_ROM_FILE.\n' >&2
    exit 2
}

if [[ -n "${AMIGA_PREFIX:-}" ]]; then
    amiga_prefix=$AMIGA_PREFIX
else
    amiga_prefix="$(brew --prefix amiga-gcc 2>/dev/null || printf '%s' /opt/homebrew/opt/amiga-gcc)/m68k-amigaos"
fi
ndk_include="$amiga_prefix/ndk-include"
libnix="$amiga_prefix/libnix/lib"
amiga_lib="$amiga_prefix/lib"
[[ -d "$ndk_include" && -d "$libnix" && -d "$amiga_lib" ]] || {
    printf 'ERROR: Amiga GCC runtime not found below %s.\n' "$amiga_prefix" >&2
    exit 2
}

mkdir -p "$run_root"
run_dir=$(mktemp -d "$run_root/run.XXXXXX")
system_dir="$run_dir/system"
mkdir -p "$system_dir"
cp -R "$assets_system"/. "$system_dir"/
mkdir -p "$system_dir/C" "$system_dir/Results" "$system_dir/S"

probe_binary="$run_dir/memory-functions-probe"
"$amiga_cc" \
    -std=c99 -Wall -Wextra -Werror -Wno-pointer-sign \
    -O2 -m68020 -noixemul -fomit-frame-pointer -fno-builtin \
    -Isrc/amiga -Iinclude -Itests -DBFS_AMIGA=1 -I"$ndk_include" \
    -o "$probe_binary" \
    tests/amiga/memory_functions_probe.c src/amiga/memcpy_68k.s \
    -B"$libnix/" -L"$libnix" -L"$amiga_lib" -lamiga -s
cp "$probe_binary" "$system_dir/C/memory-functions-probe"

# The source Workbench is copied into this unique run directory, so clearing a
# stale result affects only this disposable emulator image.
rm -f "$system_dir/Results/memory-functions.txt" \
    "$system_dir/Results/memory-functions.done"
printf 'FailAt 21\nC:memory-functions-probe\n' >| \
    "$system_dir/S/Startup-Sequence"

config="$run_dir/memory-functions.fs-uae"
cat >| "$config" <<EOF
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
kickstart_file = $rom_file
hard_drive_0 = $system_dir
hard_drive_0_label = System
hard_drive_0_priority = 0
floppy_speed = 0
window_hidden = 1
automatic_input_grab = 0
audio_driver = null
EOF

result="$system_dir/Results/memory-functions.txt"
completion_marker="$system_dir/Results/memory-functions.done"
emulator_log="$run_dir/fs-uae.log"
emulator_pid=
cleanup() {
    if [[ -n "$emulator_pid" ]] && kill -0 "$emulator_pid" 2>/dev/null; then
        kill "$emulator_pid" 2>/dev/null || true
        wait "$emulator_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

printf 'Running memory-function probe in %s (timeout %ss).\n' \
    "$run_dir" "$timeout_seconds"
FSEMU_AUDIO_DRIVER=null fs-uae "$config" >"$emulator_log" 2>&1 &
emulator_pid=$!
deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$result" && -f "$completion_marker" ]]; then
        if grep -q '^MEMORY-FUNCTIONS-PASS$' "$result"; then
            cat "$result"
            cleanup
            emulator_pid=
            trap - EXIT INT TERM
            exit 0
        fi
        if grep -q '^MEMORY-FUNCTIONS-FAIL$' "$result"; then
            cat "$result"
            printf 'ERROR: memory-function probe reported failures.\n' >&2
            exit 1
        fi
    fi
    if ! kill -0 "$emulator_pid" 2>/dev/null; then
        break
    fi
    sleep 1
done

if [[ -f "$result" ]]; then cat "$result"; fi
printf 'ERROR: memory-function probe did not finish within %ss.\n' \
    "$timeout_seconds" >&2
tail -n 40 "$emulator_log" >&2 || true
exit 1
