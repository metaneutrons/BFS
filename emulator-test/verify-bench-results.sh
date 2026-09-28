#!/usr/bin/env bash
# Accept only a completed, symmetric, data-checked filesystem comparison.
set -euo pipefail

run_dir=${1:?usage: verify-bench-results.sh RUN_DIR}
results="$run_dir/system/Results"
marker="$results/complete.txt"
info="$results/info-after-format.txt"

[[ -f "$marker" ]] && [[ $(tr -d '\r\n' <"$marker") == BFS-PFS3-COMPLETE ]] || {
    printf 'ERROR: guest completion marker is absent or invalid\n' >&2
    exit 1
}
[[ -s "$info" ]] || { printf 'ERROR: post-format volume inventory is missing\n' >&2; exit 1; }
grep -Eq 'DH1.*Read/Write BFSTest' "$info" || {
    printf 'ERROR: BFS volume was not mounted\n' >&2; exit 1;
}
grep -Eq 'DH2.*Read/Write PFSTest' "$info" || {
    printf 'ERROR: PFS3 volume was not mounted\n' >&2; exit 1;
}

for filesystem in bfs pfs3; do
    output="$results/$filesystem.tsv"
    [[ -s "$output" ]] || { printf 'ERROR: missing %s workload output\n' "$filesystem" >&2; exit 1; }
    if [[ "$filesystem" == bfs ]]; then drive=DH1:; else drive=DH2:; fi
    awk -F '\t' -v drive="$drive" '
        BEGIN { split("SMALL_CREATE_40_US LOOKUP_400_US SMALL_READ_40_US SEQ_WRITE_8M_US SEQ_READ_8M_US SMALL_DELETE_40_US", names, " ") }
        NR == 1 { if (NF != 2 || $1 != "FS_COMPARE_BENCH" || $2 != "1") exit 1; next }
        NR == 2 { if (NF != 2 || $1 != "DRIVE" || $2 != drive) exit 1; next }
        NR >= 3 && NR <= 8 { if (NF != 2 || $1 != names[NR-2] || $2 !~ /^[1-9][0-9]*$/) exit 1; next }
        NR == 9 { if (NF != 2 || $1 != "PASS" || $2 != "1") exit 1; next }
        { exit 1 }
        END { if (NR != 9) exit 1 }
    ' "$output" || { printf 'ERROR: invalid or incomplete %s workload output\n' "$filesystem" >&2; exit 1; }
done

printf 'PASS: both mounted volumes have complete, data-checked workload results\n'
