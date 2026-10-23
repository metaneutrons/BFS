#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Sizes of the hit-rate counter's types as the m68k compiler lays them out
# (-O2 -m68040, -DBFS_HITRATE=1), read from the common symbols of arrays declared
# with sizeof(type) bytes. Usage: memory-probe.sh TREE (a checkout with
# hitrate-handler.patch applied).
set -euo pipefail
tree=${1:?Usage: memory-probe.sh TREE}
work=$(mktemp -d)
trap '/bin/rm -rf "$work"' EXIT
prefix=$(brew --prefix amiga-gcc)/m68k-amigaos
flags=(-std=c99 -O2 -m68040 -DBFS_HITRATE=1 -noixemul -fomit-frame-pointer -I"$tree/src/amiga"
       -I"$tree/include" -I"$tree/tests" -DBFS_AMIGA=1 -I"$prefix/ndk-include")
/bin/cp -f "$tree/src/amiga/handler.c" "$work/handler_probe.c"
cat >>"$work/handler_probe.c" <<'PROBE'

const char hr_state_bytes[sizeof(struct hr_state)];
const char hr_dir_bytes[sizeof(hr_dir_t)];
const char hr_rec_bytes[sizeof(hr_rec_t)];
const char hr_lock_pass_bytes[sizeof(hr_lock_pass_t)];
const char hr_lock_bytes[sizeof(bfs_lock_t)];
const char hr_report_bytes[sizeof(bfs_hitrate_report_t)];
PROBE
m68k-amigaos-gcc "${flags[@]}" -c -o "$work/handler_probe.o" "$work/handler_probe.c"
printf 'type\tbytes\n'
m68k-amigaos-nm "$work/handler_probe.o" | while read -r value kind name; do
    case "${name#_}" in hr_*_bytes) [[ $kind == C ]] && printf '%s\t%d\n' "${name#_}" "$((16#$value))" ;; esac
done
