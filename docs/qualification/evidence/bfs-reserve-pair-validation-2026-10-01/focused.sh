#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
baseline_sources=$(rg --files src/core -g '*.c' | sed 's@src/core/alloc.c@build/reserve-pair-validation-baseline.c@' | LC_ALL=C sort | tr '\n' ' ')
[[ ! -e build/host-reserve-pair-old/test_alloc_metadata_reuse ]]
[[ ! -e build/host-reserve-pair-focused/test_alloc_metadata_reuse ]]
shasum -a 256 build/reserve-pair-validation-baseline.c src/core/alloc.c tests/test_alloc_metadata_reuse.c > build/reserve-pair-validation-focused-identities.log
make build/host-reserve-pair-old/test_alloc_metadata_reuse BUILD_HOST=build/host-reserve-pair-old CORE_SRC="$baseline_sources" > build/reserve-pair-validation-focused-old-build.log 2>&1
build/host-reserve-pair-old/test_alloc_metadata_reuse > build/reserve-pair-validation-focused-old.log 2>&1
make build/host-reserve-pair-focused/test_alloc_metadata_reuse BUILD_HOST=build/host-reserve-pair-focused > build/reserve-pair-validation-focused-new-build.log 2>&1
build/host-reserve-pair-focused/test_alloc_metadata_reuse > build/reserve-pair-validation-focused-new.log 2>&1
printf 'Both old and new helpers pass the same final focused tests\n'
