/* SPDX-License-Identifier: MPL-2.0 */
/* Disposable FS-UAE-only I/O profiler. Not linked into production handlers. */

#include <exec/io.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <string.h>

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
