/* SPDX-License-Identifier: MPL-2.0 */
/* Disposable FS-UAE-only I/O profiler. Not linked into production handlers. */

#include <exec/io.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <string.h>
#include <stdlib.h>

#include "perf_probe.h"
#include "bfs_fs.h"

struct Device *TimerBase;
bfs_perf_probe_snapshot_t bfs_perf_probe_counters;
ULONG bfs_perf_probe_data_depth;
struct bfs_fs *bfs_perf_probe_fs;
ULONG bfs_perf_probe_free_tree_phase;

static struct timerequest *probe_timer;
static BOOL timer_open;

void bfs_perf_probe_node_write(const void *layout, BOOL deferred)
{
    bfs_perf_probe_snapshot_t *c = &bfs_perf_probe_counters;
    c->btree_node_writes++;
    if (deferred) c->deferred_node_writes++;
    if (bfs_perf_probe_fs && layout == bfs_perf_probe_fs->freespace.tree.ops) {
        c->free_tree_node_writes++;
        switch (bfs_perf_probe_free_tree_phase) {
        case BFS_PERF_FREE_TREE_PHASE_ALLOCATION_BODY:
            c->free_tree_allocation_body_node_writes++; break;
        case BFS_PERF_FREE_TREE_PHASE_RESERVE_REFILL:
            c->free_tree_reserve_refill_node_writes++; break;
        case BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN:
            c->free_tree_reserve_return_node_writes++; break;
        case BFS_PERF_FREE_TREE_PHASE_POST_PUBLISH_PENDING_RECLAIM:
            c->free_tree_post_publish_pending_reclaim_node_writes++; break;
        case BFS_PERF_FREE_TREE_PHASE_OTHER:
        default:
            c->free_tree_other_node_writes++; break;
        }
    } else if (bfs_perf_probe_fs && layout == bfs_perf_probe_fs->dir_tree.tree.ops)
        c->dir_tree_node_writes++;
    else if (bfs_perf_probe_fs && layout == bfs_perf_probe_fs->inode_tree.ops)
        c->inode_tree_node_writes++;
    else if (bfs_perf_probe_fs && layout == bfs_perf_probe_fs->refcount.tree.ops)
        c->refcount_tree_node_writes++;
    else
        c->other_tree_node_writes++;
}

static void node_view_record(const struct bfs_btree *tree, BOOL resident,
                             BOOL has_expected_level, uint16_t expected_level)
{
    bfs_perf_probe_snapshot_t *c = &bfs_perf_probe_counters;
    if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->freespace.tree) {
        c->free_tree_node_views++;
        if (resident) c->free_tree_resident_views++;
    } else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->dir_tree.tree) {
        c->dir_tree_node_views++;
        if (resident) c->dir_tree_resident_views++;
        if (has_expected_level) {
            if (expected_level == 0) {
                c->dir_tree_leaf_node_views++;
                if (resident) c->dir_tree_leaf_resident_views++;
            } else {
                c->dir_tree_internal_node_views++;
                if (resident) c->dir_tree_internal_resident_views++;
            }
        }
    } else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->inode_tree) {
        c->inode_tree_node_views++;
        if (resident) c->inode_tree_resident_views++;
        if (has_expected_level) {
            if (expected_level == 0) {
                c->inode_tree_leaf_node_views++;
                if (resident) c->inode_tree_leaf_resident_views++;
            } else {
                c->inode_tree_internal_node_views++;
                if (resident) c->inode_tree_internal_resident_views++;
            }
        }
    } else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->refcount.tree) {
        c->refcount_tree_node_views++;
        if (resident) c->refcount_tree_resident_views++;
    } else {
        c->other_tree_node_views++;
        if (resident) c->other_tree_resident_views++;
    }
}

void bfs_perf_probe_node_view(const struct bfs_btree *tree, BOOL resident)
{
    node_view_record(tree, resident, FALSE, 0);
}

void bfs_perf_probe_node_view_at_level(const struct bfs_btree *tree,
                                      BOOL resident, uint16_t expected_level)
{
    node_view_record(tree, resident, TRUE, expected_level);
}

void bfs_perf_probe_init(struct MsgPort *port)
{
    if (!port) return;
    probe_timer = (struct timerequest *)CreateIORequest(port, sizeof(*probe_timer));
    if (!probe_timer) return;
    if (OpenDevice(TIMERNAME, UNIT_ECLOCK,
                   (struct IORequest *)probe_timer, 0) != 0) return;
    timer_open = TRUE;
    TimerBase = (struct Device *)probe_timer->tr_node.io_Device;
    {
        struct EClockVal initial;
        bfs_perf_probe_counters.clock_hz = ReadEClock(&initial);
    }
}

void bfs_perf_probe_close(void)
{
    if (probe_timer) {
        if (timer_open) CloseDevice((struct IORequest *)probe_timer);
        DeleteIORequest((struct IORequest *)probe_timer);
    }
    probe_timer = NULL;
    timer_open = FALSE;
    TimerBase = NULL;
    bfs_perf_probe_fs = NULL;
}

void bfs_perf_probe_reset(void)
{
    ULONG hz = bfs_perf_probe_counters.clock_hz;
    memset(&bfs_perf_probe_counters, 0, sizeof(bfs_perf_probe_counters));
    bfs_perf_probe_counters.clock_hz = hz;
    bfs_perf_probe_data_depth = 0;
    bfs_perf_probe_free_tree_phase = BFS_PERF_FREE_TREE_PHASE_OTHER;
    if (hz != 0) {
        uint64_t pair_ticks = 0;
        for (ULONG i = 0; i < 256; i++) {
            struct EClockVal started = {0};
            bfs_perf_probe_begin(&started);
            pair_ticks += bfs_perf_probe_elapsed(&started);
        }
        bfs_perf_probe_counters.clock_pair_ticks = (ULONG)(pair_ticks / 256);
    }
}

ULONG bfs_perf_probe_free_tree_phase_enter(enum bfs_perf_free_tree_phase phase)
{
    ULONG previous_phase = bfs_perf_probe_free_tree_phase;
    if (phase != BFS_PERF_FREE_TREE_PHASE_RESERVE_REFILL ||
        (previous_phase != BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN &&
         previous_phase != BFS_PERF_FREE_TREE_PHASE_POST_PUBLISH_PENDING_RECLAIM))
        bfs_perf_probe_free_tree_phase = (ULONG)phase;
    return previous_phase;
}

void bfs_perf_probe_free_tree_phase_leave(ULONG previous_phase)
{
    bfs_perf_probe_free_tree_phase = previous_phase;
}

void bfs_perf_probe_reserve_return_call(void)
{
    bfs_perf_probe_counters.free_tree_reserve_return_calls++;
}

void bfs_perf_probe_reserve_return_run(ULONG blocks, ULONG node_writes)
{
    bfs_perf_probe_snapshot_t *counters = &bfs_perf_probe_counters;
    if (blocks == 0) return;
    counters->free_tree_reserve_return_runs++;
    counters->free_tree_reserve_return_blocks += blocks;
    counters->free_tree_reserve_return_run_node_writes += node_writes;
    if (blocks == 1)
        counters->free_tree_reserve_return_runs_1_block++;
    else if (blocks <= 3)
        counters->free_tree_reserve_return_runs_2_3_blocks++;
    else if (blocks <= 7)
        counters->free_tree_reserve_return_runs_4_7_blocks++;
    else
        counters->free_tree_reserve_return_runs_8_plus_blocks++;
    if (blocks > counters->free_tree_reserve_return_max_run_blocks)
        counters->free_tree_reserve_return_max_run_blocks = blocks;
    if (node_writes > counters->free_tree_reserve_return_max_node_writes_per_run)
        counters->free_tree_reserve_return_max_node_writes_per_run = node_writes;
}

void bfs_perf_probe_reserve_return_batch(ULONG blocks, ULONG node_writes)
{
    bfs_perf_probe_snapshot_t *counters = &bfs_perf_probe_counters;
    if (blocks == 0) return;
    counters->free_tree_reserve_return_batch_calls++;
    counters->free_tree_reserve_return_batch_blocks += blocks;
    counters->free_tree_reserve_return_batch_node_writes += node_writes;
}

void bfs_perf_probe_begin(struct EClockVal *start)
{
    if (timer_open && bfs_perf_probe_counters.clock_hz != 0)
        (void)ReadEClock(start);
}

uint64_t bfs_perf_probe_elapsed(const struct EClockVal *start)
{
    struct EClockVal end;
    if (!timer_open || bfs_perf_probe_counters.clock_hz == 0) return 0;
    (void)ReadEClock(&end);
    return (((uint64_t)end.ev_hi << 32) | end.ev_lo) -
           (((uint64_t)start->ev_hi << 32) | start->ev_lo);
}

void bfs_perf_probe_cpu_scope_record(enum bfs_perf_cpu_scope scope,
                                     uint64_t ticks)
{
    switch (scope) {
#define BFS_PERF_CPU_SCOPE_RECORD_CASE(upper, lower) \
    case BFS_PERF_CPU_SCOPE_##upper: \
        bfs_perf_probe_counters.lower##_calls++; \
        bfs_perf_probe_counters.lower##_samples++; \
        bfs_perf_probe_counters.lower##_sample_ticks += ticks; \
        break;
        BFS_PERF_CPU_SCOPES(BFS_PERF_CPU_SCOPE_RECORD_CASE)
#undef BFS_PERF_CPU_SCOPE_RECORD_CASE
    }
}

bfs_perf_detail_sample_t bfs_perf_probe_detail_begin(enum bfs_perf_detail_scope scope)
{
    bfs_perf_detail_sample_t sample = {{0}, scope, FALSE};
    ULONG call = 0;
    switch (scope) {
#define BFS_PERF_DETAIL_SCOPE_CALL(upper, lower) \
    case BFS_PERF_DETAIL_SCOPE_##upper: \
        call = ++bfs_perf_probe_counters.lower##_calls; break;
        BFS_PERF_DETAIL_SCOPES(BFS_PERF_DETAIL_SCOPE_CALL)
#undef BFS_PERF_DETAIL_SCOPE_CALL
    case BFS_PERF_DETAIL_SCOPE_DISABLED: return sample;
    }
    sample.sampled = (call % BFS_PERF_DETAIL_SAMPLE_STRIDE) == 0;
    if (sample.sampled) bfs_perf_probe_begin(&sample.start);
    return sample;
}

bfs_perf_detail_sample_t bfs_perf_probe_detail_tree_begin(
    const struct bfs_btree *tree, enum bfs_perf_detail_scope inode_scope,
    enum bfs_perf_detail_scope dir_scope)
{
    enum bfs_perf_detail_scope scope = BFS_PERF_DETAIL_SCOPE_DISABLED;
    if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->inode_tree)
        scope = inode_scope;
    else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->dir_tree.tree)
        scope = dir_scope;
    return bfs_perf_probe_detail_begin(scope);
}

void bfs_perf_probe_detail_end(const bfs_perf_detail_sample_t *sample)
{
    if (!sample->sampled) return;
    uint64_t ticks = bfs_perf_probe_elapsed(&sample->start);
    switch (sample->scope) {
#define BFS_PERF_DETAIL_SCOPE_SAMPLE(upper, lower) \
    case BFS_PERF_DETAIL_SCOPE_##upper: \
        bfs_perf_probe_counters.lower##_samples++; \
        bfs_perf_probe_counters.lower##_sample_ticks += ticks; break;
        BFS_PERF_DETAIL_SCOPES(BFS_PERF_DETAIL_SCOPE_SAMPLE)
#undef BFS_PERF_DETAIL_SCOPE_SAMPLE
    case BFS_PERF_DETAIL_SCOPE_DISABLED: break;
    }
}

/* Direct B-tree buffer heap calls only. These diagnostic intervals include
 * the underlying allocator, but not caller-side buffer copies/initialization.
 * They may lie inside the inclusive allocation scopes; never add them to
 * those scopes as if they were disjoint elapsed time. */
void *bfs_perf_probe_btree_malloc(size_t size)
{
    struct EClockVal started = {0};
    bfs_perf_probe_counters.btree_malloc_calls++;
    bfs_perf_probe_begin(&started);
    void *result = malloc(size);
    uint64_t ticks = bfs_perf_probe_elapsed(&started);
    bfs_perf_probe_counters.btree_malloc_samples++;
    bfs_perf_probe_counters.btree_malloc_sample_ticks += ticks;
    return result;
}

void bfs_perf_probe_btree_free(void *pointer)
{
    struct EClockVal started = {0};
    bfs_perf_probe_counters.btree_free_calls++;
    bfs_perf_probe_begin(&started);
    free(pointer);
    uint64_t ticks = bfs_perf_probe_elapsed(&started);
    bfs_perf_probe_counters.btree_free_samples++;
    bfs_perf_probe_counters.btree_free_sample_ticks += ticks;
}

void bfs_perf_probe_end(enum bfs_perf_io_kind kind, BOOL data,
                        const struct EClockVal *start)
{
    uint64_t ticks = bfs_perf_probe_elapsed(start);
    switch (kind) {
    case BFS_PERF_IO_READ:
        bfs_perf_probe_counters.bio_read_ticks += ticks;
        if (data) bfs_perf_probe_counters.data_read_ticks += ticks;
        break;
    case BFS_PERF_IO_WRITE:
        bfs_perf_probe_counters.bio_write_ticks += ticks;
        if (data) bfs_perf_probe_counters.data_write_ticks += ticks;
        break;
    case BFS_PERF_IO_UPDATE:
        bfs_perf_probe_counters.bio_update_ticks += ticks;
        break;
    }
}
