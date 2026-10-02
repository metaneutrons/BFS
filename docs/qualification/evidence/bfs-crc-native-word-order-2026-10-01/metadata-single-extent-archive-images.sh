#!/bin/bash
set -euo pipefail
root=/tmp/bfs-perf-crc.UBHsLz
prefix=${1:-metadata-batch}
[[ $prefix =~ ^[a-z0-9-]+$ ]]
archive=/home/fabian/.cache/bfs-performance/$prefix-2026-10-01
if pgrep -x fs-uae; then exit 1; fi
[[ ! -e $archive ]]
mkdir -p "$archive"
archive_case() {
    local label=$1 mode=$2
    local first=bfs.tsv second=pfs3.tsv
    if [[ $mode == deep-compare ]]; then
        first=bfs.deep-compare.tsv
        second=pfs3.deep-compare.tsv
    fi
    [[ -d $root/$label && -s $root/$label/system/Results/complete.txt ]]
    [[ ! -e $archive/$label ]]
    local before after
    before=$(sha256sum "$root/$label/system/Results/$first" "$root/$label/system/Results/$second" | awk '{print $1}')
    command mv "$root/$label" "$archive/$label"
    after=$(sha256sum "$archive/$label/system/Results/$first" "$archive/$label/system/Results/$second" | awk '{print $1}')
    [[ $before == "$after" ]]
    sha256sum "$archive/$label/system/Results/$first" "$archive/$label/system/Results/$second"
}
for order in bfs-first pfs3-first; do
    archive_case "$prefix-probe-$order" deep-compare
done
labels=(baseline-bfs-first candidate-pfs3-first baseline-pfs3-first candidate-bfs-first repeat-candidate-bfs-first repeat-baseline-pfs3-first repeat-candidate-pfs3-first repeat-baseline-bfs-first)
for suffix in "${labels[@]}"; do
    archive_case "$prefix-normal-$suffix" compare
done
df -h /tmp /home/fabian
printf 'Ten completed %s images archived recoverably with identical TSV hashes\n' "$prefix"
