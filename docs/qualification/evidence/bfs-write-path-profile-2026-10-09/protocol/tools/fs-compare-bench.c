/* SPDX-License-Identifier: MPL-2.0 */
/* Identical checked AmigaDOS workload for two filesystem handlers. */

#include <exec/memory.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "perf_probe.h"
#include "write_probe.h"
#include "fs-compare-split.h"

#define SMALL_COUNT 40
#define SMALL_BYTES 1024
#define BUFFER_BYTES 65536
#define LARGE_BYTES (8UL * 1024UL * 1024UL)
#define LIST_PASSES 10
#define LIST_BUFFER_BYTES 4096
#define LIST_SEEN_WORDS 32
#define LIST_MAX_COUNT 1000
#define APPEND_BLOCK_BYTES 4096
#define APPEND_BLOCK_TOTAL (1024UL * 1024UL)
#define APPEND_SMALL_BYTES 1024
#define APPEND_SMALL_TOTAL (256UL * 1024UL)

static struct MsgPort *timer_port;
static struct timerequest *timer_request;
static BOOL timer_open;
static UBYTE *expected;
static UBYTE *received;
static const char *listing_directory;
static ULONG listing_expected_count;
static ULONG listing_pass_count;
static BOOL split_mode;
static BOOL split_write_mode;
static BOOL split_operations_active;
static BOOL split_timing_failed;
struct split_operation { ULONG calls; unsigned long long us; };
#define BFS_SPLIT_OPERATION_FIELD(UPPER, lower) struct split_operation lower;
static struct {
    BFS_SPLIT_OPS(BFS_SPLIT_OPERATION_FIELD)
} split_operations;
#undef BFS_SPLIT_OPERATION_FIELD

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

static BOOL write_probe_read(const char *drive, bfs_write_probe_snapshot_t *snapshot,
                             ULONG clock_hz)
{
    struct MsgPort *port = DeviceProc(drive);
    if (!port) return FALSE;
    snapshot->version = 0;
    snapshot->size = 0;
    return DoPkt(port, BFS_ACTION_PERF_WRITE_READ, (LONG)snapshot,
                 (LONG)sizeof(*snapshot), 0, 0, 0) != 0 &&
           snapshot->version == BFS_WRITE_PROBE_VERSION &&
           snapshot->size == (ULONG)sizeof(*snapshot) &&
           snapshot->clock_hz == clock_hz;
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
    char name[160];
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
#define BFS_EMIT_LOOKUP_COUNTER(UPPER, lower) \
    prefixed_metric(phase, #UPPER, snapshot->lower);
    BFS_PERF_LOOKUP_COUNTERS(BFS_EMIT_LOOKUP_COUNTER)
#undef BFS_EMIT_LOOKUP_COUNTER
#define BFS_EMIT_NODE_LEVEL_COUNTER(UPPER, lower) \
    prefixed_metric(phase, #UPPER, snapshot->lower);
    BFS_PERF_NODE_LEVEL_COUNTERS(BFS_EMIT_NODE_LEVEL_COUNTER)
#undef BFS_EMIT_NODE_LEVEL_COUNTER
#define BFS_EMIT_DETAIL_SCOPE(UPPER, lower) \
    prefixed_metric(phase, #UPPER "_CALLS", snapshot->lower##_calls); \
    prefixed_metric(phase, #UPPER "_SAMPLES", snapshot->lower##_samples); \
    prefixed_metric(phase, #UPPER "_SAMPLE_TICKS", \
                    (unsigned long long)snapshot->lower##_sample_ticks);
    BFS_PERF_DETAIL_SCOPES(BFS_EMIT_DETAIL_SCOPE)
#undef BFS_EMIT_DETAIL_SCOPE
    prefixed_metric(phase, "DEFERRED_NODE_WRITES", snapshot->deferred_node_writes);
}

/* Wrappers are defined before their workload-only aliases. Output writes and
 * fixture setup are never counted. These are inclusive DOS intervals, not
 * exclusive handler CPU time. Timer calls themselves perturb this diagnostic. */
static BOOL split_begin(struct timeval *before)
{
    if (!split_operations_active) return FALSE;
    if (!clock_time(before)) split_timing_failed = TRUE;
    return !split_timing_failed;
}

static void split_end(struct split_operation *operation,
                      const struct timeval *before, BOOL timed)
{
    struct timeval after;
    ULONG us;
    if (!split_operations_active) return;
    operation->calls++;
    if (!timed || !clock_time(&after) || !elapsed_us(before, &after, &us)) {
        split_timing_failed = TRUE;
        return;
    }
    operation->us += us;
}

static BPTR split_open(CONST_STRPTR name, LONG mode)
{
    struct timeval before;
    BOOL timed = split_begin(&before);
    BPTR result = Open(name, mode);
    split_end(&split_operations.open, &before, timed);
    return result;
}

static LONG split_read(BPTR handle, APTR buffer, LONG length)
{
    struct timeval before;
    BOOL timed = split_begin(&before);
    LONG result = Read(handle, buffer, length);
    split_end(&split_operations.read, &before, timed);
    return result;
}

static LONG split_write(BPTR handle, APTR buffer, LONG length)
{
    struct timeval before;
    BOOL timed = split_begin(&before);
    LONG result = Write(handle, buffer, length);
    split_end(&split_operations.write, &before, timed);
    return result;
}

static LONG split_flush(BPTR handle)
{
    struct timeval before;
    BOOL timed = split_begin(&before);
    LONG result = Flush(handle);
    split_end(&split_operations.handle_flush, &before, timed);
    return result;
}

static LONG split_close(BPTR handle)
{
    struct timeval before;
    BOOL timed = split_begin(&before);
    LONG result = Close(handle);
    split_end(&split_operations.close, &before, timed);
    return result;
}

static BOOL split_equal(const UBYTE *first, const UBYTE *second, ULONG length)
{
    struct timeval before;
    BOOL timed = split_begin(&before);
    BOOL result = equal_bytes(first, second, length);
    split_end(&split_operations.verify, &before, timed);
    return result;
}

#undef Open
#undef Read
#undef Write
#undef Flush
#undef Close
#define Open split_open
#define Read split_read
#define Write split_write
#define Flush split_flush
#define Close split_close
#define equal_bytes split_equal

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

static BOOL listing_path(char *path, ULONG capacity, const char *drive,
                         const char *directory, ULONG index)
{
    ULONG drive_length = text_length(drive);
    ULONG directory_length = text_length(directory);
    if (index >= LIST_MAX_COUNT || drive_length + directory_length + 6 > capacity)
        return FALSE;
    copy_bytes(path, drive, drive_length);
    copy_bytes(path + drive_length, directory, directory_length);
    path[drive_length + directory_length] = '/';
    path[drive_length + directory_length + 1] = 'f';
    path[drive_length + directory_length + 2] = (char)('0' + (index / 100) % 10);
    path[drive_length + directory_length + 3] = (char)('0' + (index / 10) % 10);
    path[drive_length + directory_length + 4] = (char)('0' + index % 10);
    path[drive_length + directory_length + 5] = '\0';
    return TRUE;
}

static BOOL checked_close(BPTR handle);

static const char *create_listing_fixture(const char *drive, const char *directory,
                                          ULONG expected_count)
{
    char path[128];
    ULONG index;
    BPTR lock;
    if (expected_count == 0 || expected_count > LIST_MAX_COUNT ||
        !make_path(path, sizeof(path), drive, directory)) return "listing-path";
    lock = CreateDir(path);
    if (!lock) return "listing-mkdir";
    UnLock(lock);
    for (index = 0; index < expected_count; index++) {
        BPTR handle;
        if (!listing_path(path, sizeof(path), drive, directory, index))
            return "listing-path";
        handle = Open(path, MODE_NEWFILE);
        if (!handle) return "listing-open";
        if (!checked_close(handle)) return "listing-close";
    }
    return NULL;
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

/* Mark an expected regular file fNNN; FALSE for wrong type/name or a repeat. */
static BOOL mark_listed(const char *name, LONG type, ULONG expected_count,
                        ULONG seen[LIST_SEEN_WORDS])
{
    ULONG index;
    if (type != ST_FILE || name[0] != 'f' || name[1] < '0' || name[1] > '9' || name[2] < '0' ||
        name[2] > '9' || name[3] < '0' || name[3] > '9' || name[4] != '\0')
        return FALSE;
    index = (ULONG)(name[1] - '0') * 100 + (ULONG)(name[2] - '0') * 10 +
            (ULONG)(name[3] - '0');
    if (index >= expected_count || (seen[index / 32] & (1UL << (index % 32))))
        return FALSE;
    seen[index / 32] |= 1UL << (index % 32);
    return TRUE;
}

/* Every pass returns each expected regular file exactly once. */
static const char *list_exnext_entries(const char *drive, const char *directory,
                                       ULONG expected_count, ULONG pass_count)
{
    char path[128];
    const char *error = NULL;
    ULONG pass;
    BPTR lock;
    struct FileInfoBlock *fib;
    if (!make_path(path, sizeof(path), drive, directory)) return "path";
    lock = Lock(path, SHARED_LOCK);
    if (!lock) return "list-lock";
    fib = AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return "list-allocation";
    }
    for (pass = 0; pass < pass_count && !error; pass++) {
        ULONG seen[LIST_SEEN_WORDS] = {0};
        ULONG count = 0;
        if (!Examine(lock, fib)) {
            error = "list-examine";
            break;
        }
        while (ExNext(lock, fib)) {
            if (!mark_listed(fib->fib_FileName, fib->fib_DirEntryType,
                             expected_count, seen)) {
                error = "list-exnext-name";
                break;
            }
            count++;
        }
        if (!error && (IoErr() != ERROR_NO_MORE_ENTRIES || count != expected_count))
            error = "list-exnext-count";
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return error;
}

static const char *list_exnext(const char *drive)
{
    return list_exnext_entries(drive, listing_directory, listing_expected_count,
                               listing_pass_count);
}

static const char *list_exall_entries(const char *drive, const char *directory,
                                      ULONG expected_count, ULONG pass_count)
{
    char path[128];
    const char *error = NULL;
    ULONG pass;
    BPTR lock;
    struct ExAllControl *control;
    if (!make_path(path, sizeof(path), drive, directory)) return "path";
    lock = Lock(path, SHARED_LOCK);
    if (!lock) return "list-lock";
    control = AllocDosObject(DOS_EXALLCONTROL, NULL);
    if (!control) {
        UnLock(lock);
        return "list-allocation";
    }
    for (pass = 0; pass < pass_count && !error; pass++) {
        ULONG seen[LIST_SEEN_WORDS] = {0};
        ULONG count = 0;
        BOOL more;
        control->eac_LastKey = 0;
        control->eac_MatchString = NULL;
        control->eac_MatchFunc = NULL;
        do {
            struct ExAllData *entry = (struct ExAllData *)received;
            ULONG index;
            more = ExAll(lock, entry, LIST_BUFFER_BYTES, ED_COMMENT, control);
            if (!more && IoErr() != ERROR_NO_MORE_ENTRIES) {
                error = "list-exall";
                break;
            }
            for (index = 0; index < control->eac_Entries; index++) {
                if (!entry || !mark_listed((const char *)entry->ed_Name,
                                           entry->ed_Type, expected_count, seen)) {
                    error = "list-exall-name";
                    break;
                }
                count++;
                entry = entry->ed_Next;
            }
        } while (more && !error);
        if (more) ExAllEnd(lock, (struct ExAllData *)received, LIST_BUFFER_BYTES,
                           ED_COMMENT, control);
        if (!error && count != expected_count) error = "list-exall-count";
    }
    FreeDosObject(DOS_EXALLCONTROL, control);
    UnLock(lock);
    return error;
}

static const char *list_exall(const char *drive)
{
    return list_exall_entries(drive, listing_directory, listing_expected_count,
                              listing_pass_count);
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

/* Grow a file by step bytes per Write, as copy tools with small buffers and
 * log writers do. The content repeats the expected pattern, so a reader can
 * verify it in whole buffers. */
static const char *append_file(const char *drive, const char *name, ULONG step,
                               ULONG total)
{
    char path[128];
    ULONG offset;
    BPTR handle;
    if (!make_path(path, sizeof(path), drive, name)) return "path";
    handle = Open(path, MODE_NEWFILE);
    if (!handle) return "append-open-write";
    for (offset = 0; offset < total; offset += step) {
        if (Write(handle, expected + offset % BUFFER_BYTES, (LONG)step) != (LONG)step) {
            Close(handle);
            return "append-write";
        }
    }
    {
        BOOL flushed = Flush(handle);
        BOOL closed = checked_close(handle);
        if (!flushed || !closed) return "append-close";
    }
    return NULL;
}

static const char *append_block_file(const char *drive)
{
    return append_file(drive, "perf_append4k", APPEND_BLOCK_BYTES, APPEND_BLOCK_TOTAL);
}

static const char *append_small_file(const char *drive)
{
    return append_file(drive, "perf_append1k", APPEND_SMALL_BYTES, APPEND_SMALL_TOTAL);
}

static const char *read_appended_file(const char *drive, const char *name, ULONG total)
{
    char path[128];
    ULONG offset;
    BPTR handle;
    if (!make_path(path, sizeof(path), drive, name)) return "path";
    handle = Open(path, MODE_OLDFILE);
    if (!handle) return "append-open-read";
    for (offset = 0; offset < total; offset += BUFFER_BYTES) {
        if (Read(handle, received, BUFFER_BYTES) != BUFFER_BYTES ||
            !equal_bytes(expected, received, BUFFER_BYTES)) {
            Close(handle);
            return "append-read-verify";
        }
    }
    {
        LONG trailing = Read(handle, received, 1);
        BOOL closed = checked_close(handle);
        if (trailing != 0 || !closed) return "append-read-close";
    }
    return NULL;
}

static const char *read_appended_files(const char *drive)
{
    const char *error = read_appended_file(drive, "perf_append4k", APPEND_BLOCK_TOTAL);
    if (error) return error;
    return read_appended_file(drive, "perf_append1k", APPEND_SMALL_TOTAL);
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

static void emit_split_counters(const char *phase, const char *stage,
                                const bfs_perf_probe_snapshot_t *snapshot,
                                const bfs_write_probe_snapshot_t *write_snapshot)
{
    char prefix[96];
    ULONG length = text_length(phase), stage_length = text_length(stage);
    copy_bytes(prefix, phase, length);
    prefix[length] = '_';
    copy_bytes(prefix + length + 1, stage, stage_length + 1);
#define BFS_EMIT_SPLIT(UPPER, lower) prefixed_metric(prefix, #UPPER, snapshot->lower);
    BFS_SPLIT_COUNTERS(BFS_EMIT_SPLIT)
#undef BFS_EMIT_SPLIT
#define BFS_EMIT_SPLIT_CPU(UPPER, lower) \
    prefixed_metric(prefix, #UPPER "_CALLS", snapshot->lower##_calls); \
    prefixed_metric(prefix, #UPPER "_SAMPLES", snapshot->lower##_samples); \
    prefixed_metric(prefix, #UPPER "_SAMPLE_TICKS", snapshot->lower##_sample_ticks);
    BFS_PERF_CPU_SCOPES(BFS_EMIT_SPLIT_CPU)
#undef BFS_EMIT_SPLIT_CPU
#define BFS_EMIT_SPLIT_LOOKUP(UPPER, lower) prefixed_metric(prefix, #UPPER, snapshot->lower);
    BFS_PERF_LOOKUP_COUNTERS(BFS_EMIT_SPLIT_LOOKUP)
#undef BFS_EMIT_SPLIT_LOOKUP
    if (split_write_mode) {
#define BFS_EMIT_WRITE_SCOPE(UPPER, lower) \
    prefixed_metric(prefix, "WRITE_DETAIL_" #UPPER "_CALLS", write_snapshot->lower##_calls); \
    prefixed_metric(prefix, "WRITE_DETAIL_" #UPPER "_SAMPLES", write_snapshot->lower##_samples); \
    prefixed_metric(prefix, "WRITE_DETAIL_" #UPPER "_SAMPLE_TICKS", write_snapshot->lower##_sample_ticks);
        BFS_WRITE_PROBE_SCOPES(BFS_EMIT_WRITE_SCOPE)
#undef BFS_EMIT_WRITE_SCOPE
#define BFS_EMIT_EXISTING_SCOPE(UPPER, lower) \
    prefixed_metric(prefix, #UPPER "_CALLS", snapshot->lower##_calls); \
    prefixed_metric(prefix, #UPPER "_SAMPLES", snapshot->lower##_samples); \
    prefixed_metric(prefix, #UPPER "_SAMPLE_TICKS", snapshot->lower##_sample_ticks);
        BFS_SPLIT_WRITE_HEAP_SCOPES(BFS_EMIT_EXISTING_SCOPE)
        BFS_PERF_DETAIL_SCOPES(BFS_EMIT_EXISTING_SCOPE)
#undef BFS_EMIT_EXISTING_SCOPE
#define BFS_EMIT_WRITE_COUNTER(UPPER, lower) prefixed_metric(prefix, #UPPER, snapshot->lower);
        BFS_SPLIT_WRITE_COUNTERS(BFS_EMIT_WRITE_COUNTER)
#undef BFS_EMIT_WRITE_COUNTER
    }
}

static const char *run_split_phase(const char *drive, const char *phase,
                                   BOOL probe_enabled, BOOL flush_after,
                                   ULONG *clock_hz, workload_fn workload)
{
    struct timeval before, after;
    /* The CLI guest has a small default stack. Snapshots are serial scratch
     * storage, not nested stack frames. Clear the absent-flush record afresh. */
    static bfs_perf_probe_snapshot_t work, flush;
    static bfs_write_probe_snapshot_t write_work, write_flush;
    ULONG work_us, flush_us = 0;
    ULONG index;
    const char *error;
    for (index = 0; index < sizeof(flush); index++) ((UBYTE *)&flush)[index] = 0;
    for (index = 0; index < sizeof(write_flush); index++) ((UBYTE *)&write_flush)[index] = 0;
#define BFS_RESET_SPLIT(UPPER, lower) \
    split_operations.lower.calls = 0; split_operations.lower.us = 0;
    BFS_SPLIT_OPS(BFS_RESET_SPLIT)
#undef BFS_RESET_SPLIT
    split_timing_failed = FALSE;
    if (probe_enabled && !perf_reset(drive)) return "split-reset-work";
    if (!clock_time(&before)) return "split-clock-work";
    split_operations_active = TRUE;
    error = workload(drive);
    split_operations_active = FALSE;
    if (error) return error;
    if (split_timing_failed || !clock_time(&after) ||
        !elapsed_us(&before, &after, &work_us)) return "split-clock-work";
    if (probe_enabled) {
        error = capture_probe(drive, &work, clock_hz);
        if (error) return error;
        if (split_write_mode && !write_probe_read(drive, &write_work, *clock_hz))
            return "write-probe-work";
    }
    if (flush_after) {
        if (probe_enabled && !perf_reset(drive)) return "split-reset-flush";
        if (!clock_time(&before)) return "split-clock-flush";
        error = flush_volume(drive);
        if (error) return error;
        if (!clock_time(&after) || !elapsed_us(&before, &after, &flush_us))
            return "split-clock-flush";
        if (probe_enabled) {
            error = capture_probe(drive, &flush, clock_hz);
            if (error) return error;
            if (split_write_mode && !write_probe_read(drive, &write_flush, *clock_hz))
                return "write-probe-flush";
        }
    }
    /* Reset/snapshot gaps are excluded. This sum is not the production
     * whole-phase timer and must not be used as a production speed claim. */
    prefixed_metric(phase, "US", (unsigned long long)work_us + flush_us);
    prefixed_metric(phase, "WORK_US", work_us);
    prefixed_metric(phase, "VOLUME_FLUSH_US", flush_us);
    prefixed_metric(phase, "HAS_VOLUME_FLUSH", flush_after ? 1 : 0);
#define BFS_EMIT_SPLIT_OP(UPPER, lower) \
    prefixed_metric(phase, "GUEST_" #UPPER "_CALLS", split_operations.lower.calls); \
    prefixed_metric(phase, "GUEST_" #UPPER "_US", split_operations.lower.us);
    BFS_SPLIT_OPS(BFS_EMIT_SPLIT_OP)
#undef BFS_EMIT_SPLIT_OP
    if (probe_enabled) {
        emit_split_counters(phase, "WORK", &work, &write_work);
        emit_split_counters(phase, "VOLUME_FLUSH", &flush, &write_flush);
    }
    return NULL;
}

static const char *run_phase(const char *drive, const char *phase,
                             const char *reset_error, const char *timer_error,
                             BOOL probe_enabled, BOOL flush_after, ULONG *clock_hz,
                             workload_fn workload)
{
    struct timeval before, after;
    bfs_perf_probe_snapshot_t snapshot;
    ULONG elapsed;
    const char *error;
    if (split_mode)
        return run_split_phase(drive, phase, probe_enabled, flush_after,
                               clock_hz, workload);
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

static const char *run_listing_pair(const char *drive, const char *directory,
                                   ULONG expected_count, const char *first_phase,
                                   const char *repeat_phase, workload_fn workload,
                                   BOOL probe_enabled, ULONG *clock_hz)
{
    const char *error;
    listing_directory = directory;
    listing_expected_count = expected_count;
    listing_pass_count = 1;
    error = run_phase(drive, first_phase, "perf-reset-list", "timer-list-first",
                      probe_enabled, FALSE, clock_hz, workload);
    if (error) return error;
    listing_pass_count = LIST_PASSES;
    return run_phase(drive, repeat_phase, "perf-reset-list", "timer-list-repeat",
                     probe_enabled, FALSE, clock_hz, workload);
}

static const char *run_listing_schema4(const char *drive, BOOL probe_enabled,
                                       ULONG *clock_hz)
{
    struct listing_fixture {
        const char *directory;
        ULONG expected_count;
        const char *first_phase;
        const char *repeat_phase;
        workload_fn workload;
    };
    static const struct listing_fixture fixtures[] = {
        {"perf_exnext_40", 40, "LIST_EXNEXT_40_ENTRIES_FIRST_PASS",
         "LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL", list_exnext},
        {"perf_exall_40", 40, "LIST_EXALL_40_ENTRIES_FIRST_PASS",
         "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL", list_exall},
        {"perf_exnext_400", 400, "LIST_EXNEXT_400_ENTRIES_FIRST_PASS",
         "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL", list_exnext},
        {"perf_exall_400", 400, "LIST_EXALL_400_ENTRIES_FIRST_PASS",
         "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", list_exall},
        {"perf_exnext_1000", 1000, "LIST_EXNEXT_1000_ENTRIES_FIRST_PASS",
         "LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL", list_exnext},
        {"perf_exall_1000", 1000, "LIST_EXALL_1000_ENTRIES_FIRST_PASS",
         "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL", list_exall},
    };
    ULONG index;
    const char *error;

    /* Setup is intentionally outside all listing timers. Creating these
     * files may warm metadata; host/device cache state is not controlled. */
    for (index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); index++) {
        error = create_listing_fixture(drive, fixtures[index].directory,
                                       fixtures[index].expected_count);
        if (error) return error;
    }
    error = flush_volume(drive);
    if (error) return error;

    for (index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); index++) {
        error = run_listing_pair(drive, fixtures[index].directory,
                                 fixtures[index].expected_count,
                                 fixtures[index].first_phase,
                                 fixtures[index].repeat_phase,
                                 fixtures[index].workload, probe_enabled,
                                 clock_hz);
        if (error) return error;
    }
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
    if (split_write_mode) emit("FS_SPLIT_COMPARE\t2\nDRIVE\t");
    else if (split_mode) emit("FS_SPLIT_COMPARE\t1\nDRIVE\t");
    else if (deep_mode) emit("FS_DEEP_COMPARE\t15\nDRIVE\t");
    else if (durable_mode) emit("FS_DURABLE_COMPARE\t4\nDRIVE\t");
    else emit("FS_COMPARE_BENCH\t4\nDRIVE\t");
    emit(drive);
    emit("\n");
    if (split_mode) {
        metric("DURABLE_MODE", durable_mode ? 1 : 0);
        metric("PROBE_ENABLED", probe_enabled ? 1 : 0);
    }
    error = run_phase(drive, "SMALL_CREATE_40", "perf-reset-create", "timer-create",
                      probe_enabled, durable_mode, &clock_hz, create_small_files);
    if (error) return fail(error);
    error = run_phase(drive, "LOOKUP_400", "perf-reset-lookup", "timer-lookup",
                      probe_enabled, FALSE, &clock_hz, lookup_small_files);
    if (error) return fail(error);
    error = run_phase(drive, "SMALL_READ_40", "perf-reset-small-read", "timer-small-read",
                      probe_enabled, FALSE, &clock_hz, read_small_files);
    if (error) return fail(error);
    /* Legacy small-directory listing rows remain in the normal compare output.
     * Deep schemas 12–15 profile scaled fixtures after the six established
     * phases. */
    if (!deep_mode) {
        listing_directory = "perf";
        listing_expected_count = SMALL_COUNT;
        listing_pass_count = LIST_PASSES;
        error = run_phase(drive, "LIST_EXNEXT_400", "perf-reset-list", "timer-list-exnext",
                          split_mode && probe_enabled, FALSE, &clock_hz, list_exnext);
        if (error) return fail(error);
        error = run_phase(drive, "LIST_EXALL_400", "perf-reset-list", "timer-list-exall",
                          split_mode && probe_enabled, FALSE, &clock_hz, list_exall);
        if (error) return fail(error);
    }
    error = run_phase(drive, "SEQ_WRITE_8M", "perf-reset-large-write", "timer-large-write",
                      probe_enabled, durable_mode, &clock_hz, write_large_file);
    if (error) return fail(error);
    error = run_phase(drive, "SEQ_READ_8M", "perf-reset-large-read", "timer-large-read",
                      probe_enabled, FALSE, &clock_hz, read_large_file);
    if (error) return fail(error);
    error = run_phase(drive, "SMALL_DELETE_40", "perf-reset-delete", "timer-delete",
                      probe_enabled, durable_mode, &clock_hz, delete_small_files);
    if (error) return fail(error);
    /* Schema 3 appends growth in small steps after the earlier phases, so
     * those run on the same volume state as before. Deep schemas 12–15 repeat
     * those workloads with the internal probe enabled. */
    error = run_phase(drive, "APPEND_4K_1M", "perf-reset-append", "timer-append-4k",
                      probe_enabled, durable_mode, &clock_hz, append_block_file);
    if (error) return fail(error);
    error = run_phase(drive, "APPEND_1K_256K", "perf-reset-append", "timer-append-1k",
                      probe_enabled, durable_mode, &clock_hz, append_small_file);
    if (error) return fail(error);
    error = run_phase(drive, "APPEND_READ_1280K", "perf-reset-append",
                      "timer-append-read", probe_enabled, FALSE, &clock_hz,
                      read_appended_files);
    if (error) return fail(error);
    error = run_listing_schema4(drive, probe_enabled, &clock_hz);
    if (error) return fail(error);
    if (split_mode) {
        metric("PROBE_VERSION", probe_enabled ? BFS_PERF_PROBE_VERSION : 0);
        metric("CLOCK_HZ", probe_enabled ? clock_hz : 0);
        metric("CRC_SAMPLE_STRIDE", probe_enabled ? BFS_PERF_CRC_SAMPLE_STRIDE : 0);
        metric("CPU_SAMPLE_STRIDE", probe_enabled ? BFS_PERF_CPU_SAMPLE_STRIDE : 0);
        if (split_write_mode) {
            metric("WRITE_PROBE_VERSION", probe_enabled ? BFS_WRITE_PROBE_VERSION : 0);
            metric("WRITE_SAMPLE_STRIDE", probe_enabled ? BFS_WRITE_PROBE_SAMPLE_STRIDE : 0);
            metric("DETAIL_SAMPLE_STRIDE", probe_enabled ? BFS_PERF_DETAIL_SAMPLE_STRIDE : 0);
        }
    } else if (probe_enabled) {
        metric("CLOCK_HZ", clock_hz);
        metric("CRC_SAMPLE_STRIDE", BFS_PERF_CRC_SAMPLE_STRIDE);
        metric("CPU_SAMPLE_STRIDE", BFS_PERF_CPU_SAMPLE_STRIDE);
        metric("DETAIL_SAMPLE_STRIDE", BFS_PERF_DETAIL_SAMPLE_STRIDE);
    }
    emit("PASS\t1\n");
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    BOOL deep_mode = argc == 3 && argv[2] && text_equal(argv[2], "deep");
    BOOL durable_mode = argc == 3 && argv[2] &&
        (text_equal(argv[2], "durable") || text_equal(argv[2], "split-durable") ||
         text_equal(argv[2], "split-write-durable"));
    BOOL probe_enabled;
    split_mode = argc == 3 && argv[2] &&
        (text_equal(argv[2], "split") || text_equal(argv[2], "split-durable") ||
         text_equal(argv[2], "split-write") || text_equal(argv[2], "split-write-durable"));
    split_write_mode = argc == 3 && argv[2] &&
        (text_equal(argv[2], "split-write") || text_equal(argv[2], "split-write-durable"));

    if ((argc != 2 && !deep_mode && !durable_mode && !split_mode) || !argv[1] || !*argv[1] ||
        argv[1][text_length(argv[1]) - 1] != ':') {
        emit("Usage: fs-compare-bench DRIVE: [deep|durable|split|split-durable|split-write|split-write-durable]\n");
        return 20;
    }
    probe_enabled = (deep_mode || split_mode) && text_equal(argv[1], "DH1:");
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
