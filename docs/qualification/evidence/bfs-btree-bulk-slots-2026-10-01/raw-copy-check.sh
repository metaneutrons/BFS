#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-btree-bulk-slots-2026-10-01
count=0
while IFS= read -r -d '' result; do
    relative=${result#build/benchmark/}
    cmp "$result" "$destination/$relative"
    shasum -a 256 "$destination/$relative"
    count=$((count + 1))
done < <(find build/benchmark -path '*/btree-bulk-slots-*/system/Results/*.tsv' -type f -print0)
[[ $count -eq 20 ]]
printf 'All twenty copied candidate/control raw TSV files are byte-identical\n'
