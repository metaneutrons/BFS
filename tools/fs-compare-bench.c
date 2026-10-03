/* SPDX-License-Identifier: MPL-2.0 */
/* Identical checked AmigaDOS workload for two filesystem handlers. */

#include <exec/memory.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "perf_probe.h"

#define SMALL_COUNT 40
#define SMALL_BYTES 1024
#define BUFFER_BYTES 65536
#define LARGE_BYTES (8UL * 1024UL * 1024UL)

static struct MsgPort *timer_port;
static struct timerequest *timer_request;
static BOOL timer_open;
static UBYTE *expected;
static UBYTE *received;

static ULONG text_length(const char *value)
{
    ULONG length = 0;
    while (value[length]) length++;
    return length;
}

static BOOL text_equal(const char *first, const char *second)
{
    ULONG index = 0;
    while (first[index] && second[index]) {
        if (first[index] != second[index]) return FALSE;
        index++;
    }
    return first[index] == second[index];
}

static void copy_bytes(void *destination, const void *source, ULONG length)
{
    UBYTE *target = destination;
    const UBYTE *input = source;
    ULONG index;
    for (index = 0; index < length; index++) target[index] = input[index];
}

static BOOL equal_bytes(const UBYTE *first, const UBYTE *second, ULONG length)
{
    ULONG index;
    for (index = 0; index < length; index++)
        if (first[index] != second[index]) return FALSE;
    return TRUE;
}

static void emit(const char *value)
{
    Write(Output(), (APTR)value, (LONG)text_length(value));
}

static void emit_number(unsigned long long value)
{
    char digits[20];
    unsigned int position = sizeof(digits);

    do {
        digits[--position] = (char)('0' + value % 10ULL);
        value /= 10ULL;
    } while (value != 0ULL && position != 0);
    Write(Output(), (APTR)&digits[position], (LONG)(sizeof(digits) - position));
}

static void metric(const char *name, unsigned long long value)
{
    emit(name);
    emit("\t");
    emit_number(value);
    emit("\n");
}

static int fail(const char *phase)
{
    emit("FAIL\t");
    emit(phase);
    emit("\t");
    emit_number((ULONG)IoErr());
    emit("\n");
    return 20;
}

static BOOL start_timer(void)
{
    timer_port = CreateMsgPort();
    if (!timer_port) return FALSE;
    timer_request = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_request));
    if (!timer_request) return FALSE;
    timer_open = OpenDevice("timer.device", UNIT_MICROHZ,
                            (struct IORequest *)timer_request, 0) == 0;
    return timer_open;
}

static void stop_timer(void)
{
    if (timer_request) {
        if (timer_open)
            CloseDevice((struct IORequest *)timer_request);
        DeleteIORequest((struct IORequest *)timer_request);
    }
    if (timer_port) DeleteMsgPort(timer_port);
}

static BOOL clock_time(struct timeval *value)
{
    struct timerequest request = *timer_request;
    request.tr_node.io_Command = TR_GETSYSTIME;
    if (DoIO((struct IORequest *)&request) != 0) return FALSE;
    *value = request.tr_time;
    return TRUE;
}

static BOOL elapsed_us(const struct timeval *start, const struct timeval *end,
                       ULONG *elapsed)
{
    ULONG seconds;
    LONG microseconds;

    if (end->tv_secs < start->tv_secs) return FALSE;
    seconds = end->tv_secs - start->tv_secs;
    microseconds = (LONG)end->tv_micro - (LONG)start->tv_micro;
    if (microseconds < 0) {
        if (seconds == 0) return FALSE;
        seconds--;
        microseconds += 1000000L;
    }
    *elapsed = seconds * 1000000UL + (ULONG)microseconds;
    return TRUE;
}

static BOOL perf_reset(const char *drive)
{
    struct MsgPort *port = DeviceProc(drive);
    if (!port) return FALSE;
    return DoPkt(port, BFS_ACTION_PERF_RESET, 0, 0, 0, 0, 0) != 0;
}

static BOOL perf_read(const char *drive, bfs_perf_probe_snapshot_t *snapshot)
{
    struct MsgPort *port = DeviceProc(drive);
    if (!port) return FALSE;
    snapshot->version = 0;
    snapshot->size = 0;
    if (!DoPkt(port, BFS_ACTION_PERF_READ, (LONG)snapshot,
               (LONG)sizeof(*snapshot), 0, 0, 0)) return FALSE;
    return snapshot->version == BFS_PERF_PROBE_VERSION &&
           snapshot->size == (ULONG)sizeof(*snapshot);
}

static const char *capture_probe(const char *drive, bfs_perf_probe_snapshot_t *snapshot,
                                 ULONG *clock_hz)
{
    if (!perf_read(drive, snapshot)) return "perf-read";
    if (snapshot->clock_hz == 0) return "perf-clock-hz";
    *clock_hz = snapshot->clock_hz;
    return NULL;
}

static void prefixed_metric(const char *prefix, const char *suffix,
                            unsigned long long value)
{
    char name[96];
    ULONG prefix_length = text_length(prefix);
    ULONG suffix_length = text_length(suffix);

    if (prefix_length + suffix_length + 2 > sizeof(name)) return;
    copy_bytes(name, prefix, prefix_length);
    name[prefix_length] = '_';
    copy_bytes(name + prefix_length + 1, suffix, suffix_length);
    name[prefix_length + suffix_length + 1] = '\0';
    metric(name, value);
}

static void emit_deep_counter_rows(const char *phase,
                                  const bfs_perf_probe_snapshot_t *snapshot)
{
    prefixed_metric(phase, "BIO_READS", snapshot->bio_read_calls);
    prefixed_metric(phase, "BIO_WRITES", snapshot->bio_write_calls);
    prefixed_metric(phase, "BIO_UPDATES", snapshot->bio_update_calls);
    prefixed_metric(phase, "DATA_READS", snapshot->data_read_calls);
    prefixed_metric(phase, "DATA_WRITES", snapshot->data_write_calls);
    prefixed_metric(phase, "NODE_WRITES", snapshot->btree_node_writes);
    prefixed_metric(phase, "TXN_COMMITS", snapshot->txn_commit_calls);
    prefixed_metric(phase, "FREESPACE_ALLOCS", snapshot->freespace_alloc_calls);
    prefixed_metric(phase, "EXTENT_MAPS", snapshot->extent_map_calls);
    prefixed_metric(phase, "READ_TICKS", (unsigned long long)snapshot->bio_read_ticks);
    prefixed_metric(phase, "WRITE_TICKS", (unsigned long long)snapshot->bio_write_ticks);
    prefixed_metric(phase, "UPDATE_TICKS", (unsigned long long)snapshot->bio_update_ticks);
    prefixed_metric(phase, "DATA_READ_TICKS", (unsigned long long)snapshot->data_read_ticks);
    prefixed_metric(phase, "DATA_WRITE_TICKS", (unsigned long long)snapshot->data_write_ticks);
    prefixed_metric(phase, "FREE_TREE_NODE_WRITES", snapshot->free_tree_node_writes);
    prefixed_metric(phase, "FREE_TREE_ALLOCATION_BODY_NODE_WRITES",
                    snapshot->free_tree_allocation_body_node_writes);
    prefixed_metric(phase, "FREE_TREE_RESERVE_REFILL_NODE_WRITES",
                    snapshot->free_tree_reserve_refill_node_writes);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_NODE_WRITES",
                    snapshot->free_tree_reserve_return_node_writes);
    prefixed_metric(phase, "FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES",
                    snapshot->free_tree_post_publish_pending_reclaim_node_writes);
    prefixed_metric(phase, "FREE_TREE_OTHER_NODE_WRITES",
                    snapshot->free_tree_other_node_writes);
    prefixed_metric(phase, "DIR_TREE_NODE_WRITES", snapshot->dir_tree_node_writes);
    prefixed_metric(phase, "INODE_TREE_NODE_WRITES", snapshot->inode_tree_node_writes);
    prefixed_metric(phase, "REFCOUNT_TREE_NODE_WRITES", snapshot->refcount_tree_node_writes);
    prefixed_metric(phase, "OTHER_TREE_NODE_WRITES", snapshot->other_tree_node_writes);
    prefixed_metric(phase, "NODE_CRC_READ_CALLS", snapshot->node_crc_read_calls);
    prefixed_metric(phase, "NODE_CRC_READ_SAMPLES", snapshot->node_crc_read_samples);
    prefixed_metric(phase, "NODE_CRC_READ_SAMPLE_TICKS",
                    (unsigned long long)snapshot->node_crc_read_sample_ticks);
    prefixed_metric(phase, "NODE_CRC_WRITE_CALLS", snapshot->node_crc_write_calls);
    prefixed_metric(phase, "NODE_CRC_WRITE_SAMPLES", snapshot->node_crc_write_samples);
    prefixed_metric(phase, "NODE_CRC_WRITE_SAMPLE_TICKS",
                    (unsigned long long)snapshot->node_crc_write_sample_ticks);
    prefixed_metric(phase, "CLOCK_PAIR_TICKS", snapshot->clock_pair_ticks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_CALLS",
                    snapshot->free_tree_reserve_return_calls);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_RUNS",
                    snapshot->free_tree_reserve_return_runs);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_BLOCKS",
                    snapshot->free_tree_reserve_return_blocks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK",
                    snapshot->free_tree_reserve_return_runs_1_block);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS",
                    snapshot->free_tree_reserve_return_runs_2_3_blocks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS",
                    snapshot->free_tree_reserve_return_runs_4_7_blocks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS",
                    snapshot->free_tree_reserve_return_runs_8_plus_blocks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS",
                    snapshot->free_tree_reserve_return_max_run_blocks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN",
                    snapshot->free_tree_reserve_return_max_node_writes_per_run);
    prefixed_metric(phase, "POST_PUBLISH_RECLAIM_PASSES",
                    snapshot->post_publish_reclaim_passes);
    prefixed_metric(phase, "MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT",
                    snapshot->max_post_publish_reclaim_passes_per_commit);
    prefixed_metric(phase, "SUPERBLOCK_PUBLICATIONS",
                    snapshot->superblock_publications);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES",
                    snapshot->free_tree_reserve_return_run_node_writes);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
                    snapshot->free_tree_reserve_return_batch_calls);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS",
                    snapshot->free_tree_reserve_return_batch_blocks);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES",
                    snapshot->free_tree_reserve_return_batch_node_writes);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
                    snapshot->free_tree_reserve_return_skip_shape);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_SKIP_SMALL",
                    snapshot->free_tree_reserve_return_skip_small);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY",
                    snapshot->free_tree_reserve_return_skip_emergency);
    prefixed_metric(phase, "FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY",
                    snapshot->free_tree_reserve_return_skip_capacity);
    prefixed_metric(phase, "SEALED_COMMITS", snapshot->sealed_commits);
    prefixed_metric(phase, "SEALED_METADATA_FENCES",
                    snapshot->sealed_metadata_fences);
    prefixed_metric(phase, "BTREE_MALLOC_CALLS", snapshot->btree_malloc_calls);
    prefixed_metric(phase, "BTREE_MALLOC_SAMPLES", snapshot->btree_malloc_samples);
    prefixed_metric(phase, "BTREE_MALLOC_SAMPLE_TICKS",
                    (unsigned long long)snapshot->btree_malloc_sample_ticks);
    prefixed_metric(phase, "BTREE_FREE_CALLS", snapshot->btree_free_calls);
    prefixed_metric(phase, "BTREE_FREE_SAMPLES", snapshot->btree_free_samples);
    prefixed_metric(phase, "BTREE_FREE_SAMPLE_TICKS",
                    (unsigned long long)snapshot->btree_free_sample_ticks);
    prefixed_metric(phase, "IFACE_ALLOC_CALLS", snapshot->iface_alloc_calls);
    prefixed_metric(phase, "IFACE_ALLOC_SAMPLES", snapshot->iface_alloc_samples);
    prefixed_metric(phase, "IFACE_ALLOC_SAMPLE_TICKS",
                    (unsigned long long)snapshot->iface_alloc_sample_ticks);
    prefixed_metric(phase, "FREESPACE_ALLOC_SAMPLES",
                    snapshot->freespace_alloc_samples);
    prefixed_metric(phase, "FREESPACE_ALLOC_SAMPLE_TICKS",
                    (unsigned long long)snapshot->freespace_alloc_sample_ticks);
#define BFS_EMIT_CPU_SCOPE(UPPER, lower) \
    prefixed_metric(phase, #UPPER "_CALLS", snapshot->lower##_calls); \
    prefixed_metric(phase, #UPPER "_SAMPLES", snapshot->lower##_samples); \
    prefixed_metric(phase, #UPPER "_SAMPLE_TICKS", \
                    (unsigned long long)snapshot->lower##_sample_ticks);
    BFS_PERF_CPU_SCOPES(BFS_EMIT_CPU_SCOPE)
#undef BFS_EMIT_CPU_SCOPE
}

static BOOL make_path(char *path, ULONG capacity, const char *drive, const char *suffix)
{
    ULONG drive_len = text_length(drive);
    ULONG suffix_len = text_length(suffix);
    if (drive_len + suffix_len + 1 > capacity) return FALSE;
    copy_bytes(path, drive, drive_len);
    copy_bytes(path + drive_len, suffix, suffix_len + 1);
    return TRUE;
}

static BOOL small_path(char *path, ULONG capacity, const char *drive, ULONG index)
{
    ULONG length = text_length(drive);
    if (index >= SMALL_COUNT || length + 10 > capacity) return FALSE;
    copy_bytes(path, drive, length);
    copy_bytes(path + length, "perf/f", 6);
    path[length + 6] = (char)('0' + (index / 100) % 10);
    path[length + 7] = (char)('0' + (index / 10) % 10);
    path[length + 8] = (char)('0' + index % 10);
    path[length + 9] = '\0';
    return TRUE;
}

static BOOL checked_close(BPTR handle)
{
    return Close(handle) != 0;
}

static const char *create_small_files(const char *drive)
{
    char path[128];
    ULONG index;
    for (index = 0; index < SMALL_COUNT; index++) {
        BPTR handle;
        if (!small_path(path, sizeof(path), drive, index)) return "small-path";
        handle = Open(path, MODE_NEWFILE);
        if (!handle) return "small-open-write";
        if (Write(handle, expected, SMALL_BYTES) != SMALL_BYTES) {
            Close(handle);
            return "small-write";
        }
        {
            BOOL flushed = Flush(handle);
            BOOL closed = checked_close(handle);
            if (!flushed || !closed) return "small-close";
        }
    }
    return NULL;
}

static const char *lookup_small_files(const char *drive)
{
    char path[128];
    ULONG index, pass;
    for (pass = 0; pass < 10; pass++) {
        for (index = 0; index < SMALL_COUNT; index++) {
            BPTR handle;
            if (!small_path(path, sizeof(path), drive, index)) return "small-path";
            handle = Lock(path, SHARED_LOCK);
            if (!handle) return "small-lock";
            UnLock(handle);
        }
    }
    return NULL;
}

static const char *read_small_files(const char *drive)
{
    char path[128];
    ULONG index;
    for (index = 0; index < SMALL_COUNT; index++) {
        BPTR handle;
        if (!small_path(path, sizeof(path), drive, index)) return "small-path";
        handle = Open(path, MODE_OLDFILE);
        if (!handle) return "small-open-read";
        if (Read(handle, received, SMALL_BYTES) != SMALL_BYTES ||
            !equal_bytes(expected, received, SMALL_BYTES) ||
            Read(handle, received, 1) != 0) {
            Close(handle);
            return "small-read-verify";
        }
        if (!checked_close(handle)) return "small-read-close";
    }
    return NULL;
}

static const char *write_large_file(const char *drive)
{
    char path[128];
    ULONG offset;
    BPTR handle;
    if (!make_path(path, sizeof(path), drive, "perf_big")) return "path";
    handle = Open(path, MODE_NEWFILE);
    if (!handle) return "large-open-write";
    for (offset = 0; offset < LARGE_BYTES; offset += BUFFER_BYTES) {
        if (Write(handle, expected, BUFFER_BYTES) != BUFFER_BYTES) {
            Close(handle);
            return "large-write";
        }
    }
    {
        BOOL flushed = Flush(handle);
        BOOL closed = checked_close(handle);
        if (!flushed || !closed) return "large-close";
    }
    return NULL;
}

static const char *read_large_file(const char *drive)
{
    char path[128];
    ULONG offset;
    BPTR handle;
    if (!make_path(path, sizeof(path), drive, "perf_big")) return "path";
    handle = Open(path, MODE_OLDFILE);
    if (!handle) return "large-open-read";
    for (offset = 0; offset < LARGE_BYTES; offset += BUFFER_BYTES) {
        if (Read(handle, received, BUFFER_BYTES) != BUFFER_BYTES ||
            !equal_bytes(expected, received, BUFFER_BYTES)) {
            Close(handle);
            return "large-read-verify";
        }
    }
    {
        LONG trailing = Read(handle, received, 1);
        BOOL closed = checked_close(handle);
        if (trailing != 0 || !closed) return "large-read-close";
    }
    return NULL;
}

static const char *delete_small_files(const char *drive)
{
    char path[128];
    ULONG index;
    for (index = 0; index < SMALL_COUNT; index++) {
        if (!small_path(path, sizeof(path), drive, index)) return "small-path";
        if (!DeleteFile(path)) return "small-delete";
    }
    return NULL;
}

/* Ask the handler to commit pending state, so a durable phase also measures
 * each filesystem's deferred commit work (for PFS3, its timed UpdateDisk). */
static const char *flush_volume(const char *drive)
{
    struct MsgPort *port = DeviceProc(drive);
    if (!port) return "flush-port";
    return DoPkt(port, ACTION_FLUSH, 0, 0, 0, 0, 0) ? NULL : "flush";
}

typedef const char *(*workload_fn)(const char *drive);

static const char *run_phase(const char *drive, const char *phase,
                             const char *reset_error, const char *timer_error,
                             BOOL probe_enabled, BOOL flush_after, ULONG *clock_hz,
                             workload_fn workload)
{
    struct timeval before, after;
    bfs_perf_probe_snapshot_t snapshot;
    ULONG elapsed;
    const char *error;
    if (probe_enabled && !perf_reset(drive)) return reset_error;
    if (!clock_time(&before)) return timer_error;
    error = workload(drive);
    if (error) return error;
    if (flush_after) {
        error = flush_volume(drive);
        if (error) return error;
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return timer_error;
    if (probe_enabled) {
        error = capture_probe(drive, &snapshot, clock_hz);
        if (error) return error;
    }
    prefixed_metric(phase, "US", elapsed);
    if (probe_enabled) emit_deep_counter_rows(phase, &snapshot);
    return NULL;
}

static int run(const char *drive, BOOL deep_mode, BOOL durable_mode,
               BOOL probe_enabled)
{
    char path[128];
    BPTR handle;
    ULONG clock_hz = 0;
    const char *error;
    if (!make_path(path, sizeof(path), drive, "perf")) return fail("path");
    handle = CreateDir(path);
    if (!handle) return fail("mkdir");
    UnLock(handle);
    if (deep_mode) emit("FS_DEEP_COMPARE\t11\nDRIVE\t");
    else if (durable_mode) emit("FS_DURABLE_COMPARE\t1\nDRIVE\t");
    else emit("FS_COMPARE_BENCH\t1\nDRIVE\t");
    emit(drive);
    emit("\n");
    error = run_phase(drive, "SMALL_CREATE_40", "perf-reset-create", "timer-create",
                      probe_enabled, durable_mode, &clock_hz, create_small_files);
    if (error) return fail(error);
    error = run_phase(drive, "LOOKUP_400", "perf-reset-lookup", "timer-lookup",
                      probe_enabled, FALSE, &clock_hz, lookup_small_files);
    if (error) return fail(error);
    error = run_phase(drive, "SMALL_READ_40", "perf-reset-small-read", "timer-small-read",
                      probe_enabled, FALSE, &clock_hz, read_small_files);
    if (error) return fail(error);
    error = run_phase(drive, "SEQ_WRITE_8M", "perf-reset-large-write", "timer-large-write",
                      probe_enabled, durable_mode, &clock_hz, write_large_file);
    if (error) return fail(error);
    error = run_phase(drive, "SEQ_READ_8M", "perf-reset-large-read", "timer-large-read",
                      probe_enabled, FALSE, &clock_hz, read_large_file);
    if (error) return fail(error);
    error = run_phase(drive, "SMALL_DELETE_40", "perf-reset-delete", "timer-delete",
                      probe_enabled, durable_mode, &clock_hz, delete_small_files);
    if (error) return fail(error);
    if (probe_enabled) {
        metric("CLOCK_HZ", clock_hz);
        metric("CRC_SAMPLE_STRIDE", BFS_PERF_CRC_SAMPLE_STRIDE);
        metric("CPU_SAMPLE_STRIDE", BFS_PERF_CPU_SAMPLE_STRIDE);
    }
    emit("PASS\t1\n");
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    BOOL deep_mode = argc == 3 && argv[2] && text_equal(argv[2], "deep");
    BOOL durable_mode = argc == 3 && argv[2] && text_equal(argv[2], "durable");
    BOOL probe_enabled;

    if ((argc != 2 && !deep_mode && !durable_mode) || !argv[1] || !*argv[1] ||
        argv[1][text_length(argv[1]) - 1] != ':') {
        emit("Usage: fs-compare-bench DRIVE: [deep|durable]\n");
        return 20;
    }
    probe_enabled = deep_mode && text_equal(argv[1], "DH1:");
    if (!start_timer()) {
        emit("FAIL\ttimer-open\n");
        stop_timer();
        return 20;
    }
    expected = AllocVec(BUFFER_BYTES, MEMF_ANY);
    received = AllocVec(BUFFER_BYTES, MEMF_ANY);
    if (!expected || !received) {
        emit("FAIL\tallocation\n");
        result = 20;
    } else {
        ULONG index;
        for (index = 0; index < BUFFER_BYTES; index++)
            expected[index] = (UBYTE)((index * 31UL + 17UL) & 0xff);
        result = run(argv[1], deep_mode, durable_mode, probe_enabled);
    }
    if (received) FreeVec(received);
    if (expected) FreeVec(expected);
    stop_timer();
    return result;
}
