#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
destination=docs/qualification/evidence/bfs-memory-copy-48-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_log() {
    sed 's/[[:blank:]]*$//' "$1" | awk '{a[NR]=$0; if ($0 != "") last=NR} END {for (i=1;i<=last;i++) print a[i]}' > "$2"
}
for name in memory-copy-48-prepare.sh memory-copy-48-remote-run.sh memory-copy-48-summary.mjs memory-copy-48-summary-probes.mjs memory-copy-48-archive.sh memory-copy-48-candidate.patch memory-copy-48-baseline.s; do
    command cp -f "build/$name" "$destination/$name"
done
for name in preparation-attempt1 preparation-attempt2 preparation-attempt3 restored-build; do
    copy_log "build/memory-copy-48-$name.log" "$destination/$name.log"
done
for name in archive final-identities assembler assembler-run2 summary-probes; do
    copy_log "build/memory-copy-48/$name.log" "$destination/$name.log"
done
for pair in 'failed-build run.MT99aB' 'timeout60 run.Eg3Hzc' 'completed300 run.TG9eTx'; do
    read -r label run <<< "$pair"
    source=build/memory-copy-48/$run
    mkdir -p "$destination/$label"
    for name in memory-copy-probe-source.c abi-probe-source.s memory-copy-candidate-source.s memory-copy-retained-source.s memory-copy-baseline-renamed.s; do
        command cp -f "$source/$name" "$destination/$label/$name"
    done
    copy_log "$source/build.log" "$destination/$label/build.log"
    if [[ $label != failed-build ]]; then
        for name in memory-copy.fs-uae identities.log; do
            command cp -f "$source/$name" "$destination/$label/$name"
        done
        copy_log "$source/remote-run.log" "$destination/$label/remote-run.log"
        copy_log "$source/fs-uae.log" "$destination/$label/fs-uae.log"
    fi
done
for name in memory-copy-compare.txt memory-copy-compare.done; do
    command cp -f "build/memory-copy-48/run.TG9eTx/system/Results/$name" "$destination/completed300/$name"
done
command cp -f build/memory-copy-48/run.TG9eTx/summary.json "$destination/completed300/summary.json"
command cp -f build/memory-copy-48-persist.sh "$destination/persist.sh"
(
    cd "$destination"
    inventory=$(find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort)
    while IFS= read -r artifact; do shasum -a 256 "$artifact"; done <<< "$inventory" > SHA256SUMS
    shasum -a 256 -c SHA256SUMS
)
