/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_PERF_PROBE_H
#define BFS_AMIGA_PERF_PROBE_H

#include <exec/types.h>
#include <devices/timer.h>
#include <stdint.h>

#define BFS_ACTION_PERF_RESET 3010
#define BFS_ACTION_PERF_READ  3011
#define BFS_PERF_PROBE_VERSION 5
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
} bfs_perf_probe_snapshot_t;

#ifdef BFS_PERF_PROBE
struct bfs_fs;
extern bfs_perf_probe_snapshot_t bfs_perf_probe_counters;
extern ULONG bfs_perf_probe_data_depth;
extern struct bfs_fs *bfs_perf_probe_fs;

enum bfs_perf_io_kind {
    BFS_PERF_IO_READ,
    BFS_PERF_IO_WRITE,
    BFS_PERF_IO_UPDATE
};

void bfs_perf_probe_init(struct MsgPort *port);
void bfs_perf_probe_close(void);
void bfs_perf_probe_reset(void);
void bfs_perf_probe_begin(struct EClockVal *start);
uint64_t bfs_perf_probe_elapsed(const struct EClockVal *start);
void bfs_perf_probe_end(enum bfs_perf_io_kind kind, BOOL data,
                        const struct EClockVal *start);
#endif

#endif /* BFS_AMIGA_PERF_PROBE_H */
