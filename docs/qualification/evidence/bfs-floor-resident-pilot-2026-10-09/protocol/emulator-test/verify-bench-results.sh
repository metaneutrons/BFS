#!/usr/bin/env bash
# Accept only a completed, symmetric, data-checked filesystem comparison.
set -euo pipefail

run_dir=${1:?usage: verify-bench-results.sh RUN_DIR [compare|durable-compare|profile|internal|deep|deep-compare]}
mode=${2:-compare}
if [[ "$mode" == split-* ]]; then
    script_dir=$(cd "$(dirname "$0")" && pwd)
    exec python3 "$script_dir/../tools/verify-split-bench.py" "$run_dir/system/Results" "$mode"
fi
header_version=1
fixed_metric_name=
fixed_metric_value=
fixed_metric_name2=
fixed_metric_value2=
fixed_metric_name3=
fixed_metric_value3=
case "$mode" in
    compare)
        marker_text=BFS-PFS3-COMPLETE
        suffix=tsv
        header=FS_COMPARE_BENCH
        versioned_compare=1
        value_pattern='^[1-9][0-9]*$'
        filesystems=(bfs pfs3)
        ;;
    durable-compare)
        marker_text=BFS-PFS3-DURABLE-COMPLETE
        suffix=durable.tsv
        header=FS_DURABLE_COMPARE
        versioned_compare=1
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
        deep_compare_counters_v9=("${deep_compare_counters_v8[@]}" SEALED_COMMITS SEALED_METADATA_FENCES)
        deep_compare_counters_v10=("${deep_compare_counters_v9[@]}" BTREE_MALLOC_CALLS BTREE_MALLOC_SAMPLES BTREE_MALLOC_SAMPLE_TICKS BTREE_FREE_CALLS BTREE_FREE_SAMPLES BTREE_FREE_SAMPLE_TICKS IFACE_ALLOC_CALLS IFACE_ALLOC_SAMPLES IFACE_ALLOC_SAMPLE_TICKS FREESPACE_ALLOC_SAMPLES FREESPACE_ALLOC_SAMPLE_TICKS)
        deep_compare_cpu_scopes=(PACKET PACKET_OPEN PACKET_READ PACKET_WRITE PACKET_END PACKET_DELETE PACKET_FLUSH PACKET_OTHER CORE_CREATE CORE_DELETE CORE_FILE_WRITE CORE_SYNC IFACE_FREE SEAL_COMMIT)
        deep_compare_packet_categories=(PACKET_OPEN PACKET_READ PACKET_WRITE PACKET_END PACKET_DELETE PACKET_FLUSH PACKET_OTHER)
        deep_compare_counters_v11=("${deep_compare_counters_v10[@]}")
        for cpu_scope in "${deep_compare_cpu_scopes[@]}"; do
            deep_compare_counters_v11+=("${cpu_scope}_CALLS" "${cpu_scope}_SAMPLES" "${cpu_scope}_SAMPLE_TICKS")
        done
        deep_compare_lookup_counters=(INODE_READ_CALLS BTREE_SEARCH_CALLS BTREE_INDEX_HINT_HITS BTREE_LEAF_HINT_HITS CACHE_READ_CALLS CACHE_READ_HITS CACHE_READ_MISSES FREE_TREE_NODE_VIEWS FREE_TREE_RESIDENT_VIEWS DIR_TREE_NODE_VIEWS DIR_TREE_RESIDENT_VIEWS INODE_TREE_NODE_VIEWS INODE_TREE_RESIDENT_VIEWS REFCOUNT_TREE_NODE_VIEWS REFCOUNT_TREE_RESIDENT_VIEWS OTHER_TREE_NODE_VIEWS OTHER_TREE_RESIDENT_VIEWS)
        deep_compare_counters_v12=("${deep_compare_counters_v11[@]}" "${deep_compare_lookup_counters[@]}")
        deep_compare_detail_scopes=(DETAIL_INODE_READ DETAIL_INODE_VALIDATE DETAIL_INODE_SEARCH DETAIL_INODE_NODE_VIEW DETAIL_DIR_NODE_VIEW DETAIL_INODE_BINARY_SEARCH DETAIL_DIR_BINARY_SEARCH DETAIL_NODE_STRUCTURE DETAIL_CACHE_PEEK DETAIL_BUFFER_ALLOC DETAIL_BUFFER_FREE DETAIL_EXALL_FILL)
        deep_compare_counters_v13=("${deep_compare_counters_v12[@]}")
        for detail_scope in "${deep_compare_detail_scopes[@]}"; do
            deep_compare_counters_v13+=("${detail_scope}_CALLS" "${detail_scope}_SAMPLES" "${detail_scope}_SAMPLE_TICKS")
        done
        deep_compare_counters_v14=("${deep_compare_counters_v13[@]}" DEFERRED_NODE_WRITES)
        deep_compare_node_level_counters=(DIR_TREE_LEAF_NODE_VIEWS DIR_TREE_LEAF_RESIDENT_VIEWS DIR_TREE_INTERNAL_NODE_VIEWS DIR_TREE_INTERNAL_RESIDENT_VIEWS INODE_TREE_LEAF_NODE_VIEWS INODE_TREE_LEAF_RESIDENT_VIEWS INODE_TREE_INTERNAL_NODE_VIEWS INODE_TREE_INTERNAL_RESIDENT_VIEWS)
        deep_compare_counters_v15=("${deep_compare_counters_v12[@]}" "${deep_compare_node_level_counters[@]}")
        for detail_scope in "${deep_compare_detail_scopes[@]}"; do
            deep_compare_counters_v15+=("${detail_scope}_CALLS" "${detail_scope}_SAMPLES" "${detail_scope}_SAMPLE_TICKS")
        done
        deep_compare_counters_v15+=(DEFERRED_NODE_WRITES)
        deep_compare_readonly_counters=(BIO_WRITES BIO_UPDATES DATA_READS DATA_WRITES NODE_WRITES TXN_COMMITS FREESPACE_ALLOCS EXTENT_MAPS FREE_TREE_NODE_WRITES DIR_TREE_NODE_WRITES INODE_TREE_NODE_WRITES REFCOUNT_TREE_NODE_WRITES OTHER_TREE_NODE_WRITES NODE_CRC_WRITE_CALLS NODE_CRC_WRITE_SAMPLES NODE_CRC_WRITE_SAMPLE_TICKS FREE_TREE_RESERVE_RETURN_CALLS FREE_TREE_RESERVE_RETURN_RUNS FREE_TREE_RESERVE_RETURN_BLOCKS FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES FREE_TREE_RESERVE_RETURN_BATCH_CALLS FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES FREE_TREE_RESERVE_RETURN_SKIP_SHAPE FREE_TREE_RESERVE_RETURN_SKIP_SMALL FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY POST_PUBLISH_RECLAIM_PASSES MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT SUPERBLOCK_PUBLICATIONS SEALED_COMMITS SEALED_METADATA_FENCES)
        deep_compare_phases_v12=(SMALL_CREATE_40 LOOKUP_400 SMALL_READ_40 SEQ_WRITE_8M SEQ_READ_8M SMALL_DELETE_40 APPEND_4K_1M APPEND_1K_256K APPEND_READ_1280K LIST_EXNEXT_40_ENTRIES_FIRST_PASS LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL LIST_EXALL_40_ENTRIES_FIRST_PASS LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL LIST_EXNEXT_400_ENTRIES_FIRST_PASS LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL LIST_EXALL_400_ENTRIES_FIRST_PASS LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL LIST_EXNEXT_1000_ENTRIES_FIRST_PASS LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL LIST_EXALL_1000_ENTRIES_FIRST_PASS LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL)
        deep_compare_bfs_metrics=
        deep_compare_pfs3_metrics=
        positive_metric_names=
        filesystems=(bfs pfs3)
        ;;
    *) printf 'ERROR: mode must be compare, durable-compare, profile, internal, deep, or deep-compare\n' >&2; exit 2 ;;
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
    elif [[ -n "${versioned_compare:-}" ]]; then
        # Schema 2 adds the legacy listings after the small reads; schema 3
        # appends small-step growth; schema 4 appends validated listing scales.
        IFS=$'\t' read -r observed_header observed_version < "$output"
        if [[ "$observed_header" != "$header" ||
              ( "$observed_version" != 1 && "$observed_version" != 2 &&
                "$observed_version" != 3 && "$observed_version" != 4 ) ]]; then
            printf 'ERROR: invalid compare schema header in %s output\n' "$filesystem" >&2
            exit 1
        fi
        if [[ "$filesystem" == "${filesystems[0]}" ]]; then
            header_version=$observed_version
        elif [[ "$observed_version" != "$header_version" ]]; then
            printf 'ERROR: mixed compare schema versions\n' >&2
            exit 1
        fi
        if [[ "$header_version" == 1 ]]; then
            metrics='SMALL_CREATE_40_US LOOKUP_400_US SMALL_READ_40_US SEQ_WRITE_8M_US SEQ_READ_8M_US SMALL_DELETE_40_US'
        elif [[ "$header_version" == 2 ]]; then
            metrics='SMALL_CREATE_40_US LOOKUP_400_US SMALL_READ_40_US LIST_EXNEXT_400_US LIST_EXALL_400_US SEQ_WRITE_8M_US SEQ_READ_8M_US SMALL_DELETE_40_US'
        elif [[ "$header_version" == 3 ]]; then
            metrics='SMALL_CREATE_40_US LOOKUP_400_US SMALL_READ_40_US LIST_EXNEXT_400_US LIST_EXALL_400_US SEQ_WRITE_8M_US SEQ_READ_8M_US SMALL_DELETE_40_US APPEND_4K_1M_US APPEND_1K_256K_US APPEND_READ_1280K_US'
        else
            metrics='SMALL_CREATE_40_US LOOKUP_400_US SMALL_READ_40_US LIST_EXNEXT_400_US LIST_EXALL_400_US SEQ_WRITE_8M_US SEQ_READ_8M_US SMALL_DELETE_40_US APPEND_4K_1M_US APPEND_1K_256K_US APPEND_READ_1280K_US LIST_EXNEXT_40_ENTRIES_FIRST_PASS_US LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL_US LIST_EXALL_40_ENTRIES_FIRST_PASS_US LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL_US LIST_EXNEXT_400_ENTRIES_FIRST_PASS_US LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL_US LIST_EXALL_400_ENTRIES_FIRST_PASS_US LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL_US LIST_EXNEXT_1000_ENTRIES_FIRST_PASS_US LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL_US LIST_EXALL_1000_ENTRIES_FIRST_PASS_US LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL_US'
        fi
        read -r -a metric_names_array <<<"$metrics"
        metric_count=${#metric_names_array[@]}
        positive_metric_names=
    elif [[ "$mode" == deep-compare ]]; then
        IFS=$'\t' read -r observed_header observed_version < "$output"
        if [[ "$observed_header" != "$header" ]]; then
            printf 'ERROR: invalid deep-compare schema header in %s output\n' "$filesystem" >&2
            exit 1
        fi
        if [[ "$filesystem" == bfs ]]; then
            if [[ "$observed_version" != 4 && "$observed_version" != 5 && "$observed_version" != 6 && "$observed_version" != 7 && "$observed_version" != 8 && "$observed_version" != 9 && "$observed_version" != 10 && "$observed_version" != 11 && "$observed_version" != 12 && "$observed_version" != 13 && "$observed_version" != 14 && "$observed_version" != 15 ]]; then
                printf 'ERROR: unsupported deep-compare schema version %s\n' "$observed_version" >&2
                exit 1
            fi
            header_version=$observed_version
            if (( header_version >= 12 )); then
                deep_compare_phases=("${deep_compare_phases_v12[@]}")
            fi
            if [[ "$header_version" == 4 ]]; then
                deep_compare_counters=("${deep_compare_counters_v4[@]}")
            elif [[ "$header_version" == 5 ]]; then
                deep_compare_counters=("${deep_compare_counters_v5[@]}")
            elif [[ "$header_version" == 6 ]]; then
                deep_compare_counters=("${deep_compare_counters_v6[@]}")
            elif [[ "$header_version" == 7 ]]; then
                deep_compare_counters=("${deep_compare_counters_v7[@]}")
            elif [[ "$header_version" == 8 ]]; then
                deep_compare_counters=("${deep_compare_counters_v8[@]}")
            elif [[ "$header_version" == 9 ]]; then
                deep_compare_counters=("${deep_compare_counters_v9[@]}")
            elif [[ "$header_version" == 10 ]]; then
                deep_compare_counters=("${deep_compare_counters_v10[@]}")
            elif [[ "$header_version" == 11 ]]; then
                deep_compare_counters=("${deep_compare_counters_v11[@]}")
            elif [[ "$header_version" == 12 ]]; then
                deep_compare_counters=("${deep_compare_counters_v12[@]}")
            elif [[ "$header_version" == 13 ]]; then
                deep_compare_counters=("${deep_compare_counters_v13[@]}")
            elif [[ "$header_version" == 14 ]]; then
                deep_compare_counters=("${deep_compare_counters_v14[@]}")
            else
                deep_compare_counters=("${deep_compare_counters_v15[@]}")
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
            if (( header_version >= 10 )); then
                deep_compare_bfs_metrics+=" CPU_SAMPLE_STRIDE"
                positive_metric_names+=" CPU_SAMPLE_STRIDE"
                fixed_metric_name2=CPU_SAMPLE_STRIDE
                fixed_metric_value2=1
            fi
            if (( header_version >= 13 )); then
                deep_compare_bfs_metrics+=" DETAIL_SAMPLE_STRIDE"
                positive_metric_names+=" DETAIL_SAMPLE_STRIDE"
                fixed_metric_name3=DETAIL_SAMPLE_STRIDE
                fixed_metric_value3=17
            fi
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
    # Expanded diagnostic schemas exceed Linux's per-argument size limit.
    # Read the ordered metric inventory from stdin, not one giant -v argument.
    printf '%s\n' "$metrics" | awk -F '\t' -v drive="$drive" -v header="$header" -v header_version="$header_version" -v metric_count="$metric_count" -v value_pattern="$value_pattern" -v positive_metric_names="${positive_metric_names:-}" -v fixed_metric_name="$fixed_metric_name" -v fixed_metric_value="$fixed_metric_value" -v fixed_metric_name2="$fixed_metric_name2" -v fixed_metric_value2="$fixed_metric_value2" -v fixed_metric_name3="$fixed_metric_name3" -v fixed_metric_value3="$fixed_metric_value3" -v phases="${deep_compare_phases[*]:-}" -v cpu_scopes="${deep_compare_cpu_scopes[*]:-}" -v packet_categories="${deep_compare_packet_categories[*]:-}" -v lookup_counters="${deep_compare_lookup_counters[*]:-}" -v node_level_counters="${deep_compare_node_level_counters[*]:-}" -v detail_scopes="${deep_compare_detail_scopes[*]:-}" -v readonly_counters="${deep_compare_readonly_counters[*]:-}" '
        function decimal_normalize(value) {
            while (length(value) > 1 && substr(value, 1, 1) == "0")
                value = substr(value, 2)
            return value
        }
        function decimal_compare(left, right, normalized_left, normalized_right) {
            normalized_left = decimal_normalize(left)
            normalized_right = decimal_normalize(right)
            if (length(normalized_left) < length(normalized_right)) return -1
            if (length(normalized_left) > length(normalized_right)) return 1
            if (("x" normalized_left) < ("x" normalized_right)) return -1
            if (("x" normalized_left) > ("x" normalized_right)) return 1
            return 0
        }
        function decimal_equal(left, right) {
            return decimal_compare(left, right) == 0
        }
        function decimal_leq(left, right) {
            return decimal_compare(left, right) <= 0
        }
        function decimal_add(left, right, result, carry, left_index,
                             right_index, left_digit, right_digit, digit_sum,
                             digit) {
            result = ""
            carry = 0
            left_index = length(left)
            right_index = length(right)
            while (left_index > 0 || right_index > 0 || carry > 0) {
                left_digit = left_index > 0 ? substr(left, left_index, 1) + 0 : 0
                right_digit = right_index > 0 ? substr(right, right_index, 1) + 0 : 0
                digit_sum = left_digit + right_digit + carry
                digit = digit_sum % 10
                carry = int(digit_sum / 10)
                result = sprintf("%d", digit) result
                left_index--
                right_index--
            }
            return decimal_normalize(result)
        }
        BEGIN {
            if ((getline metric_names < "/dev/stdin") != 1) exit 1
            close("/dev/stdin")
            split(metric_names, names, " ")
            positive_count = split(positive_metric_names, positive_names, " ")
            cpu_scope_count = split(cpu_scopes, cpu_scope_names, " ")
            packet_category_count = split(packet_categories, packet_category_names, " ")
            lookup_counter_count = split(lookup_counters, lookup_counter_names, " ")
            node_level_counter_count = split(node_level_counters, node_level_counter_names, " ")
            detail_scope_count = split(detail_scopes, detail_scope_names, " ")
            readonly_counter_count = split(readonly_counters, readonly_counter_names, " ")
        }
        NR == 1 { if (NF != 2 || $1 != header || $2 != header_version) exit 1; next }
        NR == 2 { if (NF != 2 || $1 != "DRIVE" || $2 != drive) exit 1; next }
        NR >= 3 && NR <= metric_count + 2 {
            if (NF != 2 || $1 != names[NR-2] || $2 !~ value_pattern) exit 1
            metric_values[$1] = $2 + 0
            metric_text[$1] = $2
            for (i = 1; i <= positive_count; i++)
                if ($1 == positive_names[i] && $2 !~ /^[1-9][0-9]*$/) exit 1
            if ($1 == fixed_metric_name && $2 != fixed_metric_value) exit 1
            if ($1 == fixed_metric_name2 && $2 != fixed_metric_value2) exit 1
            if ($1 == fixed_metric_name3 && $2 != fixed_metric_value3) exit 1
            next
        }
        NR == metric_count + 3 { if (NF != 2 || $1 != "PASS" || $2 != "1") exit 1; next }
        { exit 1 }
        END {
            if (NR != metric_count + 3) exit 1
            if (header_version >= 5 && drive == "DH1:") {
                phase_count = split(phases, phase_names, " ")
                for (phase_index = 1; phase_index <= phase_count; phase_index++) {
                    phase = phase_names[phase_index]
                    bucket_sum = metric_values[phase "_FREE_TREE_ALLOCATION_BODY_NODE_WRITES"] + metric_values[phase "_FREE_TREE_RESERVE_REFILL_NODE_WRITES"] + metric_values[phase "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"] + metric_values[phase "_FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES"] + metric_values[phase "_FREE_TREE_OTHER_NODE_WRITES"]
                    if (bucket_sum != metric_values[phase "_FREE_TREE_NODE_WRITES"]) exit 1
                }
            }
            if (header_version >= 6 && drive == "DH1:") {
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
                    if (header_version >= 7) {
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
                    if (header_version >= 7) {
                        if (run_node_writes + batch_node_writes != reserve_writes) exit 1
                        if (runs == 0 && run_node_writes != 0) exit 1
                        if (max_run_writes > run_node_writes) exit 1
                        if (batch_calls > return_calls || batch_blocks < batch_calls) exit 1
                        if (batch_calls == 0 && (batch_blocks != 0 || batch_node_writes != 0)) exit 1
                    }
                    if (header_version >= 8) {
                        skip_shape = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_SHAPE"]
                        skip_small = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_SMALL"]
                        skip_emergency = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY"]
                        skip_capacity = metric_values[phase "_FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY"]
                        if (batch_calls + skip_shape + skip_small + skip_emergency + skip_capacity != return_calls) exit 1
                    }
                    if (header_version >= 9) {
                        sealed_commits = metric_values[phase "_SEALED_COMMITS"]
                        metadata_fences = metric_values[phase "_SEALED_METADATA_FENCES"]
                        bio_updates = metric_values[phase "_BIO_UPDATES"]
                        free_tree_writes = metric_values[phase "_FREE_TREE_NODE_WRITES"]
                        if (sealed_commits > txn_commits ||
                            metadata_fences != sealed_commits ||
                            return_calls + sealed_commits != txn_commits + passes ||
                            passes > (txn_commits - sealed_commits) * 256 ||
                            bio_updates < publications + metadata_fences + txn_commits ||
                            sealed_commits > free_tree_writes) exit 1
                    } else if (return_calls < txn_commits) exit 1
                    if (passes > txn_commits * 256 || max_passes > 256 || max_passes > passes) exit 1
                    if ((passes == 0 && max_passes != 0) || (passes > 0 && max_passes == 0)) exit 1
                    if (publications != txn_commits + passes) exit 1
                    if (header_version >= 10) {
                        cpu_stride = metric_values["CPU_SAMPLE_STRIDE"]
                        malloc_calls = metric_values[phase "_BTREE_MALLOC_CALLS"]
                        malloc_samples = metric_values[phase "_BTREE_MALLOC_SAMPLES"]
                        malloc_ticks = metric_values[phase "_BTREE_MALLOC_SAMPLE_TICKS"]
                        free_calls = metric_values[phase "_BTREE_FREE_CALLS"]
                        free_samples = metric_values[phase "_BTREE_FREE_SAMPLES"]
                        free_ticks = metric_values[phase "_BTREE_FREE_SAMPLE_TICKS"]
                        iface_calls = metric_values[phase "_IFACE_ALLOC_CALLS"]
                        iface_samples = metric_values[phase "_IFACE_ALLOC_SAMPLES"]
                        iface_ticks = metric_values[phase "_IFACE_ALLOC_SAMPLE_TICKS"]
                        freespace_calls = metric_values[phase "_FREESPACE_ALLOCS"]
                        freespace_samples = metric_values[phase "_FREESPACE_ALLOC_SAMPLES"]
                        freespace_ticks = metric_values[phase "_FREESPACE_ALLOC_SAMPLE_TICKS"]
                        if (malloc_samples != int(malloc_calls / cpu_stride) ||
                            free_samples != int(free_calls / cpu_stride) ||
                            iface_samples != int(iface_calls / cpu_stride) ||
                            freespace_samples != int(freespace_calls / cpu_stride)) exit 1
                        if ((malloc_samples == 0 && malloc_ticks != 0) ||
                            (free_samples == 0 && free_ticks != 0) ||
                            (iface_samples == 0 && iface_ticks != 0) ||
                            (freespace_samples == 0 && freespace_ticks != 0)) exit 1
                    }
                    if (header_version >= 11) {
                        for (scope_index = 1; scope_index <= cpu_scope_count; scope_index++) {
                            scope = cpu_scope_names[scope_index]
                            scope_calls = decimal_normalize(metric_text[phase "_" scope "_CALLS"])
                            scope_samples = decimal_normalize(metric_text[phase "_" scope "_SAMPLES"])
                            scope_ticks = decimal_normalize(metric_text[phase "_" scope "_SAMPLE_TICKS"])
                            if (!decimal_leq(scope_calls, "4294967295") ||
                                !decimal_leq(scope_samples, "4294967295") ||
                                !decimal_leq(scope_ticks, "18446744073709551615") ||
                                !decimal_equal(scope_samples, scope_calls) ||
                                (decimal_equal(scope_calls, "0") &&
                                 !decimal_equal(scope_ticks, "0"))) exit 1
                        }
                        packet_calls = decimal_normalize(metric_text[phase "_PACKET_CALLS"])
                        packet_samples = decimal_normalize(metric_text[phase "_PACKET_SAMPLES"])
                        packet_ticks = decimal_normalize(metric_text[phase "_PACKET_SAMPLE_TICKS"])
                        category_calls = category_samples = "0"
                        category_ticks = "0"
                        for (category_index = 1; category_index <= packet_category_count; category_index++) {
                            category = packet_category_names[category_index]
                            category_calls = decimal_add(category_calls, metric_text[phase "_" category "_CALLS"])
                            category_samples = decimal_add(category_samples, metric_text[phase "_" category "_SAMPLES"])
                            category_ticks = decimal_add(category_ticks, metric_text[phase "_" category "_SAMPLE_TICKS"])
                        }
                        if (!decimal_equal(packet_calls, category_calls) ||
                            !decimal_equal(packet_samples, category_samples) ||
                            length(packet_ticks) != length(category_ticks) ||
                            ("x" packet_ticks) != ("x" category_ticks)) exit 1
                    }
                    if (header_version >= 12 && drive == "DH1:") {
                        for (counter_index = 1;
                             counter_index <= lookup_counter_count;
                             counter_index++) {
                            counter = lookup_counter_names[counter_index]
                            counter_value = metric_text[phase "_" counter]
                            if (!decimal_leq(counter_value, "4294967295")) exit 1
                        }

                        cache_calls = metric_text[phase "_CACHE_READ_CALLS"]
                        cache_hits = metric_text[phase "_CACHE_READ_HITS"]
                        cache_misses = metric_text[phase "_CACHE_READ_MISSES"]
                        if (!decimal_equal(cache_calls,
                                           decimal_add(cache_hits, cache_misses))) exit 1

                        searches = metric_text[phase "_BTREE_SEARCH_CALLS"]
                        hint_hits = decimal_add(metric_text[phase "_BTREE_INDEX_HINT_HITS"], metric_text[phase "_BTREE_LEAF_HINT_HITS"])
                        if (!decimal_leq(hint_hits, searches)) exit 1

                        for (tree_name_index = 1; tree_name_index <= 5;
                             tree_name_index++) {
                            if (tree_name_index == 1) tree_name = "FREE_TREE"
                            else if (tree_name_index == 2) tree_name = "DIR_TREE"
                            else if (tree_name_index == 3) tree_name = "INODE_TREE"
                            else if (tree_name_index == 4) tree_name = "REFCOUNT_TREE"
                            else tree_name = "OTHER_TREE"
                            views = metric_text[phase "_" tree_name "_NODE_VIEWS"]
                            resident = metric_text[phase "_" tree_name "_RESIDENT_VIEWS"]
                            if (!decimal_leq(resident, views)) exit 1
                        }

                        read_crc_calls = metric_values[phase "_NODE_CRC_READ_CALLS"]
                        read_crc_samples = metric_values[phase "_NODE_CRC_READ_SAMPLES"]
                        write_crc_calls = metric_values[phase "_NODE_CRC_WRITE_CALLS"]
                        write_crc_samples = metric_values[phase "_NODE_CRC_WRITE_SAMPLES"]
                        crc_stride = metric_values["CRC_SAMPLE_STRIDE"]
                        if (!decimal_leq(metric_text[phase "_NODE_CRC_READ_CALLS"], "4294967295") ||
                            !decimal_leq(metric_text[phase "_NODE_CRC_READ_SAMPLES"], "4294967295") ||
                            !decimal_leq(metric_text[phase "_NODE_CRC_WRITE_CALLS"], "4294967295") ||
                            !decimal_leq(metric_text[phase "_NODE_CRC_WRITE_SAMPLES"], "4294967295") ||
                            !decimal_leq(metric_text[phase "_NODE_CRC_READ_SAMPLE_TICKS"], "18446744073709551615") ||
                            !decimal_leq(metric_text[phase "_NODE_CRC_WRITE_SAMPLE_TICKS"], "18446744073709551615")) exit 1
                        if (read_crc_samples != int(read_crc_calls / crc_stride) ||
                            write_crc_samples != int(write_crc_calls / crc_stride)) exit 1
                        if ((read_crc_samples == 0 &&
                             metric_values[phase "_NODE_CRC_READ_SAMPLE_TICKS"] != 0) ||
                            (write_crc_samples == 0 &&
                             metric_values[phase "_NODE_CRC_WRITE_SAMPLE_TICKS"] != 0)) exit 1

                        if (phase ~ /^LIST_/) {
                            for (counter_index = 1;
                                 counter_index <= readonly_counter_count;
                                 counter_index++) {
                                counter = readonly_counter_names[counter_index]
                                if (!decimal_equal(metric_text[phase "_" counter], "0")) exit 1
                            }
                            for (scope_index = 1;
                                 scope_index <= cpu_scope_count;
                                 scope_index++) {
                                scope = cpu_scope_names[scope_index]
                                if ((scope == "CORE_CREATE" ||
                                     scope == "CORE_DELETE" ||
                                     scope == "CORE_FILE_WRITE" ||
                                     scope == "CORE_SYNC" ||
                                     scope == "SEAL_COMMIT") &&
                                    !decimal_equal(metric_text[phase "_" scope "_CALLS"], "0")) exit 1
                            }

                            expected_inodes = 0
                            if (phase ~ /_40_ENTRIES_/) expected_inodes = 40
                            else if (phase ~ /_400_ENTRIES_/) expected_inodes = 400
                            else if (phase ~ /_1000_ENTRIES_/) expected_inodes = 1000
                            if (phase ~ /REPEAT10_TOTAL$/) expected_inodes *= 10
                            if (!decimal_leq(sprintf("%.0f", expected_inodes),
                                             metric_text[phase "_INODE_READ_CALLS"])) exit 1
                        }
                    }
                    if (header_version >= 13 && drive == "DH1:") {
                        detail_stride = metric_values["DETAIL_SAMPLE_STRIDE"]
                        for (detail_index = 1;
                             detail_index <= detail_scope_count;
                             detail_index++) {
                            detail_scope = detail_scope_names[detail_index]
                            detail_calls_name = phase "_" detail_scope "_CALLS"
                            detail_samples_name = phase "_" detail_scope "_SAMPLES"
                            detail_ticks_name = phase "_" detail_scope "_SAMPLE_TICKS"
                            detail_calls = metric_text[detail_calls_name]
                            detail_samples = metric_text[detail_samples_name]
                            detail_ticks = metric_text[detail_ticks_name]
                            if (!decimal_leq(detail_calls, "4294967295") ||
                                !decimal_leq(detail_samples, "4294967295") ||
                                !decimal_leq(detail_ticks, "18446744073709551615") ||
                                detail_samples != int((detail_calls + 0) / detail_stride) ||
                                (decimal_equal(detail_samples, "0") &&
                                 !decimal_equal(detail_ticks, "0"))) exit 1
                        }

                        if (!decimal_equal(metric_text[phase "_DETAIL_INODE_READ_CALLS"],
                                           metric_text[phase "_INODE_READ_CALLS"]) ||
                            !decimal_leq(metric_text[phase "_DETAIL_INODE_SEARCH_CALLS"],
                                        metric_text[phase "_BTREE_SEARCH_CALLS"]) ||
                            !decimal_equal(metric_text[phase "_DETAIL_INODE_NODE_VIEW_CALLS"],
                                           metric_text[phase "_INODE_TREE_NODE_VIEWS"]) ||
                            !decimal_equal(metric_text[phase "_DETAIL_DIR_NODE_VIEW_CALLS"],
                                           metric_text[phase "_DIR_TREE_NODE_VIEWS"])) exit 1

                        if (phase ~ /^LIST_EXNEXT_/) {
                            if (!decimal_equal(metric_text[phase "_DETAIL_INODE_VALIDATE_CALLS"],
                                               metric_text[phase "_INODE_READ_CALLS"]) ||
                                !decimal_equal(metric_text[phase "_DETAIL_INODE_SEARCH_CALLS"],
                                               metric_text[phase "_INODE_READ_CALLS"]) ||
                                !decimal_equal(metric_text[phase "_DETAIL_EXALL_FILL_CALLS"], "0")) exit 1
                        } else if (phase ~ /^LIST_EXALL_/) {
                            if (!decimal_equal(metric_text[phase "_DETAIL_INODE_VALIDATE_CALLS"],
                                               metric_text[phase "_INODE_READ_CALLS"]) ||
                                !decimal_equal(metric_text[phase "_DETAIL_INODE_SEARCH_CALLS"],
                                               metric_text[phase "_INODE_READ_CALLS"]) ||
                                !decimal_leq(metric_text[phase "_INODE_READ_CALLS"],
                                            metric_text[phase "_DETAIL_EXALL_FILL_CALLS"])) exit 1
                        }
                    }
                    if (header_version >= 14 && drive == "DH1:") {
                        deferred_writes = metric_text[phase "_DEFERRED_NODE_WRITES"]
                        node_writes = metric_text[phase "_NODE_WRITES"]
                        if (!decimal_leq(deferred_writes, "4294967295") ||
                            !decimal_leq(deferred_writes, node_writes)) exit 1

                        tree_write_sum = "0"
                        for (tree_write_index = 1; tree_write_index <= 5;
                             tree_write_index++) {
                            if (tree_write_index == 1) tree_name = "FREE_TREE"
                            else if (tree_write_index == 2) tree_name = "DIR_TREE"
                            else if (tree_write_index == 3) tree_name = "INODE_TREE"
                            else if (tree_write_index == 4) tree_name = "REFCOUNT_TREE"
                            else tree_name = "OTHER_TREE"
                            tree_write_sum = decimal_add(tree_write_sum,
                                metric_text[phase "_" tree_name "_NODE_WRITES"])
                        }
                        if (!decimal_equal(tree_write_sum, node_writes) ||
                            (phase ~ /^LIST_/ &&
                             !decimal_equal(deferred_writes, "0"))) exit 1
                    }
                    if (header_version >= 15 && drive == "DH1:") {
                        for (counter_index = 1;
                             counter_index <= node_level_counter_count;
                             counter_index++) {
                            counter = node_level_counter_names[counter_index]
                            if (!decimal_leq(metric_text[phase "_" counter],
                                             "4294967295")) exit 1
                        }

                        for (tree_index = 1; tree_index <= 2; tree_index++) {
                            if (tree_index == 1) tree_name = "DIR_TREE"
                            else tree_name = "INODE_TREE"
                            leaf_views = metric_text[phase "_" tree_name "_LEAF_NODE_VIEWS"]
                            leaf_resident = metric_text[phase "_" tree_name "_LEAF_RESIDENT_VIEWS"]
                            internal_views = metric_text[phase "_" tree_name "_INTERNAL_NODE_VIEWS"]
                            internal_resident = metric_text[phase "_" tree_name "_INTERNAL_RESIDENT_VIEWS"]
                            aggregate_views = metric_text[phase "_" tree_name "_NODE_VIEWS"]
                            aggregate_resident = metric_text[phase "_" tree_name "_RESIDENT_VIEWS"]
                            if (!decimal_equal(aggregate_views,
                                               decimal_add(leaf_views, internal_views)) ||
                                !decimal_equal(aggregate_resident,
                                               decimal_add(leaf_resident, internal_resident)) ||
                                !decimal_leq(leaf_resident, leaf_views) ||
                                !decimal_leq(internal_resident, internal_views)) exit 1
                        }
                    }
                }
            }
        }
    ' "$output" || { printf 'ERROR: invalid or incomplete %s workload output\n' "$filesystem" >&2; exit 1; }
done

printf 'PASS: requested mounted-volume workload outputs are complete and data-checked\n'
