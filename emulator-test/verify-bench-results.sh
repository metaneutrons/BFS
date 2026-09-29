#!/usr/bin/env bash
# Accept only a completed, symmetric, data-checked filesystem comparison.
set -euo pipefail

run_dir=${1:?usage: verify-bench-results.sh RUN_DIR [compare|profile|internal]}
mode=${2:-compare}
case "$mode" in
    compare)
        marker_text=BFS-PFS3-COMPLETE
        suffix=tsv
        header=FS_COMPARE_BENCH
        metrics='SMALL_CREATE_40_US LOOKUP_400_US SMALL_READ_40_US SEQ_WRITE_8M_US SEQ_READ_8M_US SMALL_DELETE_40_US'
        metric_count=6
        value_pattern='^[1-9][0-9]*$'
        filesystems=(bfs pfs3)
        ;;
    profile)
        marker_text=BFS-PFS3-PROFILE-COMPLETE
        suffix=profile.tsv
        header=FS_PROFILE_BENCH
        metrics='FRESH_WRITE_US FRESH_FLUSH_US FRESH_CLOSE_US OVERWRITE_WRITE_US OVERWRITE_FLUSH_US OVERWRITE_CLOSE_US READ_VERIFY_8M_US'
        metric_count=7
        value_pattern='^[0-9]+$'
        filesystems=(bfs pfs3)
        ;;
    internal)
        marker_text=BFS-INTERNAL-COMPLETE
        suffix=internal.tsv
        header=FS_INTERNAL_PROFILE
        metrics='FRESH_WRITE_US FRESH_FLUSH_US FRESH_CLOSE_US FRESH_BIO_READS FRESH_BIO_WRITES FRESH_BIO_UPDATES FRESH_FREESPACE_ALLOCS FRESH_EXTENT_MAPS OVERWRITE_WRITE_US OVERWRITE_FLUSH_US OVERWRITE_CLOSE_US OVERWRITE_BIO_READS OVERWRITE_BIO_WRITES OVERWRITE_BIO_UPDATES OVERWRITE_FREESPACE_ALLOCS OVERWRITE_EXTENT_MAPS READ_VERIFY_8M_US'
        metric_count=17
        value_pattern='^[0-9]+$'
        filesystems=(bfs)
        ;;
    *) printf 'ERROR: mode must be compare, profile, or internal\n' >&2; exit 2 ;;
esac
results="$run_dir/system/Results"
marker="$results/complete.txt"
info="$results/info-after-format.txt"

if [[ ! -f "$marker" ]] || [[ $(tr -d '\r\n' <"$marker") != "$marker_text" ]]; then
    printf 'ERROR: guest completion marker is absent or invalid\n' >&2
    exit 1
fi
[[ -s "$info" ]] || { printf 'ERROR: post-format volume inventory is missing\n' >&2; exit 1; }
grep -Eq 'DH1.*Read/Write BFSTest' "$info" || {
    printf 'ERROR: BFS volume was not mounted\n' >&2; exit 1;
}
grep -Eq 'DH2.*Read/Write PFSTest' "$info" || {
    printf 'ERROR: PFS3 volume was not mounted\n' >&2; exit 1;
}

for filesystem in "${filesystems[@]}"; do
    output="$results/$filesystem.$suffix"
    [[ -s "$output" ]] || { printf 'ERROR: missing %s workload output\n' "$filesystem" >&2; exit 1; }
    if [[ "$filesystem" == bfs ]]; then drive=DH1:; else drive=DH2:; fi
    awk -F '\t' -v drive="$drive" -v header="$header" -v metric_names="$metrics" -v metric_count="$metric_count" -v value_pattern="$value_pattern" '
        BEGIN { split(metric_names, names, " ") }
        NR == 1 { if (NF != 2 || $1 != header || $2 != "1") exit 1; next }
        NR == 2 { if (NF != 2 || $1 != "DRIVE" || $2 != drive) exit 1; next }
        NR >= 3 && NR <= metric_count + 2 { if (NF != 2 || $1 != names[NR-2] || $2 !~ value_pattern) exit 1; next }
        NR == metric_count + 3 { if (NF != 2 || $1 != "PASS" || $2 != "1") exit 1; next }
        { exit 1 }
        END { if (NR != metric_count + 3) exit 1 }
    ' "$output" || { printf 'ERROR: invalid or incomplete %s workload output\n' "$filesystem" >&2; exit 1; }
done

printf 'PASS: requested mounted-volume workload outputs are complete and data-checked\n'
