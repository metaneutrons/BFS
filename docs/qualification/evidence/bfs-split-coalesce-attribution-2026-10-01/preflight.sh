#!/bin/bash
set -euo pipefail
cd /tmp/bfs-perf-crc.UBHsLz
for order in bfs-first pfs3-first; do
    label=split-coalesce-attribution-probe-$order
    [[ ! -e $label/system/Results/complete.txt && ! -e $label/bench.fs-uae && ! -e $label/fs-uae.log ]]
    [[ $(sha256sum "$label/system/L/bfshandler" | awk '{print $1}') == 4c5ae37e9afbc859bbe5c2d33815df50c66a9503d6c22a69575922ffd483d566 ]]
    [[ $(sha256sum "$label/system/C/fs-compare-bench" | awk '{print $1}') == 44581f7f8ce259b34a7521337a45f7f0957662a20a256e623730e3efb512a024 ]]
    [[ $(sha256sum "$label/system/L/pfs3aio" | awk '{print $1}') == bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 ]]
    sha256sum "$label/system/L/bfshandler" "$label/system/C/fs-compare-bench" "$label/system/L/pfs3aio" "$label/bench-bfs.hdf" "$label/bench-pfs3.hdf"
done
[[ $(sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom | awk '{print $1}') == 68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c ]]
sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom
sha256sum source-split-coalesce-attribution/src/core/{btree,alloc}.c source-split-coalesce-attribution/src/amiga/{perf_probe,perf_paths}.h source-split-coalesce-attribution/tools/fs-compare-bench.c source-split-coalesce-attribution/tools/attribution-consumer/consume.py
if ps -eo comm= | awk '$1 == "fs-uae" || $1 == "cc1" || $1 == "m68k-amigaos-gc" {found=1; print} END {exit !found}'; then
    printf 'Unexpected emulator or compiler present\n' >&2
    exit 1
fi
[[ $(systemctl --user is-active coffin.service || true) == inactive ]]
fs-uae --version
uname -a
df -h /tmp /home/fabian
printf 'Both fresh diagnostic systems and idle preflight verified\n'
