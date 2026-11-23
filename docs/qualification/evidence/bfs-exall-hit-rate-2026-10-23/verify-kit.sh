#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Boots a copy of the kit with an automatic test and records what the kit shows:
# the BFS volume, a listing of a large directory and the counters written by the
# background loop. It takes no screenshot: a capture of the host screen would show
# whatever else is open there.
# Usage: verify-kit.sh KIT RUNDIR [wb|shell]
# In wb mode the script also reports whether Workbench can start (the kit's own
# check of icon.library and workbench.library, and LoadWB's return code if it ran)
# and exits with 3 if it cannot, so that a broken Workbench is not overlooked.
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
# Runs after the Workbench decision of the kit's startup. In the console (no Workbench)
# a foreground List must work, which hung before the kit had its assigns and the check.
cat >"$run/System/S/Autotest-after" <<'AFTER'
If EXISTS RAM:Console-only
  List BFS:Berichte >SYS:after-list.txt
  Dir BFS:Archiv >SYS:after-dir.txt
Else
  Run >NIL: <NIL: Dir BFS:Archiv >SYS:after-dir.txt
  Wait 10 SEC
EndIf
Echo "done" >SYS:after.done
AFTER
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
while kill -0 "$pid" 2>/dev/null && [[ ! -e $run/System/after.done ]] && (( SECONDS < deadline )); do sleep 1; done
sleep 5
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
[[ -e $run/System/smoke.done ]] || { echo "WARNING: autotest did not finish" >&2; exit 1; }
[[ -e $run/System/after.done ]] || { echo "WARNING: the console did not run the commands after the Workbench decision (it hangs)" >&2; exit 1; }
echo "smoke run complete: $run"
echo "after the Workbench decision: after-dir.txt $(wc -l <"$run/System/after-dir.txt" | tr -d ' ') lines, after-list.txt $(wc -l <"$run/System/after-list.txt" 2>/dev/null | tr -d ' ' || echo 0) lines"
if [[ $mode == wb ]]; then
    if [[ -e $run/System/workbench-check.txt ]] && ! grep -q FAILED "$run/System/workbench-check.txt"; then
        echo "WORKBENCH: available (icon.library and workbench.library open); LoadWB rc: $(cat "$run/System/loadwb-rc.txt" 2>/dev/null || echo 'not recorded')"
    else
        echo "WORKBENCH: NOT AVAILABLE with this System folder:" >&2
        cat "$run/System/workbench-check.txt" >&2 2>/dev/null || true
        exit 3
    fi
fi
