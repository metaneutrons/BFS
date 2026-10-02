#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-crc-native-word-order-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_log() {
    local source=$1 target=$2
    [[ -f $source ]]
    sed 's/[[:blank:]]*$//' "$source" | awk '{a[NR]=$0; if ($0 != "") last=NR} END {for (i=1;i<=last;i++) print a[i]}' > "$target"
}
for name in crc32-native-order-host.log crc32-native-order-sanitizer.log crc32-native-order-quality.log crc32-native-order-linux-fuse.log crc32-native-order-m68k-build.log crc32-native-order-m68k-rebuild.log crc32-native-order-reproduction.log crc32-native-order-assembler-run1.log crc32-native-order-assembler-run2.log crc32-native-order-model-run1.log crc32-native-order-model-run2.log crc32-native-order-prepare-attempt1.log crc32-native-order-prepare-attempt2.log crc32-native-order-prepare-attempt3.log crc-native-order-preparation.log crc-native-order-preflight.log crc-native-order-run.log crc-native-order-restored-build.log crc-native-order-restoration.log crc-native-order-review-notes.log crc-native-order-final-identities.log crc-native-order-completed-image-archive.log; do
    copy_log "build/$name" "$destination/$name"
done
for name in crc32-native-order-model-v1.mjs crc32-native-order-model.mjs crc32-native-order-summary.mjs crc-native-order-normal-summary.mjs crc-native-order-normal-summary.json crc32-native-order-prepare-pilot1.sh crc32-native-order-prepare.sh crc32-native-order-remote-run.sh prepare-metadata-batch-measurements.sh prepare-crc-native-order-measurements.sh crc-native-order-preflight.sh run-crc-native-order-measurements.sh metadata-single-extent-archive-images.sh crc-native-order-rejected-production.patch crc32-native-order-candidate.patch; do
    command cp -f "build/$name" "$destination/$name"
done
command cp -f build/crc32-native-order-baseline.s "$destination/crc32-retained-source.s"
mkdir -p "$destination/failed-build1"
copy_log build/crc32-native-order/run.44Fuan/build.log "$destination/failed-build1/build.log"
command cp -f build/crc32-native-order/run.44Fuan/crc32-probe-source-build1.c "$destination/failed-build1/crc32-probe-source-reconstructed.c"
for pair in 'pilot1 run.FnUvHJ' 'pilot2 run.s0tN5q'; do
    read -r label run <<< "$pair"
    mkdir -p "$destination/$label"
    for name in crc32-candidate-source.s crc32-baseline-renamed.s crc32.fs-uae identities.log summary.json; do
        command cp -f "build/crc32-native-order/$run/$name" "$destination/$label/$name"
    done
    copy_log "build/crc32-native-order/$run/build.log" "$destination/$label/build.log"
    copy_log "build/crc32-native-order/$run/remote-run.log" "$destination/$label/remote-run.log"
    copy_log "build/crc32-native-order/$run/fs-uae.log" "$destination/$label/fs-uae.log"
    command cp -f "build/crc32-native-order/$run/system/Results/crc32-probe.txt" "$destination/$label/crc32-probe.txt"
    command cp -f "build/crc32-native-order/$run/system/Results/crc32-probe.done" "$destination/$label/crc32-probe.done"
done
suffixes=(normal-baseline-bfs-first normal-candidate-pfs3-first normal-baseline-pfs3-first normal-candidate-bfs-first normal-repeat-candidate-bfs-first normal-repeat-baseline-pfs3-first normal-repeat-candidate-pfs3-first normal-repeat-baseline-bfs-first probe-bfs-first probe-pfs3-first)
for suffix in "${suffixes[@]}"; do
    label=crc-native-order-$suffix
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
for source in emulator-test/build-bench-image.sh emulator-test/run-bench.sh emulator-test/verify-bench-results.sh; do
    command cp -f "$source" "$destination/${source##*/}"
done
command cp -f build/crc-native-order-persist.sh "$destination/persist.sh"
(
    cd "$destination"
    files=$(find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort)
    while IFS= read -r file; do shasum -a 256 "$file"; done <<< "$files" > SHA256SUMS
    shasum -a 256 -c SHA256SUMS
)
