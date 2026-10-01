#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-single-block-absence-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_log() {
    local source=$1 target=$2
    [[ -f $source ]]
    sed 's/[[:blank:]]*$//' "$source" | awk '{a[NR]=$0; if ($0 != "") last=NR} END {for (i=1;i<=last;i++) print a[i]}' > "$target"
}
for name in red red-build red2 red2-build green green-build host sanitizer quality linux-fuse m68k-build m68k-rebuild preparation preflight measurements identities archive; do
    copy_log "build/single-block-absence-$name.log" "$destination/$name.log"
done
copy_log build/single-block-absence-probe-summary-run1.log "$destination/probe-summary-run1.log"
for name in single-block-absence-baseline.c single-block-absence-tests-red1.c single-block-absence-tests-red2.c single-block-absence-candidate.patch single-block-absence-normal-summary.mjs single-block-absence-normal-summary.json single-block-absence-probe-summary.mjs single-block-absence-probe-summary.json single-block-absence-linux.sh single-block-absence-qualification-notes.md single-block-absence-normalize-formatter.mjs prepare-single-block-absence-measurements.sh run-single-block-absence-measurements.sh prepare-metadata-batch-measurements.sh crc-native-order-preflight.sh metadata-single-extent-archive-images.sh; do
    command cp -f "build/$name" "$destination/$name"
done
command cp -f build/single-block-absence-probe-summary-run1.mjs "$destination/probe-summary-run1.mjs"
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=single-block-absence-$suffix
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
    label=handler-cpu-scopes-extended-$order
    source=build/benchmark/$label
    mkdir -p "$destination/retained-probe-controls/$label/system/Results"
    for result in "$source"/system/Results/*.tsv; do
        command cp -f "$result" "$destination/retained-probe-controls/$label/system/Results/${result##*/}"
    done
done
for source in emulator-test/build-bench-image.sh emulator-test/run-bench.sh emulator-test/verify-bench-results.sh; do
    command cp -f "$source" "$destination/${source##*/}"
done
command cp -f build/single-block-absence-persist.sh "$destination/persist.sh"
