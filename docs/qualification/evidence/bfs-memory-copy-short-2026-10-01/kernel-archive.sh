#!/bin/bash
set -euo pipefail
root=/tmp/bfs-perf-crc.UBHsLz
archive=/home/fabian/.cache/bfs-performance/memory-copy-short-kernel-2026-10-01
if pgrep -x fs-uae; then exit 1; fi
[[ ! -e $archive ]]
for label in memory-copy-short-run.l3US52 memory-copy-short-run.dnMTQJ; do
    [[ -d $root/$label && -f $root/$label/memory-copy.fs-uae ]]
done
before=$(sha256sum "$root/memory-copy-short-run.l3US52/system/Results/memory-copy-compare.txt" "$root/memory-copy-short-run.l3US52/system/Results/memory-copy-compare.done" | awk '{print $1}')
mkdir -p "$archive"
command mv "$root/memory-copy-short-run.l3US52" "$archive/memory-copy-short-run.l3US52"
command mv "$root/memory-copy-short-run.dnMTQJ" "$archive/memory-copy-short-run.dnMTQJ"
after=$(sha256sum "$archive/memory-copy-short-run.l3US52/system/Results/memory-copy-compare.txt" "$archive/memory-copy-short-run.l3US52/system/Results/memory-copy-compare.done" | awk '{print $1}')
[[ $before == "$after" ]]
sha256sum "$archive/memory-copy-short-run.l3US52/system/Results/memory-copy-compare.txt" "$archive/memory-copy-short-run.l3US52/system/Results/memory-copy-compare.done"
printf 'One completed kernel system and one unused reproducibility preparation archived recoverably; two result hashes unchanged\n'
