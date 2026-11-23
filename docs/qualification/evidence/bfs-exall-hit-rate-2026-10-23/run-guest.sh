#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Run a startup sequence in FS-UAE (A1200, 68040, the Kickstart of the assets)
# with a BFS volume "BFS:" served by the given handler, and wait for a marker.
# Adapted from emulator-test/ci-local.sh. The window opens on the host.
#
# Usage: run-guest.sh RUNDIR HANDLER TOOLDIR STARTUP DONEFILE TIMEOUT
#   RUNDIR    new empty directory for the run
#   HANDLER   the (instrumented) bfshandler
#   TOOLDIR   m68k programs copied into SYS:C (may be empty)
#   STARTUP   the S/Startup-Sequence, run from the boot drive
#   DONEFILE  file below RUNDIR/system that the startup sequence creates last
#   TIMEOUT   seconds
# Environment: BFS_AMIGA_ASSETS_DIR (Kickstart ROM and C, L, Libs), BFS_HOST_BFS
# (host formatter, default build/host/bfs), BFS_TREE (optional directory shown as
# SRC: for startup sequences that copy a tree into BFS:).
set -euo pipefail
run=${1:?RUNDIR}; handler=${2:?HANDLER}; tools=${3:?TOOLDIR}; startup=${4:?STARTUP}
done_file=${5:?DONEFILE}; timeout=${6:?TIMEOUT}
assets=${BFS_AMIGA_ASSETS_DIR:?BFS_AMIGA_ASSETS_DIR}
rom=${BFS_ROM_FILE:-$assets/A1200.47.102.rom}
host_bfs=${BFS_HOST_BFS:-build/host/bfs}
[[ ! -e $run ]] || { echo "ERROR: $run exists" >&2; exit 2; }
for f in "$rom" "$handler" "$startup" "$host_bfs"; do [[ -e $f ]] || { echo "ERROR: missing $f" >&2; exit 2; }; done
mkdir -p "$run/system/C" "$run/system/L" "$run/system/Libs" "$run/system/S" "$run/system/Devs"
/bin/cp -R "$assets/C/." "$run/system/C/"
[[ -d $assets/L ]] && /bin/cp -R "$assets/L/." "$run/system/L/"
/bin/cp -R "$assets/Libs/." "$run/system/Libs/"
if [[ -d $tools ]]; then /bin/cp -f "$tools"/* "$run/system/C/" 2>/dev/null || true; fi
/bin/cp -f "$startup" "$run/system/S/Startup-Sequence"
hdf=$run/bfs.hdf
rdbtool -f "$hdf" create size=128Mi cyls=256 heads=16 secs=32 \
    + init \
    + add name=BFS start=2 end=255 dostype=0x42465300 bootable=False \
    + fsadd "$handler" version=1.0 dostype=0x42465300 >/dev/null
part_blocks=$(( (254 * 16 * 32 * 512) / 4096 ))
part=$run/partition.img
dd if=/dev/zero of="$part" bs=4096 count="$part_blocks" status=none
"$host_bfs" format "$part" --label BFS >/dev/null
dd if="$part" of="$hdf" bs=512 seek=1024 conv=notrunc status=none
/bin/rm -f "$part"
cfg=$run/run.fs-uae
{
    echo "[fs-uae]"
    echo "amiga_model = A1200"
    echo "chip_memory = 2048"
    echo "fast_memory = 8192"
    echo "cpu = 68040"
    echo "uae_cpu_speed = max"
    echo "uae_cpu_24bit_addressing = false"
    echo "kickstart_file = $rom"
    echo "hard_drive_0 = $run/system"
    echo "hard_drive_0_label = System"
    echo "hard_drive_0_priority = 0"
    echo "hard_drive_1 = $hdf"
    [[ -n ${BFS_TREE:-} ]] && { echo "hard_drive_2 = $BFS_TREE"; echo "hard_drive_2_label = SRC"; echo "hard_drive_2_read_only = 1"; }
    echo "floppy_speed = 0"
    echo "audio_driver = null"
    echo "window_width = 800"
    echo "window_height = 600"
    echo "end_config = shutdown"
} > "$cfg"
FSEMU_AUDIO_DRIVER=null fs-uae "$cfg" >"$run/fs-uae.out" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
deadline=$((SECONDS + timeout))
while kill -0 "$pid" 2>/dev/null; do
    if [[ -e $run/system/$done_file ]] || (( SECONDS >= deadline )); then kill "$pid" 2>/dev/null || true; break; fi
    sleep 1
done
wait "$pid" 2>/dev/null || true
if [[ -e $run/system/$done_file ]]; then echo "run complete: $run"; else echo "WARNING: no $done_file within ${timeout}s" >&2; exit 1; fi
