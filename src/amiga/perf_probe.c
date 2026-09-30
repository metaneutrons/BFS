/* SPDX-License-Identifier: MPL-2.0 */
/* Disposable FS-UAE-only I/O profiler. Not linked into production handlers. */

#include <exec/io.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <string.h>
#include <stdlib.h>

#include "perf_probe.h"

struct Device *TimerBase;
bfs_perf_probe_snapshot_t bfs_perf_probe_counters;
ULONG bfs_perf_probe_data_depth;
struct bfs_fs *bfs_perf_probe_fs;
ULONG bfs_perf_probe_free_tree_phase;

static struct timerequest *probe_timer;
static BOOL timer_open;

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
