#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
remote=/tmp/bfs-perf-crc.UBHsLz/source-single-block-absence
ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "test ! -e '$remote' && mkdir '$remote'"
rsync -az --exclude=.git --exclude=build --exclude=docs --exclude='*.hdf' ./ "cachy:$remote/"
ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "cd '$remote' && sha256sum src/core/alloc.c tests/test_alloc_metadata_reuse.c && make -j3 conformance-test && python3 tests/fuse/test_fuse_mount.py && python3 tests/fuse/test_fuse_mount.py --format-options 7 --disk-pressure --interrupted-daemon"
