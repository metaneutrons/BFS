#!/bin/bash
set -euo pipefail
cd /tmp/bfs-perf-crc.UBHsLz/source-btree-scratch-lifetime
printf 'Focused MemorySanitizer: all core translation units and public fixture\n'
clang --version
sha256sum src/core/btree.c tests/test_btree_scratch_lifetime.c
mkdir -p build/msan-btree-scratch-lifetime
clang -std=c99 -Wall -Wextra -Werror -g -O1 -pthread \
    -fno-omit-frame-pointer -fsanitize=memory -fsanitize-memory-track-origins=2 \
    -fPIE -pie -Iinclude -Itests -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L \
    tests/test_btree_scratch_lifetime.c src/core/*.c \
    -o build/msan-btree-scratch-lifetime/test_btree_scratch_lifetime
MSAN_OPTIONS=halt_on_error=1:exit_code=86 \
    build/msan-btree-scratch-lifetime/test_btree_scratch_lifetime
printf 'Focused MemorySanitizer execution complete; executed paths only\n'
