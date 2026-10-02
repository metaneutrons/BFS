#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
flags=(-std=c99 -Wall -Wextra -Werror -Wno-pointer-sign -O2 -m68020
       -noixemul -fomit-frame-pointer -Isrc/amiga -I include -I tests
       -DBFS_AMIGA=1 -I/opt/homebrew/opt/amiga-gcc/m68k-amigaos/ndk-include)
m68k-amigaos-gcc "${flags[@]}" -S build/btree-bulk-slots-baseline.c -o build/btree-bulk-slots-baseline.s
m68k-amigaos-gcc "${flags[@]}" -S src/core/btree.c -o build/btree-bulk-slots-candidate.s
shasum -a 256 build/btree-bulk-slots-baseline.c src/core/btree.c src/amiga/string.h build/btree-bulk-slots-baseline.s build/btree-bulk-slots-candidate.s
