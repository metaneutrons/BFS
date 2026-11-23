#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Checks the "own Workbench" route of the kit with a stand-in: a small boot folder
# that plays the part of your own Workbench, an FS-UAE configuration for it, the
# kit's attach-to-config.sh run on that configuration, and a boot in which the
# stand-in lists a directory of BFS: twice and reads the counters through HR:.
# It does not test any real Workbench. No screenshot is taken.
# Usage: verify-attach.sh KIT RUNDIR
set -euo pipefail
kit=${1:?KIT}; run=${2:?RUNDIR}
[[ ! -e $run ]] || { echo "ERROR: $run exists" >&2; exit 2; }
kit=$(cd "$kit" && pwd)
mkdir -p "$run"
run=$(cd "$run" && pwd)
# Copies, so that the kit stays as built (the loop writes Hitrate/hitrate.txt).
/bin/cp -R "$kit" "$run/kit"
/bin/cp -R "$kit/System" "$run/System"
/bin/rm -f "$run/System/S/hitrate-loop" "$run/System/S/Startup-Sequence"
cat >"$run/System/S/Startup-Sequence" <<'SEQ'
MakeDir >NIL: RAM:T RAM:Clipboards RAM:ENV RAM:ENV/Sys
Assign ENV: RAM:ENV
Assign T: RAM:T
Execute S:User-Startup
Dir BFS:Projekte ALL >SYS:list1.txt
Dir BFS:Projekte ALL >SYS:list2.txt
Wait 25 SEC
Type HR:hitrate.txt >SYS:attached-counters.txt
Echo "done" >SYS:attached.done
SEQ
# The line the instructions tell you to add to S:User-Startup.
grep -E '^Run ' "$kit/Startup-Zeilen.txt" >"$run/System/S/User-Startup"
cat >"$run/own.fs-uae" <<CONFIG
[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
kickstart_file = $kit/A1200.47.102.rom
hard_drive_0 = System
hard_drive_0_label = System
floppy_speed = 0
audio_driver = null
window_width = 800
window_height = 600
CONFIG
before=$(shasum -a 256 <"$run/own.fs-uae")
"$run/kit/attach-to-config.sh" "$run/own.fs-uae" | tee "$run/attach.out"
[[ $(shasum -a 256 <"$run/own.fs-uae") == "$before" ]] || { echo "FAIL: the original configuration changed" >&2; exit 1; }
# The copy must be the original plus added lines only.
if /usr/bin/diff "$run/own.fs-uae" "$run/own-hitrate.fs-uae" | grep -q '^<'; then
    echo "FAIL: the copy removes or changes lines of the original" >&2; exit 1
fi
echo "original unchanged; the copy only adds $(/usr/bin/diff "$run/own.fs-uae" "$run/own-hitrate.fs-uae" | grep -c '^>') lines"
FSEMU_AUDIO_DRIVER=null fs-uae "$run/own-hitrate.fs-uae" >"$run/fs-uae.out" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
deadline=$((SECONDS + 240))
while kill -0 "$pid" 2>/dev/null && [[ ! -e $run/System/attached.done ]] && (( SECONDS < deadline )); do sleep 1; done
sleep 3
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
[[ -e $run/System/attached.done ]] || { echo "FAIL: the stand-in did not finish" >&2; exit 1; }
python3 "$kit/summarize.py" "$run/System/attached-counters.txt" >"$run/summary.txt"
exnext=$(awk -F'\t' '$1=="entries.exnext.stamps.kind.new"{n=$2} $1=="entries.exnext.stamps.kind.other_lock"{c=$2} END{print n" "c}' "$run/System/attached-counters.txt")
echo "listed lines: $(wc -l <"$run/System/list1.txt" | tr -d ' ') and $(wc -l <"$run/System/list2.txt" | tr -d ' ')"
echo "ExNext entries class A and class C: $exnext (the second listing goes through another lock, so both must be above 0 and equal)"
set -- $exnext
[[ $1 -gt 0 && $1 -eq $2 ]] || { echo "FAIL: counters do not match two identical listings" >&2; exit 1; }
echo "attach route works: $run"
