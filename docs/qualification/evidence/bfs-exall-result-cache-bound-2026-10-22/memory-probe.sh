#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Sizes of the bound's cache types and of the candidate's record as the m68k
# compiler lays them out (-O2 -m68040, the release flags). The sizes are read
# from the common symbols of arrays declared with sizeof(type) bytes. m68k aligns
# 32-bit integers to 2 bytes, so these sizes differ from a 4-byte-aligned layout.
# Usage: memory-probe.sh TREE
#   TREE is a checkout of the bound (current main with resultcache-handler.patch applied).
set -euo pipefail
tree=${1:?Usage: memory-probe.sh TREE}
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap '/bin/rm -rf "$work"' EXIT
prefix=$(brew --prefix amiga-gcc)/m68k-amigaos
flags=(-std=c99 -O2 -m68040 -noixemul -fomit-frame-pointer -I"$tree/src/amiga" -I"$tree/include"
       -I"$tree/tests" -DBFS_AMIGA=1 -I"$prefix/ndk-include")
/bin/cp -f "$tree/src/amiga/handler.c" "$work/handler_probe.c"
cat >>"$work/handler_probe.c" <<'PROBE'

const char bound_entry_bytes[sizeof(exall_cache_entry_t)];
const char bound_cache_bytes[sizeof(exall_cache_t)];
const char bound_pending_bytes[sizeof(exall_pending_t)];
const char bound_batch_bytes[sizeof(exall_batch_t)];
PROBE
m68k-amigaos-gcc "${flags[@]}" -c -o "$work/handler_probe.o" "$work/handler_probe.c"
m68k-amigaos-gcc "${flags[@]}" -c -o "$work/candidate.o" "$here/candidate-record.c"
printf 'type\tbytes\n'
# The toolchain's nm lists an uninitialized array as a common symbol whose value is its size.
sizes() {
    m68k-amigaos-nm "$1" | while read -r value kind name; do
        case " $2 " in *" ${name#_} "*) [[ $kind == C ]] && printf '%s\t%d\n' "${name#_}" "$((16#$value))" ;; esac
    done
}
sizes "$work/handler_probe.o" "bound_entry_bytes bound_cache_bytes bound_pending_bytes bound_batch_bytes"
sizes "$work/candidate.o" "candidate_record_bytes"
