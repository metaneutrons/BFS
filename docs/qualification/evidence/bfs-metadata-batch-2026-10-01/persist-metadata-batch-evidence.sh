#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
evidence=docs/qualification/evidence/bfs-metadata-batch-2026-10-01
[[ ! -e $evidence ]]
mkdir -p "$evidence"
for source in build/metadata-batch*.log build/metadata-batch*.patch; do
    command cp -f "$source" "$evidence/"
done
for source in prepare-metadata-batch-measurements.sh run-metadata-batch-measurements.sh metadata-batch-preflight.sh summarize-metadata-batch.js persist-metadata-batch-evidence.sh; do
    command cp -f "build/$source" "$evidence/"
done
command cp -f build/metadata-batch-comparison.json "$evidence/"
command cp -f tests/test_metadata_withdrawal_batch.c tests/test_metadata_withdrawal_batch_faults.c "$evidence/"
labels=(probe-bfs-first probe-pfs3-first normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first)
for suffix in "${labels[@]}"; do
    label=metadata-batch-$suffix
    target="$evidence/$label"
    mkdir -p "$target"
    command cp -f "build/$label-images.log" "build/$label-run.log" "$target/"
    command cp -f "build/benchmark/$label/bench.fs-uae" "$target/"
    rsync -a --include='*/' --include='*.tsv' --include='*.txt' --include='fs-uae.log' --exclude='*' "build/benchmark/$label/" "$target/"
done
rg --files --hidden --no-ignore "$evidence" | rg '\.log$' | while IFS= read -r filename; do
    perl -pi -e 's/[\t ]+$//g' "$filename"
done
printf 'Text-only metadata-batch evidence copied; timing TSVs are unchanged\n'
