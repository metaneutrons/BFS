#!/bin/bash
set -euo pipefail
root=/Users/fabian/.codex/worktrees/bfs-append-performance/BFS
cd "$root"
destination=docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01
[[ ! -e $destination ]]
mkdir -p "$destination"
copy_evidence() {
    local source=$1 target=$2 encoding
    mkdir -p "$(dirname "$target")"
    encoding=$(file --brief --mime-encoding --exclude=soft -- "$source")
    if [[ $source == *.c || $source == *.h || $source == *.s || $source == *replay-before-cdpath-fix.sh || $encoding != us-ascii && $encoding != utf-8 ]] || LC_ALL=C rg -q '[[:blank:]]+$' "$source"; then
        openssl base64 -in "$source" -out "$target.b64"
        printf '%s  %s\n' "$(shasum -a 256 "$source" | awk '{print $1}')" "$target" >> "$destination/decoded-identities.sha256"
    else
        command cp -f "$source" "$target"
    fi
}
for path in src/core/btree.c src/core/alloc.c src/amiga/perf_probe.h tools/fs-compare-bench.c; do
    copy_evidence "$path" "$destination/qualified-source/$path"
    copy_evidence "build/split-coalesce-attribution-source/$path" "$destination/diagnostic-source/$path"
done
copy_evidence build/split-coalesce-attribution-source/src/amiga/perf_paths.h "$destination/diagnostic-source/src/amiga/perf_paths.h"
for path in emulator-test/build-bench-image.sh emulator-test/verify-bench-results.sh tools/check-no-binaries.sh; do
    copy_evidence "$path" "$destination/${path##*/}"
done
for path in emulator-test/run-bench.sh emulator-test/verify-path-bench.sh; do
    copy_evidence "build/split-coalesce-attribution-source/$path" "$destination/diagnostic-source/$path"
done
for group in fixtures consumer; do
    while IFS= read -r path; do
        relative=${path#build/split-coalesce-attribution-"$group"/}
        copy_evidence "$path" "$destination/$group/$relative"
    done < <(rg --files --no-ignore "build/split-coalesce-attribution-$group" -g '*.c' -g '*.h' -g '*.py' -g '*.sh' -g '*.log' -g '*.txt' -g '*.md')
done
while IFS= read -r path; do
    name=${path#build/split-coalesce-attribution-}
    copy_evidence "$path" "$destination/$name"
done < <(rg --files --no-ignore build -g 'split-coalesce-attribution-*.log' -g 'split-coalesce-attribution-*.sh' -g 'split-coalesce-attribution-*.mjs' -g 'split-coalesce-attribution-*.json' -g 'split-coalesce-attribution-*.md' -g 'split-coalesce-attribution-*.patch' -g 'split-coalesce-attribution-abi-*.s' -g 'split-coalesce-attribution-abi-size.c' | awk 'index($0,"/")==6 && $0 !~ /\/.*\//')
for order in bfs-first pfs3-first; do
    label=split-coalesce-attribution-probe-$order
    source=build/benchmark/$label
    copy_evidence "$source/bench.fs-uae" "$destination/$label/bench.fs-uae"
    copy_evidence "$source/fs-uae.log" "$destination/$label/fs-uae.log"
    if [[ -f $source/runner-output.log ]]; then
        copy_evidence "$source/runner-output.log" "$destination/$label/runner-output.log"
    fi
    while IFS= read -r path; do
        copy_evidence "$path" "$destination/$label/system/Results/${path##*/}"
    done < <(rg --files --no-ignore "$source/system/Results" -g '*.tsv' -g '*.txt')
    control=single-block-absence-probe-$order
    while IFS= read -r path; do
        copy_evidence "$path" "$destination/retained-probe-controls/$control/system/Results/${path##*/}"
    done < <(rg --files --no-ignore "build/benchmark/$control/system/Results" -g '*.tsv' -g 'complete.txt' -g 'info-after-format.txt')
done
printf 'Persisted textual split/coalescence evidence\n'
