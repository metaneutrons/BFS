#!/bin/bash
set -euo pipefail
label=${1:?unique copy image label}
limit=${2:-60}
[[ $limit =~ ^[0-9]+$ && $limit -ge 1 && $limit -le 300 ]]
[[ $label =~ ^memory-copy-48-run\.[A-Za-z0-9]{6}$ ]]
run_dir=/tmp/bfs-perf-crc.UBHsLz/$label
[[ -f $run_dir/memory-copy.fs-uae && ! -e $run_dir/fs-uae.log ]]
[[ ! -e $run_dir/system/Results/memory-copy-compare.done ]]
[[ $(systemctl --user is-active coffin.service || true) == inactive ]]
if ps -eo comm= | awk '$1 == "fs-uae" || $1 == "cc1" {found=1; print} END {exit !found}'; then exit 1; fi
[[ $(sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom | awk '{print $1}') == 68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c ]]
pid=
# Called by the EXIT trap.
# shellcheck disable=SC2329
cleanup() {
    if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT
FSEMU_AUDIO_DRIVER=null fs-uae "$run_dir/memory-copy.fs-uae" > "$run_dir/fs-uae.log" 2>&1 &
pid=$!
deadline=$((SECONDS + limit))
while ((SECONDS < deadline)); do
    if [[ -f $run_dir/system/Results/memory-copy-compare.done && -f $run_dir/system/Results/memory-copy-compare.txt ]]; then
        cat "$run_dir/system/Results/memory-copy-compare.txt"
        grep -qx 'MEMORY-COPY-COMPARE-PASS' "$run_dir/system/Results/memory-copy-compare.done"
        grep -qx 'MEMORY-COPY-COMPARE-PASS' "$run_dir/system/Results/memory-copy-compare.txt"
        printf 'Memory copy guest completed within the bounded run\n'
        exit 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 1
done
printf 'Memory copy guest failed or did not finish within%ss\n' "$limit" >&2
tail -n 40 "$run_dir/fs-uae.log" >&2
exit 1
