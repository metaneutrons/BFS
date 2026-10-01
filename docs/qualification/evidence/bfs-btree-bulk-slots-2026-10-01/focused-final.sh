#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
baseline_sources=$(rg --files src/core -g '*.c' | sed 's@src/core/btree.c@build/btree-bulk-slots-baseline.c@' | LC_ALL=C sort | tr '\n' ' ')
[[ ! -e build/host-btree-bulk-slots-old-final/test_btree_bulk_slots ]]
[[ ! -e build/host-btree-bulk-slots-focused-final/test_btree_bulk_slots ]]
shasum -a 256 build/btree-bulk-slots-baseline.c src/core/btree.c src/amiga/string.h tests/test_btree_bulk_slots.c > build/btree-bulk-slots-focused-final-identities.log
make build/host-btree-bulk-slots-old-final/test_btree_bulk_slots BUILD_HOST=build/host-btree-bulk-slots-old-final CORE_SRC="$baseline_sources" > build/btree-bulk-slots-focused-final-old-build.log 2>&1
build/host-btree-bulk-slots-old-final/test_btree_bulk_slots > build/btree-bulk-slots-focused-final-old.log 2>&1
make build/host-btree-bulk-slots-focused-final/test_btree_bulk_slots BUILD_HOST=build/host-btree-bulk-slots-focused-final > build/btree-bulk-slots-focused-final-new-build.log 2>&1
build/host-btree-bulk-slots-focused-final/test_btree_bulk_slots > build/btree-bulk-slots-focused-final-new.log 2>&1
printf 'Both old and new sources pass the same final focused tests\n'
