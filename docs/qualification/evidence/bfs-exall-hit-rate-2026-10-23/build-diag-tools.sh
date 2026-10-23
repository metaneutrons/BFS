#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build the small Shell programs that check and diagnose Workbench in the kit:
# openlibs (opens libraries, return code 5 if one fails), quiet (runs a command
# without requesters and records its return code), taskdump and inventory
# (what the ROM and the running system hold). Same compiler and flags as the
# project's Amiga tools (Makefile, AMIGA_TOOL_FLAGS).
# Usage: build-diag-tools.sh OUTDIR
set -euo pipefail
out=${1:?Usage: build-diag-tools.sh OUTDIR}
here=$(cd "$(dirname "$0")" && pwd)
prefix=$(brew --prefix amiga-gcc 2>/dev/null || echo /opt/homebrew/opt/amiga-gcc)/m68k-amigaos
mkdir -p "$out"
for tool in openlibs quiet taskdump inventory; do
    m68k-amigaos-gcc -std=c99 -Wall -Wextra -Wno-pointer-sign -Os -m68020 -noixemul \
        -I"$prefix/ndk-include" -B"$prefix/libnix/lib/" -L"$prefix/libnix/lib" -L"$prefix/lib" \
        -o "$out/$tool" "$here/tools/$tool.c" -lamiga -s
done
ls -l "$out"
