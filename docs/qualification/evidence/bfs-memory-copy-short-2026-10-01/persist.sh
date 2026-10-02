#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-memory-copy-short-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_log() {
    local source=$1 target=$2
    [[ -f $source ]]
    sed 's/[[:blank:]]*$//' "$source" | awk '{a[NR]=$0; if ($0 != "") last=NR} END {for (i=1;i<=last;i++) print a[i]}' > "$target"
}
for name in assembly preparation-1 preparation-2 reproducibility consumer-fixtures consumer-first-strict-red consumer-fixtures-final consumer-strict-green run-1 handler-build handler-rebuild handler-identities quality host sanitizer linux-fuse linux-identities normal-preparation normal-preflight normal-measurements frozen-source archive kernel-archive restored-build restoration evidence-fixtures-first evidence-fixtures-replay; do
    copy_log "build/memory-copy-short-$name.log" "$destination/$name.log"
done
for name in baseline.s candidate.s baseline-disassembly.s candidate-disassembly.s code-bytes.mjs code-bytes.json candidate.patch harness.patch harness-baseline.c prepare.sh remote-run.sh summary-first.mjs summary-probes-first.mjs summary-strict-red.mjs summary-strict-green.mjs summary.mjs summary-probes.mjs micro-raw-summary.json micro-means.mjs micro-means.json normal-summary.mjs normal-summary.json probe-summary.mjs probe-summary.json notes.md preflight.sh linux.sh normalize-formatter.mjs kernel-archive.sh replay-consumer-fixtures.sh; do
    command cp -f "build/memory-copy-short-$name" "$destination/$name"
done
command cp -f tests/amiga/memory_copy_compare_probe.c "$destination/memory_copy_compare_probe.c"
command cp -f tests/amiga/crc32_abi_probe.s "$destination/crc32_abi_probe.s"
for name in prepare-memory-copy-short-measurements.sh run-memory-copy-short-measurements.sh metadata-single-extent-archive-images.sh; do
    command cp -f "build/$name" "$destination/$name"
done
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=memory-copy-short-$suffix
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
for short in run.l3US52 run.dnMTQJ; do
    source=build/memory-copy-short/$short
    target=$destination/kernel-$short
    mkdir -p "$target/system/Results"
    copy_log "$source/build.log" "$target/build.log"
    command cp -f "$source/memory-copy.fs-uae" "$target/memory-copy.fs-uae"
    command cp -f "$source/identities.log" "$target/identities.log"
    for name in memory-copy-probe-source.c abi-probe-source.s memory-copy-candidate-source.s memory-copy-retained-source.s memory-copy-baseline-renamed.s; do
        command cp -f "$source/$name" "$target/$name"
    done
    if [[ $short == run.l3US52 ]]; then
        copy_log "$source/fs-uae.log" "$target/fs-uae.log"
        command cp -f "$source/system/Results/memory-copy-compare.txt" "$target/system/Results/memory-copy-compare.txt"
        command cp -f "$source/system/Results/memory-copy-compare.done" "$target/system/Results/memory-copy-compare.done"
    fi
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
command cp -f build/memory-copy-short-persist.sh "$destination/persist.sh"
