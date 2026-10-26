#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build the usage kit for the BFS hit-rate session on the Mac: a boot folder with
# Workbench commands, the instrumented handler inside a hard file whose BFS volume
# holds a populated tree, an FS-UAE start script, the German instructions and the
# summary script. The kit contains licensed files (Kickstart ROM, Workbench
# commands) and is built below build/, which git ignores.
#
# Usage: build-kit.sh HANDLER TOOLDIR OUTDIR
#   HANDLER  the instrumented 68040 bfshandler (build-instrumented.sh 040 ...)
#   TOOLDIR  directory with the m68k programs hitrate and hitrate-check
#   OUTDIR   new kit directory
# Environment: BFS_AMIGA_ASSETS_DIR (required), BFS_HOST_BFS (default build/host/bfs).
set -euo pipefail
handler=${1:?HANDLER}; tools=${2:?TOOLDIR}; kit=${3:?OUTDIR}
assets=${BFS_AMIGA_ASSETS_DIR:?BFS_AMIGA_ASSETS_DIR}
rom=${BFS_ROM_FILE:-$assets/A1200.47.102.rom}
here=$(cd "$(dirname "$0")" && pwd)
[[ ! -e $kit ]] || { echo "ERROR: $kit exists" >&2; exit 2; }
mkdir -p "$kit"
kit=$(cd "$kit" && pwd)

echo "== tree"
python3 "$here/make-tree.py" "$kit/.tree" --assets "$assets"

echo "== populate the BFS volume (an emulator window opens and closes by itself)"
BFS_TREE="$kit/.tree" "$here/run-guest.sh" "$kit/.populate" "$handler" "$tools" \
    "$here/kit/populate-Startup-Sequence" populate.done 1500
cat "$kit/.populate/system/populate.txt"

echo "== assemble the kit"
mkdir -p "$kit/System/C" "$kit/System/L" "$kit/System/Libs" "$kit/System/S" "$kit/System/Devs"
/bin/cp -R "$assets/C/." "$kit/System/C/"
[[ -d $assets/L ]] && /bin/cp -R "$assets/L/." "$kit/System/L/"
/bin/cp -R "$assets/Libs/." "$kit/System/Libs/"
/bin/cp -f "$tools/hitrate" "$tools/hitrate-check" "$tools/openlibs" "$kit/System/C/"
/bin/cp -f "$here/kit/Startup-Sequence" "$kit/System/S/Startup-Sequence"
/bin/cp -f "$here/kit/hitrate-loop" "$kit/System/S/hitrate-loop"
/bin/cp -f "$kit/.populate/bfs.hdf" "$kit/bfs.hdf"
/bin/cp -f "$rom" "$kit/A1200.47.102.rom"
/bin/cp -f "$here/kit/start.sh" "$kit/start.sh"
/bin/cp -f "$here/kit/ANLEITUNG.md" "$kit/ANLEITUNG.md"
/bin/cp -f "$here/summarize.py" "$kit/summarize.py"
/bin/cp -f "$handler" "$kit/bfshandler.hitrate"
mkdir -p "$kit/Hitrate"
/bin/cp -f "$tools/hitrate" "$kit/Hitrate/hitrate"
/bin/cp -f "$here/kit/hitrate-loop-attached" "$kit/Hitrate/hitrate-loop"
/bin/cp -f "$here/kit/Startup-Zeilen.txt" "$kit/Startup-Zeilen.txt"
/bin/cp -f "$here/kit/attach-to-config.sh" "$kit/attach-to-config.sh"
/bin/rm -rf "$kit/.populate" "$kit/.tree"
echo "Kit: $kit"
du -sh "$kit" | cut -f1
