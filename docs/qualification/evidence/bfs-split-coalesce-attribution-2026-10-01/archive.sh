#!/bin/bash
set -euo pipefail
base=/tmp/bfs-perf-crc.UBHsLz
destination=/home/fabian/.cache/bfs-performance/split-coalesce-attribution-2026-10-01
[[ ! -e $destination ]]
if pgrep -x fs-uae; then exit 1; fi
mkdir -p "$destination"
for order in bfs-first pfs3-first; do
    label=split-coalesce-attribution-probe-$order
    [[ -f $base/$label/system/Results/complete.txt && ! -e $destination/$label ]]
    for filesystem in bfs pfs3; do
        file=system/Results/$filesystem.deep-compare.tsv
        expected=$(sha256sum "$base/$label/$file" | awk '{print $1}')
        printf 'Before %s %s %s\n' "$label" "$file" "$expected"
    done
    before=$(cd "$base/$label" && sha256sum system/Results/{bfs,pfs3}.deep-compare.tsv)
    command mv -- "$base/$label" "$destination/$label"
    after=$(cd "$destination/$label" && sha256sum system/Results/{bfs,pfs3}.deep-compare.tsv)
    [[ $before == "$after" ]]
    printf '%s\n' "$after"
    [[ ! -e $base/$label ]]
done
if pgrep -x fs-uae; then exit 1; fi
printf 'PASS: both completed systems recoverably archived; all four TSV hashes unchanged\n'
