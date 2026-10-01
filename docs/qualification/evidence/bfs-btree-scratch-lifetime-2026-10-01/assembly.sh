#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
flags=(-std=c99 -Wall -Wextra -Werror -Wno-pointer-sign -O2 -m68020
       -noixemul -fomit-frame-pointer -Isrc/amiga -I include -I tests
       -DBFS_AMIGA=1 -I/opt/homebrew/opt/amiga-gcc/m68k-amigaos/ndk-include)
m68k-amigaos-gcc "${flags[@]}" -S build/btree-scratch-lifetime-baseline.c -o build/btree-scratch-lifetime-baseline.s
m68k-amigaos-gcc "${flags[@]}" -S src/core/btree.c -o build/btree-scratch-lifetime-candidate.s
shasum -a 256 build/btree-scratch-lifetime-baseline.c src/core/btree.c src/amiga/memcpy_68k.s build/btree-scratch-lifetime-baseline.s build/btree-scratch-lifetime-candidate.s
