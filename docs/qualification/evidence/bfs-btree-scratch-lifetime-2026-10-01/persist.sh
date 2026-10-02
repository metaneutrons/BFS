#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-btree-scratch-lifetime-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_log() {
    local source=$1 target=$2
    [[ -f $source ]]
    sed 's/[[:blank:]]*$//' "$source" | awk '{a[NR]=$0; if ($0 != "") last=NR} END {for (i=1;i<=last;i++) print a[i]}' > "$target"
}
for name in assembly focused-baseline-build focused-baseline focused-baseline-replay initializer-build initializer-positive initializer-negative-build initializer-negative host-existing sanitizer-existing host sanitizer quality quality-final linux-fuse linux-identities msan-preflight msan handler-build handler-rebuild handler-identities frozen-source-first frozen-source normal-preparation normal-preflight normal-measurements archive; do
    copy_log "build/btree-scratch-lifetime-$name.log" "$destination/$name.log"
done
for name in baseline.c candidate.c baseline.s candidate.s candidate.patch initializer-probe.c negative-core.c notes.md assembly.sh focused-baseline.sh linux.sh msan.sh preflight.sh normal-summary.mjs normal-summary.json probe-summary.mjs probe-summary.json independent-normal.md replay.sh; do
    command cp -f "build/btree-scratch-lifetime-$name" "$destination/$name"
done
for name in restored-build-first restored-build restoration counter-equivalence; do
    if [[ -f build/btree-scratch-lifetime-$name.log ]]; then
        copy_log "build/btree-scratch-lifetime-$name.log" "$destination/$name.log"
    fi
done
command cp -f tests/test_btree_scratch_lifetime.c "$destination/test_btree_scratch_lifetime.c"
command cp -f build/btree-scratch-lifetime-counter-equivalence.mjs "$destination/counter-equivalence.mjs"
for name in prepare-btree-scratch-lifetime-measurements.sh run-btree-scratch-lifetime-measurements.sh; do
    command cp -f "build/$name" "$destination/$name"
done
command cp -f docs/qualification/evidence/bfs-memory-copy-short-2026-10-01/metadata-single-extent-archive-images.sh "$destination/archive-images.sh"
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=btree-scratch-lifetime-$suffix
    source=build/benchmark/$label
    mkdir -p "$destination/$label/system/Results"
    command cp -f "$source/bench.fs-uae" "$destination/$label/bench.fs-uae"
    copy_log "$source/fs-uae.log" "$destination/$label/fs-uae.log"
    copy_log "build/$label-images.log" "$destination/$label/images.log"
    copy_log "build/$label-run.log" "$destination/$label/run.log"
    for result in "$source"/system/Results/*.tsv "$source"/system/Results/*.txt; do
        command cp -f "$result" "$destination/$label/system/Results/${result##*/}"
    done
done
for order in bfs-first pfs3-first; do
    label=single-block-absence-probe-$order
    source=build/benchmark/$label
    mkdir -p "$destination/retained-probe-controls/$label/system/Results"
    for result in "$source"/system/Results/*.tsv; do
        command cp -f "$result" "$destination/retained-probe-controls/$label/system/Results/${result##*/}"
    done
    for name in complete.txt info-after-format.txt; do
        command cp -f "$source/system/Results/$name" "$destination/retained-probe-controls/$label/system/Results/$name"
    done
done
for source in emulator-test/build-bench-image.sh emulator-test/run-bench.sh emulator-test/verify-bench-results.sh tools/check-no-binaries.sh; do
    command cp -f "$source" "$destination/${source##*/}"
done
command cp -f build/btree-scratch-lifetime-persist.sh "$destination/persist.sh"
