#!/bin/bash
set -euo pipefail
root=/tmp/bfs-perf-crc.UBHsLz
archive=/home/fabian/.cache/bfs-performance/after-buffer-profile-2026-10-01
if pgrep -x fs-uae; then exit 1; fi
if [[ -e $archive ]]; then
    [[ -d $archive && -z $(find "$archive" -mindepth 1 -maxdepth 1 -print -quit) ]]
else
    mkdir -p "$archive"
fi
for label in after-buffer-probe-bfs-first after-buffer-probe-pfs3-first; do
    [[ -d $root/$label && -s $root/$label/system/Results/complete.txt ]]
    [[ ! -e $archive/$label ]]
    before=$(sha256sum "$root/$label/system/Results/bfs.deep-compare.tsv" "$root/$label/system/Results/pfs3.deep-compare.tsv" | awk '{print $1}')
    command mv "$root/$label" "$archive/$label"
    after=$(sha256sum "$archive/$label/system/Results/bfs.deep-compare.tsv" "$archive/$label/system/Results/pfs3.deep-compare.tsv" | awk '{print $1}')
    [[ $before == "$after" ]]
    sha256sum "$archive/$label/system/Results/bfs.deep-compare.tsv" "$archive/$label/system/Results/pfs3.deep-compare.tsv"
done
df -h /tmp /home/fabian
printf 'Two completed diagnostic images archived recoverably with identical TSV hashes\n'
