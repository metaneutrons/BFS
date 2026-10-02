#!/bin/bash
set -euo pipefail
base=/tmp/bfs-perf-crc.UBHsLz
directory=$base/split-coalesce-attribution-probe-pfs3-first
[[ ! -e $directory/bench.fs-uae && ! -e $directory/system/Results/complete.txt && ! -e $directory/runner-output.log ]]
[[ $(sha256sum "$directory/system/L/bfshandler" | awk '{print $1}') == 4c5ae37e9afbc859bbe5c2d33815df50c66a9503d6c22a69575922ffd483d566 ]]
[[ $(sha256sum "$directory/system/C/fs-compare-bench" | awk '{print $1}') == 44581f7f8ce259b34a7521337a45f7f0957662a20a256e623730e3efb512a024 ]]
[[ $(sha256sum "$directory/system/L/pfs3aio" | awk '{print $1}') == bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7 ]]
[[ $(sha256sum /home/fabian/Amiga/kick.a1200.47.102.rom | awk '{print $1}') == 68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c ]]
[[ $(sha256sum "$base/source-split-coalesce-attribution/tools/attribution-consumer/consume.py" | awk '{print $1}') == 704eeec4eb970afaff5b6778accc0ecd02b8b82389321d0398163482bb00f483 ]]
[[ $(systemctl --user is-active coffin.service || true) == inactive ]]
if pgrep -x fs-uae || pgrep -x cc1; then exit 1; fi
run_exit=0
BFS_BENCH_RUN_DIR="$directory" BFS_BENCH_MODE=deep-compare \
    BFS_ROM_FILE=/home/fabian/Amiga/kick.a1200.47.102.rom \
    xvfb-run -a bash "$base/source-split-coalesce-attribution/emulator-test/run-bench.sh" 300 \
    > "$directory/runner-output.log" 2>&1 || run_exit=$?
printf 'Second diagnostic runner exit: %s\n' "$run_exit"
exit "$run_exit"
