#!/bin/bash
# Reconstruct the original relative parser names without editing source bytes.
set -euo pipefail
evidence=$(cd "$(dirname "$0")" && pwd)
scratch=$(mktemp -d /tmp/bfs-short-copy-replay.XXXXXXXX)
[[ $scratch == /tmp/bfs-short-copy-replay.* && -d $scratch ]]
cleanup() {
    [[ $scratch == /tmp/bfs-short-copy-replay.* && -d $scratch ]] || return
    command rm -rf -- "$scratch"
}
trap cleanup EXIT
mkdir "$scratch/first" "$scratch/final"
command cp -f "$evidence/summary-first.mjs" "$scratch/first/memory-copy-short-summary.mjs"
command cp -f "$evidence/summary-first.mjs" "$scratch/first/memory-copy-short-summary-first.mjs"
command cp -f "$evidence/summary-probes-first.mjs" "$scratch/first/fixtures.mjs"
command cp -f "$evidence/summary-strict-red.mjs" "$scratch/first/strict-red.mjs"
command cp -f "$evidence/summary.mjs" "$scratch/final/memory-copy-short-summary.mjs"
command cp -f "$evidence/summary-probes.mjs" "$scratch/final/fixtures.mjs"
command cp -f "$evidence/summary-strict-green.mjs" "$scratch/final/strict-green.mjs"
shasum -a 256 "$scratch/first/memory-copy-short-summary.mjs" "$scratch/final/memory-copy-short-summary.mjs"
node "$scratch/first/fixtures.mjs"
set +e
node "$scratch/first/strict-red.mjs" > "$scratch/first-red.log" 2>&1
red_exit=$?
set -e
cat "$scratch/first-red.log"
[[ $red_exit -eq 1 ]]
rg -q 'duplicate-malformed-counter: expected reject, got 0' "$scratch/first-red.log"
printf 'PASS: exact first duplicate-counter fixture remains semantic RED with exit 1\n'
node "$scratch/final/fixtures.mjs"
node "$scratch/final/strict-green.mjs"
printf 'PASS: offline replay preserves first 24 fixtures and semantic RED, final 29 fixtures and 25 GREEN cases\n'
