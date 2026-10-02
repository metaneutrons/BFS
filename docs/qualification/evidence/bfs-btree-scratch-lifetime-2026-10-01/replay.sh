#!/bin/bash
set -euo pipefail
evidence=$(cd "$(dirname "$0")" && pwd)
node "$evidence/source-identities.mjs"
scratch=$(mktemp -d /tmp/bfs-scratch-replay.XXXXXX)
[[ $scratch == /tmp/bfs-scratch-replay.* && -d $scratch ]]
cleanup() {
    [[ $scratch == /tmp/bfs-scratch-replay.* && -d $scratch ]] || return 1
    command rm -r -- "$scratch"
}
trap cleanup EXIT
mkdir -p "$scratch/build/benchmark" "$scratch/emulator-test"
command cp -f "$evidence/verify-bench-results.sh" "$scratch/emulator-test/verify-bench-results.sh"
chmod +x "$scratch/emulator-test/verify-bench-results.sh"
for directory in "$evidence"/btree-scratch-lifetime-* "$evidence"/retained-probe-controls/single-block-absence-*; do
    [[ -d $directory ]]
    command cp -R "$directory" "$scratch/build/benchmark/"
done
cd "$scratch"
node "$evidence/normal-summary.mjs" > normal.json
node "$evidence/probe-summary.mjs" > probe.json
node "$evidence/counter-equivalence.mjs" probe.json
cmp normal.json "$evidence/normal-summary.json"
cmp probe.json "$evidence/probe-summary.json"
printf 'PASS: offline strict verification and both exact JSON extractions reproduce\n'
