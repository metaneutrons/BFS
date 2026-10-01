#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
root=$PWD
harness_sha=${1:?frozen short-mode harness SHA256}
[[ $harness_sha =~ ^[0-9a-f]{64}$ ]]
mkdir -p build/memory-copy-short
run_dir=$(mktemp -d "$root/build/memory-copy-short/run.XXXXXX")
label=memory-copy-short-${run_dir##*/}
remote=/tmp/bfs-perf-crc.UBHsLz/$label
printf 'Local directory: %s\nRemote label: %s\n' "$run_dir" "$label"
amiga_prefix=/opt/homebrew/opt/amiga-gcc/m68k-amigaos
[[ $(shasum -a 256 build/memory-copy-short-baseline.s | awk '{print $1}') == b1180cee112a9e395f7978a0fee877a03861ac8673cb62bb3f856fdfa3269eb3 ]]
[[ $(shasum -a 256 build/memory-copy-short-candidate.s | awk '{print $1}') == f9ecf90f6dc4716570f2789a29bbfdc54b27a76a871376075e907b14336bc9c3 ]]
[[ $(shasum -a 256 src/amiga/memcpy_68k.s | awk '{print $1}') == b1180cee112a9e395f7978a0fee877a03861ac8673cb62bb3f856fdfa3269eb3 ]]
[[ $(shasum -a 256 tests/amiga/memory_copy_compare_probe.c | awk '{print $1}') == "$harness_sha" ]]
[[ $(shasum -a 256 tests/amiga/crc32_abi_probe.s | awk '{print $1}') == de6988ba2350d5fa6735083df7591c4dce37f14f15933040ab4be23ef6852b8e ]]
command cp -f tests/amiga/memory_copy_compare_probe.c "$run_dir/memory-copy-probe-source.c"
command cp -f tests/amiga/crc32_abi_probe.s "$run_dir/abi-probe-source.s"
command cp -f build/memory-copy-short-candidate.s "$run_dir/memory-copy-candidate-source.s"
command cp -f build/memory-copy-short-baseline.s "$run_dir/memory-copy-retained-source.s"
sed -e 's/_memcpy/_memcpy_baseline/g' -e 's/_memset/_memset_baseline/g' build/memory-copy-short-baseline.s > "$run_dir/memory-copy-baseline-renamed.s"
m68k-amigaos-gcc -std=c99 -Wall -Wextra -Werror -Wno-pointer-sign \
    -O2 -m68020 -noixemul -fomit-frame-pointer -fno-builtin \
    -DBFS_MEMORY_COPY_SHORT_TIMING=1 \
    -Isrc/amiga -Iinclude -Itests -DBFS_AMIGA=1 -I"$amiga_prefix/ndk-include" \
    -o "$run_dir/memory-copy-probe" "$run_dir/memory-copy-probe-source.c" \
    "$run_dir/abi-probe-source.s" "$run_dir/memory-copy-candidate-source.s" \
    "$run_dir/memory-copy-baseline-renamed.s" \
    -B"$amiga_prefix/libnix/lib/" -L"$amiga_prefix/libnix/lib" \
    -L"$amiga_prefix/lib" -lamiga -s > "$run_dir/build.log" 2>&1
printf 'M68K_SHORT_MODE_GUEST_BUILD_PASS\n'
assets=build/benchmark/retained-final-deep/system
mkdir -p "$run_dir/system/C" "$run_dir/system/L" "$run_dir/system/Libs" "$run_dir/system/S" "$run_dir/system/Results"
command cp -R "$assets/C/." "$run_dir/system/C/"
command cp -R "$assets/L/." "$run_dir/system/L/"
command cp -R "$assets/Libs/." "$run_dir/system/Libs/"
command cp -f "$run_dir/memory-copy-probe" "$run_dir/system/C/memory-copy-probe"
printf 'FailAt 21\nStack 32768\nC:memory-copy-probe\n' > "$run_dir/system/S/Startup-Sequence"
printf '[fs-uae]\namiga_model = A1200\nchip_memory = 2048\nfast_memory = 8192\ncpu = 68040\nuae_cpu_speed = max\nuae_cpu_24bit_addressing = false\nkickstart_file = /home/fabian/Amiga/kick.a1200.47.102.rom\nhard_drive_0 = %s/system\nhard_drive_0_label = System\nhard_drive_0_priority = 0\nfloppy_speed = 0\nwindow_hidden = 1\nautomatic_input_grab = 0\naudio_driver = null\n' "$remote" > "$run_dir/memory-copy.fs-uae"
shasum -a 256 "$run_dir/memory-copy-probe-source.c" "$run_dir/abi-probe-source.s" "$run_dir/memory-copy-candidate-source.s" "$run_dir/memory-copy-retained-source.s" "$run_dir/memory-copy-baseline-renamed.s" "$run_dir/memory-copy-probe" "$run_dir/memory-copy.fs-uae" > "$run_dir/identities.log"
rsync -az "$run_dir/" "cachy:$remote/"
printf 'Prepared %s\n' "$label"
