#!/bin/bash
set -euo pipefail
archive=/home/fabian/.cache/bfs-performance/memory-copy-48-2026-10-01
mkdir -p "$archive"
for label in memory-copy-48-run.Eg3Hzc memory-copy-48-run.TG9eTx; do
    source=/tmp/bfs-perf-crc.UBHsLz/$label
    target=$archive/$label
    [[ -d $source && ! -e $target ]]
    before=$(sha256sum "$source/memory-copy-probe" | awk '{print $1}')
    if [[ $label == memory-copy-48-run.TG9eTx ]]; then
        before_report=$(sha256sum "$source/system/Results/memory-copy-compare.txt" | awk '{print $1}')
        before_marker=$(sha256sum "$source/system/Results/memory-copy-compare.done" | awk '{print $1}')
    else
        [[ ! -e $source/system/Results/memory-copy-compare.txt && ! -e $source/system/Results/memory-copy-compare.done ]]
    fi
    command mv -- "$source" "$target"
    after=$(sha256sum "$target/memory-copy-probe" | awk '{print $1}')
    [[ $before == "$after" ]]
    printf '%s binary %s -> %s\n' "$label" "$before" "$after"
    if [[ $label == memory-copy-48-run.TG9eTx ]]; then
        after_report=$(sha256sum "$target/system/Results/memory-copy-compare.txt" | awk '{print $1}')
        after_marker=$(sha256sum "$target/system/Results/memory-copy-compare.done" | awk '{print $1}')
        [[ $before_report == "$after_report" && $before_marker == "$after_marker" ]]
        printf '%s report %s -> %s\n' "$label" "$before_report" "$after_report"
        printf '%s marker %s -> %s\n' "$label" "$before_marker" "$after_marker"
    fi
done
printf 'Two completed/terminated private systems archived recoverably; 4 digest pairs match\n'
