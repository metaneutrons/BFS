#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-btree-scratch-lifetime-2026-10-01
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=btree-scratch-lifetime-$suffix
    for source in "build/benchmark/$label/system/Results/"*.tsv \
        "build/benchmark/$label/system/Results/complete.txt" \
        "build/benchmark/$label/system/Results/info-after-format.txt"; do
        target=$destination/$label/system/Results/${source##*/}
        cmp "$source" "$target"
        shasum -a 256 "$source" "$target"
    done
done
for order in bfs-first pfs3-first; do
    label=single-block-absence-probe-$order
    for source in "build/benchmark/$label/system/Results/"*.tsv; do
        target=$destination/retained-probe-controls/$label/system/Results/${source##*/}
        cmp "$source" "$target"
        shasum -a 256 "$source" "$target"
    done
done
printf 'PASS: twenty new and four control TSVs plus completion/inventory copies are exact\n'
