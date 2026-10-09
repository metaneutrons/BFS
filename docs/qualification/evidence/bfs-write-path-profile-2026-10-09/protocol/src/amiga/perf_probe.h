/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_PERF_PROBE_H
#define BFS_AMIGA_PERF_PROBE_H

#include <exec/types.h>
#include <devices/timer.h>
#include <stddef.h>
#include <stdint.h>

#define BFS_ACTION_PERF_RESET 3010
#define BFS_ACTION_PERF_READ  3011
#define BFS_PERF_PROBE_VERSION 16
#ifndef BFS_PERF_CRC_SAMPLE_STRIDE
#define BFS_PERF_CRC_SAMPLE_STRIDE 64u
#endif
#if BFS_PERF_CRC_SAMPLE_STRIDE != 1u && BFS_PERF_CRC_SAMPLE_STRIDE != 64u
#error "CRC probe supports only default64 or explicit all-call diagnostics"
#endif
#define BFS_PERF_CPU_SAMPLE_STRIDE 1u
#define BFS_PERF_DETAIL_SAMPLE_STRIDE 17u

/* Exact diagnostic calls, not elapsed-time or device-I/O estimates. A node
 * view counts node_view invocations; resident views count its validated-cache
 * branch, before the per-visit level/bounds checks. Search hints are separate. */
#define BFS_PERF_LOOKUP_COUNTERS(X) \
    X(INODE_READ_CALLS, inode_read_calls) \
    X(BTREE_SEARCH_CALLS, btree_search_calls) \
    X(BTREE_INDEX_HINT_HITS, btree_index_hint_hits) \
    X(BTREE_LEAF_HINT_HITS, btree_leaf_hint_hits) \
    X(CACHE_READ_CALLS, cache_read_calls) \
    X(CACHE_READ_HITS, cache_read_hits) \
    X(CACHE_READ_MISSES, cache_read_misses) \
    X(FREE_TREE_NODE_VIEWS, free_tree_node_views) \
    X(FREE_TREE_RESIDENT_VIEWS, free_tree_resident_views) \
    X(DIR_TREE_NODE_VIEWS, dir_tree_node_views) \
    X(DIR_TREE_RESIDENT_VIEWS, dir_tree_resident_views) \
    X(INODE_TREE_NODE_VIEWS, inode_tree_node_views) \
    X(INODE_TREE_RESIDENT_VIEWS, inode_tree_resident_views) \
    X(REFCOUNT_TREE_NODE_VIEWS, refcount_tree_node_views) \
    X(REFCOUNT_TREE_RESIDENT_VIEWS, refcount_tree_resident_views) \
    X(OTHER_TREE_NODE_VIEWS, other_tree_node_views) \
    X(OTHER_TREE_RESIDENT_VIEWS, other_tree_resident_views)

/* Expected-level partitions of node_view calls for the live DIR and INODE
 * trees. These are diagnostic call-site expectations, not validated node
 * level observations. */
#define BFS_PERF_NODE_LEVEL_COUNTERS(X) \
    X(DIR_TREE_LEAF_NODE_VIEWS, dir_tree_leaf_node_views) \
    X(DIR_TREE_LEAF_RESIDENT_VIEWS, dir_tree_leaf_resident_views) \
    X(DIR_TREE_INTERNAL_NODE_VIEWS, dir_tree_internal_node_views) \
    X(DIR_TREE_INTERNAL_RESIDENT_VIEWS, dir_tree_internal_resident_views) \
    X(INODE_TREE_LEAF_NODE_VIEWS, inode_tree_leaf_node_views) \
    X(INODE_TREE_LEAF_RESIDENT_VIEWS, inode_tree_leaf_resident_views) \
    X(INODE_TREE_INTERNAL_NODE_VIEWS, inode_tree_internal_node_views) \
    X(INODE_TREE_INTERNAL_RESIDENT_VIEWS, inode_tree_internal_resident_views)

/* One source of truth for the diagnostic CPU-scope ABI and guest schema. */
#define BFS_PERF_CPU_SCOPES(X) \
    X(PACKET, packet) \
    X(PACKET_OPEN, packet_open) \
    X(PACKET_READ, packet_read) \
    X(PACKET_WRITE, packet_write) \
    X(PACKET_END, packet_end) \
    X(PACKET_DELETE, packet_delete) \
    X(PACKET_FLUSH, packet_flush) \
    X(PACKET_OTHER, packet_other) \
    X(CORE_CREATE, core_create) \
    X(CORE_DELETE, core_delete) \
    X(CORE_FILE_WRITE, core_file_write) \
    X(CORE_SYNC, core_sync) \
    X(IFACE_FREE, iface_free) \
    X(SEAL_COMMIT, seal_commit)

/* Fine-grained, inclusive diagnostic intervals. Every 17th invocation is
 * timed: a prime stride avoids always selecting one side of root/leaf pairs.
 * Samples are not exclusive CPU time and must not be added across
 * nested scopes or extrapolated into a production speedup. */
#define BFS_PERF_DETAIL_SCOPES(X) \
    X(DETAIL_INODE_READ, detail_inode_read) \
    X(DETAIL_INODE_VALIDATE, detail_inode_validate) \
    X(DETAIL_INODE_SEARCH, detail_inode_search) \
    X(DETAIL_INODE_NODE_VIEW, detail_inode_node_view) \
    X(DETAIL_DIR_NODE_VIEW, detail_dir_node_view) \
    X(DETAIL_INODE_BINARY_SEARCH, detail_inode_binary_search) \
    X(DETAIL_DIR_BINARY_SEARCH, detail_dir_binary_search) \
    X(DETAIL_NODE_STRUCTURE, detail_node_structure) \
    X(DETAIL_CACHE_PEEK, detail_cache_peek) \
    X(DETAIL_BUFFER_ALLOC, detail_buffer_alloc) \
    X(DETAIL_BUFFER_FREE, detail_buffer_free) \
    X(DETAIL_EXALL_FILL, detail_exall_fill)

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
    /* Direct btree.c malloc/free requests, excluding unrelated core heap use. */
    ULONG btree_malloc_calls;
    ULONG btree_malloc_samples;
    // cppcheck-suppress unusedStructMember
    uint64_t btree_malloc_sample_ticks;
    ULONG btree_free_calls;
    ULONG btree_free_samples;
    // cppcheck-suppress unusedStructMember
    uint64_t btree_free_sample_ticks;
    /* Interface allocation requests made outside the recursive in_alloc path. */
    ULONG iface_alloc_calls;
    ULONG iface_alloc_samples;
    // cppcheck-suppress unusedStructMember
    uint64_t iface_alloc_sample_ticks;
    ULONG freespace_alloc_samples;
    // cppcheck-suppress unusedStructMember
    uint64_t freespace_alloc_sample_ticks;

#define BFS_PERF_CPU_SCOPE_FIELDS(upper, lower) \
    ULONG lower##_calls; \
    ULONG lower##_samples; \
    /* cppcheck-suppress unusedStructMember */ uint64_t lower##_sample_ticks;
    BFS_PERF_CPU_SCOPES(BFS_PERF_CPU_SCOPE_FIELDS)
#undef BFS_PERF_CPU_SCOPE_FIELDS
#define BFS_PERF_LOOKUP_FIELD(upper, lower) ULONG lower;
    BFS_PERF_LOOKUP_COUNTERS(BFS_PERF_LOOKUP_FIELD)
#undef BFS_PERF_LOOKUP_FIELD
#define BFS_PERF_DETAIL_SCOPE_FIELDS(upper, lower) \
    ULONG lower##_calls; \
    ULONG lower##_samples; \
    /* cppcheck-suppress unusedStructMember */ uint64_t lower##_sample_ticks;
    BFS_PERF_DETAIL_SCOPES(BFS_PERF_DETAIL_SCOPE_FIELDS)
#undef BFS_PERF_DETAIL_SCOPE_FIELDS
    /* Successful CRC finalizations preceding deferred write attempts. */
    ULONG deferred_node_writes;
#define BFS_PERF_NODE_LEVEL_FIELD(upper, lower) ULONG lower;
    BFS_PERF_NODE_LEVEL_COUNTERS(BFS_PERF_NODE_LEVEL_FIELD)
#undef BFS_PERF_NODE_LEVEL_FIELD
} bfs_perf_probe_snapshot_t;

#ifdef BFS_PERF_PROBE
struct bfs_fs;
struct bfs_btree;
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

enum bfs_perf_cpu_scope {
#define BFS_PERF_CPU_SCOPE_ENUM(upper, lower) BFS_PERF_CPU_SCOPE_##upper,
    BFS_PERF_CPU_SCOPES(BFS_PERF_CPU_SCOPE_ENUM)
#undef BFS_PERF_CPU_SCOPE_ENUM
};

enum bfs_perf_detail_scope {
    BFS_PERF_DETAIL_SCOPE_DISABLED,
#define BFS_PERF_DETAIL_SCOPE_ENUM(upper, lower) BFS_PERF_DETAIL_SCOPE_##upper,
    BFS_PERF_DETAIL_SCOPES(BFS_PERF_DETAIL_SCOPE_ENUM)
#undef BFS_PERF_DETAIL_SCOPE_ENUM
};

typedef struct bfs_perf_detail_sample {
    struct EClockVal start;
    enum bfs_perf_detail_scope scope;
    BOOL sampled;
} bfs_perf_detail_sample_t;

bfs_perf_detail_sample_t bfs_perf_probe_detail_begin(enum bfs_perf_detail_scope scope);
bfs_perf_detail_sample_t bfs_perf_probe_detail_tree_begin(
    const struct bfs_btree *tree, enum bfs_perf_detail_scope inode_scope,
    enum bfs_perf_detail_scope dir_scope);
void bfs_perf_probe_detail_end(const bfs_perf_detail_sample_t *sample);

void bfs_perf_probe_init(struct MsgPort *port);
void bfs_perf_probe_close(void);
void bfs_perf_probe_reset(void);
ULONG bfs_perf_probe_free_tree_phase_enter(enum bfs_perf_free_tree_phase phase);
void bfs_perf_probe_free_tree_phase_leave(ULONG previous_phase);
void bfs_perf_probe_reserve_return_call(void);
void bfs_perf_probe_reserve_return_run(ULONG blocks, ULONG node_writes);
void bfs_perf_probe_reserve_return_batch(ULONG blocks, ULONG node_writes);
void *bfs_perf_probe_btree_malloc(size_t size);
void bfs_perf_probe_btree_free(void *ptr);
void bfs_perf_probe_begin(struct EClockVal *start);
uint64_t bfs_perf_probe_elapsed(const struct EClockVal *start);
void bfs_perf_probe_end(enum bfs_perf_io_kind kind, BOOL data,
                        const struct EClockVal *start);
void bfs_perf_probe_cpu_scope_record(enum bfs_perf_cpu_scope scope,
                                     uint64_t ticks);
void bfs_perf_probe_node_view(const struct bfs_btree *tree, BOOL resident);
void bfs_perf_probe_node_view_at_level(const struct bfs_btree *tree,
                                      BOOL resident, uint16_t expected_level);
/* Node-write categories use the immutable record layout, for direct and
 * deferred finalization alike. Deferred callbacks do not retain tree pointers. */
void bfs_perf_probe_node_write(const void *layout, BOOL deferred);
#endif

#endif /* BFS_AMIGA_PERF_PROBE_H */
