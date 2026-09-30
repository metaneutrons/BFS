#!/usr/bin/env bash
# Accept only a completed, symmetric, data-checked filesystem comparison.
set -euo pipefail

run_dir=${1:?usage: verify-bench-results.sh RUN_DIR [compare|profile|internal|deep|deep-compare]}
mode=${2:-compare}
header_version=1
fixed_metric_name=
fixed_metric_value=
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
    deep)
        marker_text=BFS-PFS3-DEEP-COMPLETE
        suffix=deep.tsv
        header=FS_DEEP_PROFILE
        deep_bfs_metrics='FRESH_WRITE_US FRESH_FLUSH_US FRESH_CLOSE_US FRESH_BIO_READS FRESH_BIO_WRITES FRESH_BIO_UPDATES FRESH_DATA_READS FRESH_DATA_WRITES FRESH_NODE_WRITES FRESH_TXN_COMMITS FRESH_FREESPACE_ALLOCS FRESH_EXTENT_MAPS FRESH_READ_TICKS FRESH_WRITE_TICKS FRESH_UPDATE_TICKS FRESH_DATA_READ_TICKS FRESH_DATA_WRITE_TICKS OVERWRITE_WRITE_US OVERWRITE_FLUSH_US OVERWRITE_CLOSE_US OVERWRITE_BIO_READS OVERWRITE_BIO_WRITES OVERWRITE_BIO_UPDATES OVERWRITE_DATA_READS OVERWRITE_DATA_WRITES OVERWRITE_NODE_WRITES OVERWRITE_TXN_COMMITS OVERWRITE_FREESPACE_ALLOCS OVERWRITE_EXTENT_MAPS OVERWRITE_READ_TICKS OVERWRITE_WRITE_TICKS OVERWRITE_UPDATE_TICKS OVERWRITE_DATA_READ_TICKS OVERWRITE_DATA_WRITE_TICKS READ_VERIFY_8M_US READ_BIO_READS READ_BIO_WRITES READ_BIO_UPDATES READ_DATA_READS READ_DATA_WRITES READ_NODE_WRITES READ_TXN_COMMITS READ_FREESPACE_ALLOCS READ_EXTENT_MAPS READ_READ_TICKS READ_WRITE_TICKS READ_UPDATE_TICKS READ_DATA_READ_TICKS READ_DATA_WRITE_TICKS CLOCK_HZ'
        deep_pfs3_metrics='FRESH_WRITE_US FRESH_FLUSH_US FRESH_CLOSE_US OVERWRITE_WRITE_US OVERWRITE_FLUSH_US OVERWRITE_CLOSE_US READ_VERIFY_8M_US'
        value_pattern='^[0-9]+$'
        positive_metric_names=CLOCK_HZ
        filesystems=(bfs pfs3)
        ;;
    deep-compare)
        marker_text=BFS-PFS3-DEEP-COMPARE-COMPLETE
        suffix=deep-compare.tsv
        header=FS_DEEP_COMPARE
        value_pattern='^[0-9]+$'
        deep_compare_phases=(SMALL_CREATE_40 LOOKUP_400 SMALL_READ_40 SEQ_WRITE_8M SEQ_READ_8M SMALL_DELETE_40)
        header_version=0
        fixed_metric_name=CRC_SAMPLE_STRIDE
        fixed_metric_value=64
        deep_compare_counters_v4=(BIO_READS BIO_WRITES BIO_UPDATES DATA_READS DATA_WRITES NODE_WRITES TXN_COMMITS FREESPACE_ALLOCS EXTENT_MAPS READ_TICKS WRITE_TICKS UPDATE_TICKS DATA_READ_TICKS DATA_WRITE_TICKS FREE_TREE_NODE_WRITES DIR_TREE_NODE_WRITES INODE_TREE_NODE_WRITES REFCOUNT_TREE_NODE_WRITES OTHER_TREE_NODE_WRITES NODE_CRC_READ_CALLS NODE_CRC_READ_SAMPLES NODE_CRC_READ_SAMPLE_TICKS NODE_CRC_WRITE_CALLS NODE_CRC_WRITE_SAMPLES NODE_CRC_WRITE_SAMPLE_TICKS CLOCK_PAIR_TICKS)
        deep_compare_counters_v5=(BIO_READS BIO_WRITES BIO_UPDATES DATA_READS DATA_WRITES NODE_WRITES TXN_COMMITS FREESPACE_ALLOCS EXTENT_MAPS READ_TICKS WRITE_TICKS UPDATE_TICKS DATA_READ_TICKS DATA_WRITE_TICKS FREE_TREE_NODE_WRITES FREE_TREE_ALLOCATION_BODY_NODE_WRITES FREE_TREE_RESERVE_REFILL_NODE_WRITES FREE_TREE_RESERVE_RETURN_NODE_WRITES FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES FREE_TREE_OTHER_NODE_WRITES DIR_TREE_NODE_WRITES INODE_TREE_NODE_WRITES REFCOUNT_TREE_NODE_WRITES OTHER_TREE_NODE_WRITES NODE_CRC_READ_CALLS NODE_CRC_READ_SAMPLES NODE_CRC_READ_SAMPLE_TICKS NODE_CRC_WRITE_CALLS NODE_CRC_WRITE_SAMPLES NODE_CRC_WRITE_SAMPLE_TICKS CLOCK_PAIR_TICKS)
        deep_compare_counters_v6=("${deep_compare_counters_v5[@]}" FREE_TREE_RESERVE_RETURN_CALLS FREE_TREE_RESERVE_RETURN_RUNS FREE_TREE_RESERVE_RETURN_BLOCKS FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN POST_PUBLISH_RECLAIM_PASSES MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT SUPERBLOCK_PUBLICATIONS)
        deep_compare_counters_v7=("${deep_compare_counters_v6[@]}" FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES FREE_TREE_RESERVE_RETURN_BATCH_CALLS FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES)
        deep_compare_counters_v8=("${deep_compare_counters_v7[@]}" FREE_TREE_RESERVE_RETURN_SKIP_SHAPE FREE_TREE_RESERVE_RETURN_SKIP_SMALL FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY)
        deep_compare_bfs_metrics=
        deep_compare_pfs3_metrics=
        positive_metric_names=
        filesystems=(bfs pfs3)
        ;;
    *) printf 'ERROR: mode must be compare, profile, internal, deep, or deep-compare\n' >&2; exit 2 ;;
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
    if [[ "$mode" == deep ]]; then
        if [[ "$filesystem" == bfs ]]; then metrics=$deep_bfs_metrics; else metrics=$deep_pfs3_metrics; fi
        read -r -a metric_names_array <<<"$metrics"
        metric_count=${#metric_names_array[@]}
    elif [[ "$mode" == deep-compare ]]; then
        IFS=$'\t' read -r observed_header observed_version < "$output"
        if [[ "$observed_header" != "$header" ]]; then
            printf 'ERROR: invalid deep-compare schema header in %s output\n' "$filesystem" >&2
            exit 1
        fi
        if [[ "$filesystem" == bfs ]]; then
            if [[ "$observed_version" != 4 && "$observed_version" != 5 && "$observed_version" != 6 && "$observed_version" != 7 && "$observed_version" != 8 ]]; then
                printf 'ERROR: unsupported deep-compare schema version %s\n' "$observed_version" >&2
                exit 1
            fi
            header_version=$observed_version
            if [[ "$header_version" == 4 ]]; then
                deep_compare_counters=("${deep_compare_counters_v4[@]}")
            elif [[ "$header_version" == 5 ]]; then
                deep_compare_counters=("${deep_compare_counters_v5[@]}")
            elif [[ "$header_version" == 6 ]]; then
                deep_compare_counters=("${deep_compare_counters_v6[@]}")
            elif [[ "$header_version" == 7 ]]; then
                deep_compare_counters=("${deep_compare_counters_v7[@]}")
            else
                deep_compare_counters=("${deep_compare_counters_v8[@]}")
            fi
            deep_compare_bfs_metrics=
            for deep_phase in "${deep_compare_phases[@]}"; do
                deep_compare_bfs_metrics+="${deep_compare_bfs_metrics:+ }${deep_phase}_US"
                positive_metric_names+="${positive_metric_names:+ }${deep_phase}_US"
                for deep_counter in "${deep_compare_counters[@]}"; do
                    deep_compare_bfs_metrics+="${deep_compare_bfs_metrics:+ }${deep_phase}_${deep_counter}"
                done
            done
            deep_compare_bfs_metrics+=" CLOCK_HZ CRC_SAMPLE_STRIDE"
            positive_metric_names+=" CLOCK_HZ CRC_SAMPLE_STRIDE"
        elif [[ "$observed_version" != "$header_version" ]]; then
            printf 'ERROR: mixed deep-compare schema versions\n' >&2
            exit 1
        fi
        if [[ "$filesystem" == bfs ]]; then metrics=$deep_compare_bfs_metrics; else
            deep_compare_pfs3_metrics=
            for deep_phase in "${deep_compare_phases[@]}"; do
                deep_compare_pfs3_metrics+="${deep_compare_pfs3_metrics:+ }${deep_phase}_US"
            done
            metrics=$deep_compare_pfs3_metrics
        fi
        read -r -a metric_names_array <<<"$metrics"
        metric_count=${#metric_names_array[@]}
    else
        positive_metric_names=
    fi
    awk -F '\t' -v drive="$drive" -v header="$header" -v header_version="$header_version" -v metric_names="$metrics" -v metric_count="$metric_count" -v value_pattern="$value_pattern" -v positive_metric_names="${positive_metric_names:-}" -v fixed_metric_name="$fixed_metric_name" -v fixed_metric_value="$fixed_metric_value" -v phases="${deep_compare_phases[*]:-}" '
        BEGIN {
            split(metric_names, names, " ")
            positive_count = split(positive_metric_names, positive_names, " ")
        }
        NR == 1 { if (NF != 2 || $1 != header || $2 != header_version) exit 1; next }
        NR == 2 { if (NF != 2 || $1 != "DRIVE" || $2 != drive) exit 1; next }
        NR >= 3 && NR <= metric_count + 2 {
            if (NF != 2 || $1 != names[NR-2] || $2 !~ value_pattern) exit 1
            metric_values[$1] = $2 + 0
            for (i = 1; i <= positive_count; i++)
                if ($1 == positive_names[i] && $2 !~ /^[1-9][0-9]*$/) exit 1
            if ($1 == fixed_metric_name && $2 != fixed_metric_value) exit 1
            next
        }
        NR == metric_count + 3 { if (NF != 2 || $1 != "PASS" || $2 != "1") exit 1; next }
        { exit 1 }
        END {
            if (NR != metric_count + 3) exit 1
            if ((header_version == 5 || header_version == 6 || header_version == 7 || header_version == 8) && drive == "DH1:") {
                phase_count = split(phases, phase_names, " ")
                for (phase_index = 1; phase_index <= phase_count; phase_index++) {
                    phase = phase_names[phase_index]
                    bucket_sum = metric_values[phase "_FREE_TREE_ALLOCATION_BODY_NODE_WRITES"] + metric_values[phase "_FREE_TREE_RESERVE_REFILL_NODE_WRITES"] + metric_values[phase "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"] + metric_values[phase "_FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES"] + metric_values[phase "_FREE_TREE_OTHER_NODE_WRITES"]
                    if (bucket_sum != metric_values[phase "_FREE_TREE_NODE_WRITES"]) exit 1
                }
            }
            if ((header_version == 6 || header_version == 7 || header_version == 8) && drive == "DH1:") {
                for (phase_index = 1; phase_index <= phase_count; phase_index++) {
                    phase = phase_names[phase_index]
                    runs = metric_values[phase "_FREE_TREE_RESERVE_RETURN_RUNS"]
                    blocks = metric_values[phase "_FREE_TREE_RESERVE_RETURN_BLOCKS"]
                    one = metric_values[phase "_FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK"]
                    two_three = metric_values[phase "_FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS"]
                    four_seven = metric_values[phase "_FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS"]
                    eight_plus = metric_values[phase "_FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS"]
                    max_run = metric_values[phase "_FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS"]
                    max_run_writes = metric_values[phase "_FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN"]
                    reserve_writes = metric_values[phase "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"]
                    if (header_version == 7 || header_version == 8) {
                        run_node_writes = metric_values[phase "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES"]
                        batch_calls = metric_values[phase "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS"]
                        batch_blocks = metric_values[phase "_FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS"]
                        batch_node_writes = metric_values[phase "_FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES"]
                    }
                    txn_commits = metric_values[phase "_TXN_COMMITS"]
                    return_calls = metric_values[phase "_FREE_TREE_RESERVE_RETURN_CALLS"]
                    passes = metric_values[phase "_POST_PUBLISH_RECLAIM_PASSES"]
                    max_passes = metric_values[phase "_MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT"]
                    publications = metric_values[phase "_SUPERBLOCK_PUBLICATIONS"]
                    if (one + two_three + four_seven + eight_plus != runs) exit 1
                    min_blocks = one + 2 * two_three + 4 * four_seven + 8 * eight_plus
                    max_blocks = one + 3 * two_three + 7 * four_seven + max_run * eight_plus
                    if (blocks < runs || blocks < min_blocks || blocks > max_blocks) exit 1
                    if (runs == 0) {
                        if (blocks != 0 || max_run != 0 || max_run_writes != 0) exit 1
                        if (header_version == 6 && reserve_writes != 0) exit 1
                    } else {
                        if (max_run < 1 || max_run > blocks) exit 1
                        if (max_run == 1 && (one == 0 || two_three + four_seven + eight_plus != 0)) exit 1
                        if (max_run >= 2 && max_run <= 3 && (two_three == 0 || four_seven + eight_plus != 0)) exit 1
                        if (max_run >= 4 && max_run <= 7 && (four_seven == 0 || eight_plus != 0)) exit 1
                        if (max_run >= 8 && eight_plus == 0) exit 1
                        if (max_run_writes > reserve_writes) exit 1
                    }
                    if (header_version == 7 || header_version == 8) {
                        if (run_node_writes + batch_node_writes != reserve_writes) exit 1
                        if (runs == 0 && run_node_writes != 0) exit 1
                        if (max_run_writes > run_node_writes) exit 1
                        if (batch_calls > return_calls || batch_blocks < batch_calls) exit 1
                        if (batch_calls == 0 && (batch_blocks != 0 || batch_node_writes != 0)) exit 1
                    }
                    if (header_version == 8) {
                        skip_shape = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_SHAPE"]
                        skip_small = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_SMALL"]
                        skip_emergency = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY"]
                        skip_capacity = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY"]
                        if (batch_calls + skip_shape + skip_small + skip_emergency + skip_capacity != return_calls) exit 1
                    }
                    if (return_calls < txn_commits) exit 1
                    if (passes > txn_commits * 256 || max_passes > 256 || max_passes > passes) exit 1
                    if ((passes == 0 && max_passes != 0) || (passes > 0 && max_passes == 0)) exit 1
                    if (publications != txn_commits + passes) exit 1
                }
            }
        }
    ' "$output" || { printf 'ERROR: invalid or incomplete %s workload output\n' "$filesystem" >&2; exit 1; }
done

printf 'PASS: requested mounted-volume workload outputs are complete and data-checked\n'
