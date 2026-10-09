#!/usr/bin/env bash
# Rebuild the standalone timed executable from repository source, without setup.
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir/../../../.."
amiga_prefix=${BFS_AMIGA_PREFIX:-/opt/homebrew/opt/amiga-gcc/m68k-amigaos}
output=build/perf-2026-10-09/sparse-reproduction/crc32-sparse-probe
mkdir -p "$(dirname "$output")"
m68k-amigaos-gcc -std=c99 -Wall -Wextra -Werror -Wno-pointer-sign \
    -O2 -m68020 -noixemul -fomit-frame-pointer -fno-builtin \
    -Isrc/amiga -Iinclude -Itests -DBFS_AMIGA=1 -I"$amiga_prefix/ndk-include" \
    -o "$output" tests/amiga/crc32_sparse_probe.c tests/amiga/crc32_abi_probe.s \
    src/amiga/crc32_68k.s src/core/crc32_zero.c \
    -B"$amiga_prefix/libnix/lib/" -L"$amiga_prefix/libnix/lib" \
    -L"$amiga_prefix/lib" -lamiga -s
if command -v sha256sum >/dev/null; then
    observed=$(sha256sum "$output")
else
    observed=$(shasum -a 256 "$output")
fi
[[ ${observed%% *} == 0b9a67a9554a1a4ef594f0bde18efbe6d2be5a63db662578eff61b58d41266a4 ]] || {
    printf 'ERROR: probe rebuild identity mismatch\n' >&2
    exit 1
}
printf '%s\nM68K_PROBE_REPRODUCED\n' "$observed"
