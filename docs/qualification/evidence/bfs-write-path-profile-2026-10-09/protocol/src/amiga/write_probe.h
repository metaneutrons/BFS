/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_WRITE_PROBE_H
#define BFS_AMIGA_WRITE_PROBE_H

#include "perf_probe.h"

/* Optional sidecar: preserve the existing probe ABI and its pinned guests. */
#define BFS_ACTION_PERF_WRITE_READ 3013
#define BFS_WRITE_PROBE_VERSION 1
#define BFS_WRITE_PROBE_SAMPLE_STRIDE 1
#define BFS_WRITE_PROBE_SCOPES(X) \
    X(INODE_READ, inode_read) \
    X(INODE_WRITE, inode_write) \
    X(EXTENT_MAP, extent_map) \
    X(FREESPACE_GOAL, freespace_goal)

typedef struct bfs_write_probe_snapshot {
    ULONG version;
    ULONG size;
    ULONG clock_hz;
#define BFS_WRITE_PROBE_FIELDS(upper, lower) \
    ULONG lower##_calls; \
    ULONG lower##_samples; \
    uint64_t lower##_sample_ticks;
    BFS_WRITE_PROBE_SCOPES(BFS_WRITE_PROBE_FIELDS)
#undef BFS_WRITE_PROBE_FIELDS
} bfs_write_probe_snapshot_t;

#ifdef BFS_PERF_WRITE_DETAIL
#ifndef BFS_PERF_PROBE
#error "Write sidecar requires the disposable performance probe"
#endif
enum bfs_write_probe_scope {
    BFS_WRITE_PROBE_DISABLED,
#define BFS_WRITE_PROBE_ENUM(upper, lower) BFS_WRITE_PROBE_##upper,
    BFS_WRITE_PROBE_SCOPES(BFS_WRITE_PROBE_ENUM)
#undef BFS_WRITE_PROBE_ENUM
};
typedef struct bfs_write_probe_sample {
    struct EClockVal start;
    enum bfs_write_probe_scope scope;
} bfs_write_probe_sample_t;

extern bfs_write_probe_snapshot_t bfs_write_probe_counters;
bfs_write_probe_sample_t bfs_write_probe_begin(enum bfs_write_probe_scope scope);
void bfs_write_probe_end(const bfs_write_probe_sample_t *sample);
#endif

#endif /* BFS_AMIGA_WRITE_PROBE_H */
