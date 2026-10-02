#!/bin/bash
set -euo pipefail
cd /Users/fabian/.codex/worktrees/bfs-append-performance/BFS
archive=/opt/homebrew/opt/amiga-gcc/m68k-amigaos/libnix/lib/libm020/libnix.a
shasum -a 256 "$archive"
for member in memmove bcopy; do
    m68k-amigaos-ar p "$archive" "$member.o" > "build/btree-bulk-slots-libnix-$member.o"
    shasum -a 256 "build/btree-bulk-slots-libnix-$member.o"
    m68k-amigaos-objdump -dr "build/btree-bulk-slots-libnix-$member.o" > "build/btree-bulk-slots-libnix-$member.s"
done
printf 'Selected archive established independently by byte-identical normal-handler link map\n'
