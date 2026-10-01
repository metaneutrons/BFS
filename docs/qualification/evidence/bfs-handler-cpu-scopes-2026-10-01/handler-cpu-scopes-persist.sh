#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
evidence=docs/qualification/evidence/bfs-handler-cpu-scopes-2026-10-01
[[ ! -e $evidence ]]
mkdir -p "$evidence"
for source in build/handler-cpu-scopes-*.log build/handler-cpu-scopes-*.patch build/handler-cpu-scopes-*.sh build/handler-cpu-scopes-*.mjs; do
    command cp -f "$source" "$evidence/"
done
command cp -f build/handler-cpu-scopes-summary.json "$evidence/"
for source in build/quality/handler-cpu-v11-*.log; do
    command cp -f "$source" "$evidence/"
done
for source in emulator-test/build-bench-image.sh emulator-test/run-bench.sh emulator-test/verify-bench-results.sh; do
    command cp -f "$source" "$evidence/"
done
labels=(control-bfs-first extended-pfs3-first control-pfs3-first extended-bfs-first)
for suffix in "${labels[@]}"; do
    label=handler-cpu-scopes-$suffix
    target=$evidence/$label
    mkdir -p "$target"
    command cp -f "build/$label-images.log" "build/$label-run.log" "$target/"
    rsync -a --include='*/' --include='*.tsv' --include='*.txt' --include='bench.fs-uae' --include='fs-uae.log' --exclude='*' "build/benchmark/$label/" "$target/"
done
rg --files --hidden --no-ignore "$evidence" | rg '\.log$' | while IFS= read -r filename; do
    perl -0777 -pi -e 's/[\t ]+$//mg; s/\n\n+\z/\n/' "$filename"
done
printf 'Text-only diagnostic evidence copied; timing TSVs are unchanged\n'
