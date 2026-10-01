#!/bin/bash
set -euo pipefail
root=/Users/fabian/.codex/worktrees/bfs-append-performance/BFS
source_root="$root/build/split-coalesce-attribution-source"
remote=/tmp/bfs-perf-crc.UBHsLz/source-split-coalesce-attribution
cd "$source_root"
ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "test ! -e '$remote' && mkdir '$remote'"
rsync -az --exclude=.git --exclude=build --exclude=docs --exclude='*.hdf' ./ "cachy:$remote/"
ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "cd '$remote' && sha256sum src/core/btree.c src/core/alloc.c src/amiga/perf_probe.h src/amiga/perf_paths.h tools/fs-compare-bench.c && make -j3 conformance-test && python3 tests/fuse/test_fuse_mount.py && python3 tests/fuse/test_fuse_mount.py --format-options 7 --disk-pressure --interrupted-daemon"
