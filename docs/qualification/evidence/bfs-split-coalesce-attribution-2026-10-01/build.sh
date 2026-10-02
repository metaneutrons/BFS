#!/bin/bash
set -euo pipefail
root=/Users/fabian/.codex/worktrees/bfs-append-performance/BFS
cd "$root/build/split-coalesce-attribution-source"
for suffix in first second; do
    destination=$root/build/amiga-split-coalesce-attribution-$suffix
    [[ ! -e $destination ]]
    set -x
    make amiga amiga-perf-probe-handler amiga-fs-compare-bench BUILD_AMIGA="$destination"
    cmp "$destination/bfshandler" "$root/build/amiga-single-block-absence/bfshandler"
    set +x
    shasum -a 256 "$destination/bfshandler" "$destination/bfshandler-probe" "$destination/fs-compare-bench"
done
for name in bfshandler bfshandler-probe fs-compare-bench; do
    cmp "$root/build/amiga-split-coalesce-attribution-first/$name" \
        "$root/build/amiga-split-coalesce-attribution-second/$name"
done
shasum -a 256 src/core/btree.c src/core/alloc.c src/amiga/perf_probe.h src/amiga/perf_paths.h tools/fs-compare-bench.c
printf 'PASS: two identical diagnostic builds; both normal handlers exactly retained\n'
