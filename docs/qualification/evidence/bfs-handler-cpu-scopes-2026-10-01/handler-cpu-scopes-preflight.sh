#!/bin/bash
set -euo pipefail
extended_probe=${1:?extended handler SHA256}
extended_guest=${2:?extended guest SHA256}
[[ $extended_probe =~ ^[0-9a-f]{64}$ && $extended_guest =~ ^[0-9a-f]{64}$ ]]
cd /tmp/bfs-perf-crc.UBHsLz
labels=(control-bfs-first extended-pfs3-first control-pfs3-first extended-bfs-first)
for suffix in "${labels[@]}"; do
    label=handler-cpu-scopes-$suffix
    [[ ! -e $label/system/Results/complete.txt && ! -e $label/bench.fs-uae ]]
    if [[ $suffix == control-* ]]; then
        handler=070b162d85ebbe2aa67774b48dea2543db7b0e864270c1c05096680498660389
        guest=1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80
    else
        handler=$extended_probe
        guest=$extended_guest
    fi
    [[ $(sha256sum "$label/system/L/bfshandler" | awk '{print $1}') == "$handler" ]]
    [[ $(sha256sum "$label/system/C/fs-compare-bench" | awk '{print $1}') == "$guest" ]]
    [[ $(sha256sum "$label/system/L/pfs3aio" | awk '{print $1}') == bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 ]]
    sha256sum "$label/system/L/bfshandler" "$label/system/C/fs-compare-bench" "$label/system/L/pfs3aio" "$label/system/S/Startup-Sequence" "$label/bench-bfs.hdf" "$label/bench-pfs3.hdf"
done
[[ $(sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom | awk '{print $1}') == 68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c ]]
sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom
if ps -eo comm= | awk '$1 == "fs-uae" || $1 == "cc1" || $1 == "m68k-amigaos-gc" {found=1; print} END {exit !found}'; then
    printf 'Unexpected emulator or compiler present\n' >&2
    exit 1
fi
[[ $(systemctl --user is-active coffin.service || true) == inactive ]]
fs-uae --version
uname -a
df -h /tmp /home/fabian
printf 'Four fresh diagnostic image inputs and idle preflight verified\n'
