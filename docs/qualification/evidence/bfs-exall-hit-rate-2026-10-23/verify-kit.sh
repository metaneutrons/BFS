#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Boots a copy of the kit with an automatic test and records what the kit shows:
# the BFS volume, a listing of a large directory and the counters written by the
# background loop. It takes no screenshot: a capture of the host screen would show
# whatever else is open there.
# Usage: verify-kit.sh KIT RUNDIR [wb|shell]
set -euo pipefail
kit=${1:?KIT}; run=${2:?RUNDIR}; mode=${3:-wb}
[[ ! -e $run ]] || { echo "ERROR: $run exists" >&2; exit 2; }
mkdir -p "$run"
/bin/cp -R "$kit/System" "$run/System"
/bin/cp -f "$kit/bfs.hdf" "$run/bfs.hdf"
cat >"$run/System/S/Autotest" <<'AUTO'
Echo "autotest start" >SYS:smoke.txt
Info BFS: >>SYS:smoke.txt
Dir BFS: >>SYS:smoke.txt
Dir BFS:Archiv >SYS:smoke-archiv.txt
Dir BFS:Projekte ALL >SYS:smoke-projekte.txt
Dir BFS:Berichte >SYS:smoke-berichte.txt
Run >NIL: <NIL: Execute S:Autotest2
Wait 28 SEC
Which hitrate >>SYS:smoke.txt
Type SYS:hitrate.txt >>SYS:smoke.txt
Echo "autotest done" >>SYS:smoke.txt
Echo "done" >SYS:smoke.done
AUTO
cat >"$run/System/S/Autotest2" <<'AUTO2'
; Workbench mode: a listing started after Workbench is up. Shell mode: a foreground
; List in the boot console, which hangs once Workbench runs.
If EXISTS S:Shellmode
  List BFS:Berichte >SYS:smoke-list.txt
Else
  Wait 20 SEC
  Run >NIL: <NIL: Dir BFS:Berichte ALL >SYS:smoke-late-dir.txt
EndIf
Echo "late part done" >SYS:smoke.late
AUTO2
[[ $mode == shell ]] && : >"$run/System/S/Shellmode"
cat >"$run/run.fs-uae" <<CONFIG
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
kickstart_file = $kit/A1200.47.102.rom
hard_drive_0 = $run/System
hard_drive_0_label = System
hard_drive_0_priority = 0
hard_drive_1 = $run/bfs.hdf
floppy_speed = 0
audio_driver = null
window_width = 1024
window_height = 768
end_config = shutdown
CONFIG
FSEMU_AUDIO_DRIVER=null fs-uae "$run/run.fs-uae" >"$run/fs-uae.out" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
deadline=$((SECONDS + 240))
while kill -0 "$pid" 2>/dev/null && [[ ! -e $run/System/smoke.done ]] && (( SECONDS < deadline )); do sleep 1; done
sleep 30
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
[[ -e $run/System/smoke.done ]] && echo "smoke run complete: $run" || { echo "WARNING: autotest did not finish" >&2; exit 1; }
