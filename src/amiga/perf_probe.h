/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_PERF_PROBE_H
#define BFS_AMIGA_PERF_PROBE_H

#include <exec/types.h>

#define BFS_ACTION_PERF_RESET 3010
#define BFS_ACTION_PERF_READ  3011
#define BFS_PERF_PROBE_VERSION 1

typedef struct bfs_perf_probe_snapshot {
    ULONG version;
    ULONG size;
    ULONG bio_read_calls;
    ULONG bio_write_calls;
    ULONG bio_update_calls;
    ULONG freespace_alloc_calls;
    ULONG extent_map_calls;
} bfs_perf_probe_snapshot_t;

#ifdef BFS_PERF_PROBE
extern bfs_perf_probe_snapshot_t bfs_perf_probe_counters;
#endif

#endif /* BFS_AMIGA_PERF_PROBE_H */
