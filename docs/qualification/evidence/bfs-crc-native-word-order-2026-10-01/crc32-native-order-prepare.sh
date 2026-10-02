#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
root=$PWD
mkdir -p build/crc32-native-order
run_dir=$(mktemp -d "$root/build/crc32-native-order/run.XXXXXX")
label=crc32-native-order-${run_dir##*/}
remote=/tmp/bfs-perf-crc.UBHsLz/$label
printf 'Local directory: %s\nRemote label: %s\n' "$run_dir" "$label"
amiga_prefix=/opt/homebrew/opt/amiga-gcc/m68k-amigaos
[[ $(shasum -a 256 build/crc32-native-order-baseline.s | awk '{print $1}') == 5611527d918ac1196160dcec955e60045e25d41ef479340586d36470777dd090 ]]
[[ $(shasum -a 256 src/amiga/crc32_68k.s | awk '{print $1}') == 46b89a015bd771dbdc48acdedc4765e07ed2567710bf09eb228d0d139d370af6 ]]
[[ $(shasum -a 256 tests/amiga/crc32_probe.c | awk '{print $1}') == e0aca17922beb0ee3311174cc9e08cdcf39ce73ee549ef1e62c8250299c31607 ]]
[[ $(shasum -a 256 tests/amiga/crc32_abi_probe.s | awk '{print $1}') == de6988ba2350d5fa6735083df7591c4dce37f14f15933040ab4be23ef6852b8e ]]
command cp -f tests/amiga/crc32_probe.c "$run_dir/crc32-probe-source.c"
command cp -f tests/amiga/crc32_abi_probe.s "$run_dir/crc32-abi-probe-source.s"
command cp -f src/amiga/crc32_68k.s "$run_dir/crc32-candidate-source.s"
sed 's/_bfs_crc32/_bfs_crc32_baseline/g' build/crc32-native-order-baseline.s > "$run_dir/crc32-baseline-renamed.s"
m68k-amigaos-gcc -std=c99 -Wall -Wextra -Werror -Wno-pointer-sign \
    -O2 -m68020 -noixemul -fomit-frame-pointer -fno-builtin \
    -Isrc/amiga -Iinclude -Itests -DBFS_AMIGA=1 -I"$amiga_prefix/ndk-include" \
    -o "$run_dir/crc32-probe" tests/amiga/crc32_probe.c \
    tests/amiga/crc32_abi_probe.s src/amiga/crc32_68k.s \
    "$run_dir/crc32-baseline-renamed.s" \
    -B"$amiga_prefix/libnix/lib/" -L"$amiga_prefix/libnix/lib" \
    -L"$amiga_prefix/lib" -lamiga -s > "$run_dir/build.log" 2>&1
printf 'M68K_GUEST_BUILD_PASS\n'
assets=build/benchmark/retained-final-deep/system
mkdir -p "$run_dir/system/C" "$run_dir/system/L" "$run_dir/system/Libs" "$run_dir/system/S" "$run_dir/system/Results"
command cp -R "$assets/C/." "$run_dir/system/C/"
command cp -R "$assets/L/." "$run_dir/system/L/"
command cp -R "$assets/Libs/." "$run_dir/system/Libs/"
command cp -f "$run_dir/crc32-probe" "$run_dir/system/C/crc32-probe"
printf 'FailAt 21\nStack 32768\nC:crc32-probe\n' > "$run_dir/system/S/Startup-Sequence"
printf '[fs-uae]\namiga_model = A1200\nchip_memory = 2048\nfast_memory = 8192\ncpu = 68040\nuae_cpu_speed = max\nuae_cpu_24bit_addressing = false\nkickstart_file = /home/fabian/Amiga/kick.a1200.47.102.rom\nhard_drive_0 = %s/system\nhard_drive_0_label = System\nhard_drive_0_priority = 0\nfloppy_speed = 0\nwindow_hidden = 1\nautomatic_input_grab = 0\naudio_driver = null\n' "$remote" > "$run_dir/crc32.fs-uae"
shasum -a 256 src/amiga/crc32_68k.s tests/amiga/crc32_probe.c tests/amiga/crc32_abi_probe.s build/crc32-native-order-baseline.s "$run_dir/crc32-baseline-renamed.s" "$run_dir/crc32-probe" "$run_dir/crc32.fs-uae" > "$run_dir/identities.log"
rsync -az "$run_dir/" "cachy:$remote/"
printf 'Prepared %s\n' "$label"
