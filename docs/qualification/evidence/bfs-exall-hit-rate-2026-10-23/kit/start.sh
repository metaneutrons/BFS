#!/usr/bin/env bash
# Starts the BFS hit-rate session in FS-UAE. The configuration is written anew
# on every start, so the kit may be moved.
set -euo pipefail
kit=$(cd "$(dirname "$0")" && pwd)
# ./start.sh        Workbench with Shell windows
# ./start.sh shell  no Workbench, only the boot console (all listing commands work)
if [[ ${1:-} == shell ]]; then : >"$kit/System/S/Shellmode"; else /bin/rm -f "$kit/System/S/Shellmode"; fi
cat >"$kit/hitrate.fs-uae" <<CONFIG
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
kickstart_file = $kit/A1200.47.102.rom
hard_drive_0 = $kit/System
hard_drive_0_label = System
hard_drive_0_priority = 0
hard_drive_1 = $kit/bfs.hdf
floppy_speed = 0
audio_driver = null
window_width = 1024
window_height = 768
end_config = shutdown
CONFIG
exec fs-uae "$kit/hitrate.fs-uae"
