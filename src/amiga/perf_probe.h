/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_PERF_PROBE_H
#define BFS_AMIGA_PERF_PROBE_H

#include <exec/types.h>
#include <devices/timer.h>
#include <stdint.h>

#define BFS_ACTION_PERF_RESET 3010
#define BFS_ACTION_PERF_READ  3011
#define BFS_PERF_PROBE_VERSION 10
#define BFS_PERF_CRC_SAMPLE_STRIDE 64u

typedef struct bfs_perf_probe_snapshot {
    ULONG version;
    ULONG size;
    ULONG bio_read_calls;
    ULONG bio_write_calls;
    ULONG bio_update_calls;
    ULONG freespace_alloc_calls;
    ULONG extent_map_calls;
    ULONG data_read_calls;
    ULONG data_write_calls;
    ULONG btree_node_writes;
    ULONG txn_commit_calls;
    ULONG clock_hz;
    // cppcheck-suppress unusedStructMember
    uint64_t bio_read_ticks;
    // cppcheck-suppress unusedStructMember
    uint64_t bio_write_ticks;
    // cppcheck-suppress unusedStructMember
    uint64_t bio_update_ticks;
    // cppcheck-suppress unusedStructMember
    uint64_t data_read_ticks;
    // cppcheck-suppress unusedStructMember
    uint64_t data_write_ticks;
    ULONG free_tree_node_writes;
    ULONG dir_tree_node_writes;
    ULONG inode_tree_node_writes;
    ULONG refcount_tree_node_writes;
    ULONG other_tree_node_writes;
    ULONG node_crc_read_calls;
    ULONG node_crc_write_calls;
    ULONG node_crc_read_samples;
    ULONG node_crc_write_samples;
    ULONG clock_pair_ticks;
    // cppcheck-suppress unusedStructMember
    uint64_t node_crc_read_sample_ticks;
    // cppcheck-suppress unusedStructMember
    uint64_t node_crc_write_sample_ticks;
    ULONG free_tree_allocation_body_node_writes;
    ULONG free_tree_reserve_refill_node_writes;
    ULONG free_tree_reserve_return_node_writes;
    ULONG free_tree_post_publish_pending_reclaim_node_writes;
    ULONG free_tree_other_node_writes;
    ULONG free_tree_reserve_return_calls;
    ULONG free_tree_reserve_return_runs;
    ULONG free_tree_reserve_return_blocks;
    ULONG free_tree_reserve_return_runs_1_block;
    ULONG free_tree_reserve_return_runs_2_3_blocks;
    ULONG free_tree_reserve_return_runs_4_7_blocks;
    ULONG free_tree_reserve_return_runs_8_plus_blocks;
    ULONG free_tree_reserve_return_max_run_blocks;
    ULONG free_tree_reserve_return_max_node_writes_per_run;
    ULONG post_publish_reclaim_passes;
    ULONG max_post_publish_reclaim_passes_per_commit;
    ULONG superblock_publications;
    ULONG free_tree_reserve_return_run_node_writes;
    ULONG free_tree_reserve_return_batch_calls;
    ULONG free_tree_reserve_return_batch_blocks;
    ULONG free_tree_reserve_return_batch_node_writes;
    ULONG free_tree_reserve_return_skip_shape;
    ULONG free_tree_reserve_return_skip_small;
    ULONG free_tree_reserve_return_skip_emergency;
    ULONG free_tree_reserve_return_skip_capacity;
    ULONG sealed_commits;
    ULONG sealed_metadata_fences;
} bfs_perf_probe_snapshot_t;

#ifdef BFS_PERF_PROBE
struct bfs_fs;
extern bfs_perf_probe_snapshot_t bfs_perf_probe_counters;
extern ULONG bfs_perf_probe_data_depth;
extern struct bfs_fs *bfs_perf_probe_fs;
extern ULONG bfs_perf_probe_free_tree_phase;

enum bfs_perf_free_tree_phase {
    BFS_PERF_FREE_TREE_PHASE_OTHER,
    BFS_PERF_FREE_TREE_PHASE_ALLOCATION_BODY,
    BFS_PERF_FREE_TREE_PHASE_RESERVE_REFILL,
    BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN,
    BFS_PERF_FREE_TREE_PHASE_POST_PUBLISH_PENDING_RECLAIM
};

enum bfs_perf_io_kind {
    BFS_PERF_IO_READ,
    BFS_PERF_IO_WRITE,
    BFS_PERF_IO_UPDATE
};

void bfs_perf_probe_init(struct MsgPort *port);
void bfs_perf_probe_close(void);
void bfs_perf_probe_reset(void);
ULONG bfs_perf_probe_free_tree_phase_enter(enum bfs_perf_free_tree_phase phase);
void bfs_perf_probe_free_tree_phase_leave(ULONG previous_phase);
void bfs_perf_probe_reserve_return_call(void);
void bfs_perf_probe_reserve_return_run(ULONG blocks, ULONG node_writes);
void bfs_perf_probe_reserve_return_batch(ULONG blocks, ULONG node_writes);
void bfs_perf_probe_begin(struct EClockVal *start);
uint64_t bfs_perf_probe_elapsed(const struct EClockVal *start);
void bfs_perf_probe_end(enum bfs_perf_io_kind kind, BOOL data,
                        const struct EClockVal *start);
#endif

#endif /* BFS_AMIGA_PERF_PROBE_H */
