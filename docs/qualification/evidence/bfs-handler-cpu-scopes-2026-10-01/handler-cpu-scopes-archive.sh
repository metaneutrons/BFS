#!/bin/bash
set -euo pipefail
qual=/tmp/bfs-perf-crc.UBHsLz
archive=/home/fabian/.cache/bfs-performance/handler-cpu-scopes-2026-10-01
labels=(control-bfs-first extended-pfs3-first control-pfs3-first extended-bfs-first)
before=()
for suffix in "${labels[@]}"; do
    source=$qual/handler-cpu-scopes-$suffix
    destination=$archive/handler-cpu-scopes-$suffix
    [[ -d $source && ! -L $source && ! -e $destination ]]
    [[ $(tr -d '\r\n' < "$source/system/Results/complete.txt") == BFS-PFS3-DEEP-COMPARE-COMPLETE ]]
    for filesystem in bfs pfs3; do
        output=$source/system/Results/$filesystem.deep-compare.tsv
        [[ -s $output ]]
        before+=("$(sha256sum "$output" | awk '{print $1}')")
        sha256sum "$output"
    done
done
if ps -eo comm= | awk '$1 == "fs-uae" {found=1; print} END {exit !found}'; then
    printf 'Emulator remains active; no archive performed\n' >&2
    exit 1
fi
mkdir -p "$archive"
for suffix in "${labels[@]}"; do
    command mv "$qual/handler-cpu-scopes-$suffix" "$archive/handler-cpu-scopes-$suffix"
done
index=0
for suffix in "${labels[@]}"; do
    for filesystem in bfs pfs3; do
        output=$archive/handler-cpu-scopes-$suffix/system/Results/$filesystem.deep-compare.tsv
        [[ $(sha256sum "$output" | awk '{print $1}') == "${before[$index]}" ]]
        sha256sum "$output"
        index=$((index + 1))
    done
done
printf 'Archived four completed images recoverably; eight TSV hashes unchanged\n'
df -h /tmp /home/fabian
