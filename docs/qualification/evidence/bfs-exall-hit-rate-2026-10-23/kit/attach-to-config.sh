#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Hangs the BFS volume with the instrumented handler and the Hitrate folder onto a
# copy of an existing FS-UAE configuration, so that the hit-rate session runs on
# your own Workbench. The original configuration is not changed: the script reads
# it and writes a new file next to it.
#
# Usage: ./attach-to-config.sh CONFIG [OUTPUT]
#   CONFIG  your FS-UAE configuration (.fs-uae) with a working Workbench
#   OUTPUT  the new configuration (default: CONFIG without .fs-uae plus -hitrate.fs-uae,
#           in the same folder, so that relative paths in it keep working)
set -euo pipefail
config=${1:?Usage: attach-to-config.sh CONFIG [OUTPUT]}
kit=$(cd "$(dirname "$0")" && pwd)
output=${2:-${config%.fs-uae}-hitrate.fs-uae}
[[ -f $config ]] || { echo "ERROR: $config not found" >&2; exit 2; }
[[ $output != "$config" && ! -e $output ]] || { echo "ERROR: $output exists, nothing written" >&2; exit 2; }
[[ -f $kit/bfs.hdf && -d $kit/Hitrate ]] || { echo "ERROR: bfs.hdf or Hitrate not in $kit" >&2; exit 2; }

# The first two hard drive numbers that the configuration does not use.
used=" $(grep -Eo '^[[:space:]]*hard_drive_[0-9]+' "$config" | grep -Eo '[0-9]+$' | sort -un | tr '\n' ' ')"
free=()
for n in 0 1 2 3 4 5 6 7 8 9; do
    [[ $used == *" $n "* ]] || free+=("$n")
done
(( ${#free[@]} >= 2 )) || { echo "ERROR: fewer than two free hard drive numbers in $config" >&2; exit 2; }
hdf=${free[0]}; folder=${free[1]}

if grep -Eq '^\[' "$config" && [[ $(grep -Ec '^\[' "$config") -gt 1 ]]; then
    echo "WARNING: $config has more than one [section]; the new lines are appended to the last one." >&2
fi
cpu=$(grep -E '^[[:space:]]*cpu[[:space:]]*=' "$config" | tail -n 1 | sed 's/.*=[[:space:]]*//' || true)
case ${cpu:-} in
    68040|68060) ;;
    *) echo "WARNING: the instrumented handler is built for the 68040; your configuration has cpu = ${cpu:-(not set, depends on amiga_model)}." >&2
       echo "         Set cpu = 68040 in $output if BFS does not mount." >&2 ;;
esac

/bin/cp "$config" "$output"
added=$(
    printf '# Hit-rate session: BFS volume with the instrumented handler and the Hitrate folder\n'
    printf 'hard_drive_%s = %s/bfs.hdf\n' "$hdf" "$kit"
    printf 'hard_drive_%s = %s/Hitrate\n' "$folder" "$kit"
    printf 'hard_drive_%s_label = HR\n' "$folder"
)
printf '\n%s\n' "$added" >>"$output"
echo "Wrote the copy $output"
echo "Added to the copy (nothing else changed, $config is untouched):"
printf '%s\n' "$added" | sed 's/^/  /'
echo "Start it with: fs-uae \"$output\""
echo "Then add the line of Startup-Zeilen.txt to the S:User-Startup of your Workbench."
