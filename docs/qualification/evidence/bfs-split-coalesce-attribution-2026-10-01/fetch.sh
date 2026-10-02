#!/bin/bash
set -euo pipefail
order=${1:?bfs-first or pfs3-first required}
[[ $order == bfs-first || $order == pfs3-first ]]
root=/Users/fabian/.codex/worktrees/bfs-append-performance/BFS
cd "$root"
label=split-coalesce-attribution-probe-$order
remote=/tmp/bfs-perf-crc.UBHsLz/$label
archive="$remote/text-evidence.tar.gz"
ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "test -f '$remote/system/Results/complete.txt' && test ! -e '$archive' && if test -f '$remote/runner-output.log'; then tar -czf '$archive' -C '$remote' bench.fs-uae fs-uae.log system/Results runner-output.log; else tar -czf '$archive' -C '$remote' bench.fs-uae fs-uae.log system/Results; fi"
identity=$(ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "sha256sum '$archive'")
expected=${identity%% *}
[[ $expected =~ ^[0-9a-f]{64}$ ]]
size=$(ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "stat -c %s '$archive'")
[[ $size =~ ^[1-9][0-9]*$ && $size -lt 1048576 ]]
chunks=$(((size + 511) / 512))
printf 'Bundle %s: %s bytes, %s\n' "$label" "$size" "$expected"
ssh -o BatchMode=yes -o ConnectTimeout=8 cachy "for task_chunk in \$(seq 0 $((chunks - 1))); do dd if='$archive' bs=512 skip=\"\$task_chunk\" count=1 status=none; sleep 0.05; done" > "build/$label-text-evidence.tar.gz"
actual=$(shasum -a 256 "build/$label-text-evidence.tar.gz" | awk '{print $1}')
[[ $actual == "$expected" ]]
tar -tzf "build/$label-text-evidence.tar.gz" | awk '
  $0 == "bench.fs-uae" || $0 == "fs-uae.log" || $0 == "runner-output.log" || $0 == "system/Results/" {next}
  $0 ~ /^system\/Results\/[a-z0-9.-]+$/ {next}
  {bad=1; print "Unexpected bundle member: " $0 > "/dev/stderr"}
  END {exit bad}'
tar -xzf "build/$label-text-evidence.tar.gz" -C "build/benchmark/$label"
python3 build/split-coalesce-attribution-consumer/consume.py "build/benchmark/$label" --header build/split-coalesce-attribution-source/src/amiga/perf_probe.h --verifier emulator-test/verify-bench-results.sh > "build/$label-consumed.json"
printf 'PASS: exact bundle hash and strict schema-12 verification for %s\n' "$label"
