#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-btree-bulk-slots-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_log() {
    local source=$1 target=$2
    [[ -f $source ]]
    sed 's/[[:blank:]]*$//' "$source" | awk '{a[NR]=$0; if ($0 != "") last=NR} END {for (i=1;i<=last;i++) print a[i]}' > "$target"
}
for name in focused-run focused-old-build focused-old focused-identities focused-final-run focused-final-old-build focused-final-old focused-final-new-build focused-final-new focused-final-identities host-prebuild sanitizer-prebuild host sanitizer host-final sanitizer-final quality linux-fuse linux-focused-final m68k-build m68k-build-corrected m68k-rebuild m68k-identities map-build assembly libnix preparation preflight measurements source-identities identities archive restoration restored-host restored-sanitizer restored-m68k; do
    copy_log "build/btree-bulk-slots-$name.log" "$destination/$name.log"
done
for name in baseline.c candidate.patch tests-first.c baseline.s candidate.s link.map normal-summary.mjs normal-summary.json probe-summary.mjs probe-summary.json linux.sh focused.sh focused-final.sh assembly.sh libnix.sh libnix-memmove.s libnix-bcopy.s normalize-formatter.mjs preflight.sh qualification-notes.md; do
    command cp -f "build/btree-bulk-slots-$name" "$destination/$name"
done
command cp -f tests/test_btree_bulk_slots.c "$destination/test_btree_bulk_slots.c"
command cp -f build/btree-bulk-slots-candidate-string.h "$destination/candidate-string.h"
for name in prepare-btree-bulk-slots-measurements.sh run-btree-bulk-slots-measurements.sh metadata-single-extent-archive-images.sh; do
    command cp -f "build/$name" "$destination/$name"
done
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=btree-bulk-slots-$suffix
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
done
for source in emulator-test/build-bench-image.sh emulator-test/run-bench.sh emulator-test/verify-bench-results.sh tools/check-no-binaries.sh; do
    command cp -f "$source" "$destination/${source##*/}"
done
command cp -f build/btree-bulk-slots-persist.sh "$destination/persist.sh"
