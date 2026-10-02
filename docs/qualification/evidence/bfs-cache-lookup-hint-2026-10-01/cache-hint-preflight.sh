#!/bin/bash
set -euo pipefail
cd /tmp/bfs-perf-crc.UBHsLz
labels=(baseline-bfs-first candidate-pfs3-first baseline-pfs3-first candidate-bfs-first repeat-candidate-bfs-first repeat-baseline-pfs3-first repeat-candidate-pfs3-first repeat-baseline-bfs-first)
for suffix in "${labels[@]}"; do
    label=hint-normal-$suffix
    if [[ $suffix == *candidate* ]]; then
        expected=c832040b679669bda0de4e88c047da078d05dcc08624ebf7e6cfcca0b2f0e16e
    else
        expected=c4d4241a45b84c768ec9498e77144e650812e31576c952925f4ed1de65be0d5a
    fi
    actual=$(sha256sum "$label/system/L/bfshandler" | awk '{print $1}')
    [[ $actual == "$expected" ]]
    actual=$(sha256sum "$label/system/C/fs-compare-bench" | awk '{print $1}')
    [[ $actual == 1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80 ]]
    actual=$(sha256sum "$label/system/L/pfs3aio" | awk '{print $1}')
    [[ $actual == bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 ]]
    sha256sum "$label/system/L/bfshandler" "$label/system/C/fs-compare-bench" "$label/system/L/pfs3aio"
done
actual=$(sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom | awk '{print $1}')
[[ $actual == 68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c ]]
sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom
if ps -eo comm= | awk '$1 == "fs-uae" || $1 == "cc1" || $1 == "m68k-amigaos-gc" {found=1; print} END {exit !found}'; then
    printf 'Unexpected emulator or compiler present\n' >&2
    exit 1
fi
[[ $(systemctl --user is-active coffin.service || true) == inactive ]]
fs-uae --version
uname -a
df -h /tmp /home/fabian
printf 'All eight image inputs and idle preflight verified\n'
