#!/bin/bash
set -euo pipefail
candidate_normal=${1:?candidate normal SHA256 required}
candidate_probe=${2:?candidate probe SHA256 required}
prefix=${3:-metadata-batch}
[[ $prefix =~ ^[a-z0-9-]+$ ]]
[[ $candidate_normal =~ ^[0-9a-f]{64}$ && $candidate_probe =~ ^[0-9a-f]{64}$ ]]
cd /tmp/bfs-perf-crc.UBHsLz
suffixes=(probe-bfs-first probe-pfs3-first normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first)
for suffix in "${suffixes[@]}"; do
    label=$prefix-$suffix
    [[ ! -e $label/system/Results/complete.txt ]]
    if [[ $suffix == probe-* ]]; then expected=$candidate_probe
    elif [[ $suffix == *candidate* ]]; then expected=$candidate_normal
    else expected=572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3
    fi
    actual=$(sha256sum "$label/system/L/bfshandler" | awk '{print $1}')
    [[ $actual == "$expected" ]]
    actual=$(sha256sum "$label/system/C/fs-compare-bench" | awk '{print $1}')
    [[ $actual == 1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80 ]]
    actual=$(sha256sum "$label/system/L/pfs3aio" | awk '{print $1}')
    [[ $actual == bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 ]]
    sha256sum "$label/system/L/bfshandler" "$label/system/C/fs-compare-bench" "$label/system/L/pfs3aio" "$label/bench-bfs.hdf" "$label/bench-pfs3.hdf"
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
printf 'All ten fresh image inputs and idle preflight verified\n'
