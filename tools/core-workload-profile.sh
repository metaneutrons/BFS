#!/usr/bin/env bash
# Deterministic per-phase instruction counts for the host compare-workload replica.
# Usage: tools/core-workload-profile.sh [sync|group] [top-functions]
set -euo pipefail

mode=${1:-sync}
top=${2:-0}
case "$mode" in
    sync|group) ;;
    *) printf 'ERROR: mode must be sync or group\n' >&2; exit 2 ;;
esac
[[ "$top" =~ ^[0-9]+$ ]] || { printf 'ERROR: top-functions must be a number\n' >&2; exit 2; }
command -v valgrind >/dev/null || { printf 'ERROR: valgrind is required\n' >&2; exit 1; }

root=$(cd "$(dirname "$0")/.." && pwd)
binary="$root/build/host/core-workload"
[[ -x "$binary" ]] || { printf 'ERROR: run make build/host/core-workload first\n' >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# A fixed relative image path keeps the instruction count independent of the
# temporary directory name.
(cd "$work" && valgrind --tool=callgrind --callgrind-out-file=cg.out \
    "$binary" core-workload.img "$mode") >"$work/run.log" 2>"$work/valgrind.log" || {
    cat "$work/run.log" "$work/valgrind.log" >&2
    exit 1
}
grep -q "^PASS" "$work/run.log" || { cat "$work/run.log" >&2; exit 1; }

printf 'CORE_WORKLOAD_PROFILE\t1\nMODE\t%s\n' "$mode"
# Counts are only comparable between binaries built by the same compiler.
grep '^COMPILER' "$work/run.log"
for dump in "$work"/cg.out.*; do
    phase=$(sed -n 's/^desc: Trigger: Client Request: //p' "$dump")
    instructions=$(sed -n 's/^summary: //p' "$dump")
    [[ -n "$phase" && -n "$instructions" ]] || { printf 'ERROR: incomplete dump %s\n' "$dump" >&2; exit 1; }
    printf '%s_IR\t%s\n' "$phase" "$instructions"
    if (( top > 0 )) && command -v callgrind_annotate >/dev/null; then
        # awk reads all input: exiting early would fail the pipeline.
        callgrind_annotate --threshold=100 --inclusive=no "$dump" 2>/dev/null |
            awk -v phase="$phase" -v top="$top" -v root="$root/" '
                /^ *[0-9,]+ \(/ && !/PROGRAM TOTALS|libc_start|below main|0x[0-9a-f]+/ {
                    if (shown >= top) next
                    count = $1; gsub(",", "", count)
                    name = $0; sub(/^[^)]*\) +/, "", name); sub(/ \[.*/, "", name)
                    if (index(name, root) == 1) name = substr(name, length(root) + 1)
                    printf "%s_TOP\t%s\t%s\n", phase, count, name
                    shown++
                }'
    fi
done
printf 'PASS\t1\n'
