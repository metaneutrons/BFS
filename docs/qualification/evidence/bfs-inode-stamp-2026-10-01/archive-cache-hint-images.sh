#!/bin/bash
set -euo pipefail
root=/tmp/bfs-perf-crc.UBHsLz
archive=/home/fabian/.cache/bfs-performance/cache-hint-2026-10-01
labels=(baseline-bfs-first candidate-pfs3-first baseline-pfs3-first candidate-bfs-first repeat-candidate-bfs-first repeat-baseline-pfs3-first repeat-candidate-pfs3-first repeat-baseline-bfs-first)
if pgrep -x fs-uae; then exit 1; fi
[[ ! -e $archive ]]
mkdir -p "$archive"
for suffix in "${labels[@]}"; do
    label=hint-normal-$suffix
    [[ -d $root/$label && -s $root/$label/system/Results/complete.txt ]]
    [[ ! -e $archive/$label ]]
    before=$(sha256sum "$root/$label/system/Results/bfs.tsv" "$root/$label/system/Results/pfs3.tsv" | awk '{print $1}')
    command mv "$root/$label" "$archive/$label"
    after=$(sha256sum "$archive/$label/system/Results/bfs.tsv" "$archive/$label/system/Results/pfs3.tsv" | awk '{print $1}')
    [[ $before == "$after" ]]
    sha256sum "$archive/$label/system/Results/bfs.tsv" "$archive/$label/system/Results/pfs3.tsv"
done
df -h /tmp /home/fabian
printf 'Eight completed normal images archived recoverably with identical TSV hashes\n'
