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
    char name[64];
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

static int run(const char *drive, BOOL deep_mode, BOOL probe_enabled)
{
    char path[128];
    struct timeval before, after;
    BPTR handle;
    ULONG index, pass, offset, elapsed;
    ULONG clock_hz = 0;
    const char *probe_phase;
    bfs_perf_probe_snapshot_t snapshot;

    if (!make_path(path, sizeof(path), drive, "perf")) return fail("path");
    handle = CreateDir(path);
    if (!handle) return fail("mkdir");
    UnLock(handle);

    if (deep_mode) emit("FS_DEEP_COMPARE\t4\nDRIVE\t");
    else emit("FS_COMPARE_BENCH\t1\nDRIVE\t");
    emit(drive);
    emit("\n");

    if (probe_enabled && !perf_reset(drive)) return fail("perf-reset-create");
    if (!clock_time(&before)) return fail("timer-create");
    for (index = 0; index < SMALL_COUNT; index++) {
        if (!small_path(path, sizeof(path), drive, index)) return fail("small-path");
        handle = Open(path, MODE_NEWFILE);
        if (!handle) return fail("small-open-write");
        if (Write(handle, expected, SMALL_BYTES) != SMALL_BYTES) {
            Close(handle);
            return fail("small-write");
        }
        {
            BOOL flushed = Flush(handle);
            BOOL closed = checked_close(handle);
            if (!flushed || !closed) return fail("small-close");
        }
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return fail("timer-create");
    if (probe_enabled) {
        probe_phase = capture_probe(drive, &snapshot, &clock_hz);
        if (probe_phase) return fail(probe_phase);
    }
    metric("SMALL_CREATE_40_US", elapsed);
    if (probe_enabled) emit_deep_counter_rows("SMALL_CREATE_40", &snapshot);

    if (probe_enabled && !perf_reset(drive)) return fail("perf-reset-lookup");
    if (!clock_time(&before)) return fail("timer-lookup");
    for (pass = 0; pass < 10; pass++) {
        for (index = 0; index < SMALL_COUNT; index++) {
            if (!small_path(path, sizeof(path), drive, index)) return fail("small-path");
            handle = Lock(path, SHARED_LOCK);
            if (!handle) return fail("small-lock");
            UnLock(handle);
        }
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return fail("timer-lookup");
    if (probe_enabled) {
        probe_phase = capture_probe(drive, &snapshot, &clock_hz);
        if (probe_phase) return fail(probe_phase);
    }
    metric("LOOKUP_400_US", elapsed);
    if (probe_enabled) emit_deep_counter_rows("LOOKUP_400", &snapshot);

    if (probe_enabled && !perf_reset(drive)) return fail("perf-reset-small-read");
    if (!clock_time(&before)) return fail("timer-small-read");
    for (index = 0; index < SMALL_COUNT; index++) {
        if (!small_path(path, sizeof(path), drive, index)) return fail("small-path");
        handle = Open(path, MODE_OLDFILE);
        if (!handle) return fail("small-open-read");
        if (Read(handle, received, SMALL_BYTES) != SMALL_BYTES ||
            !equal_bytes(expected, received, SMALL_BYTES) ||
            Read(handle, received, 1) != 0) {
            Close(handle);
            return fail("small-read-verify");
        }
        if (!checked_close(handle)) return fail("small-read-close");
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return fail("timer-small-read");
    if (probe_enabled) {
        probe_phase = capture_probe(drive, &snapshot, &clock_hz);
        if (probe_phase) return fail(probe_phase);
    }
    metric("SMALL_READ_40_US", elapsed);
    if (probe_enabled) emit_deep_counter_rows("SMALL_READ_40", &snapshot);

    if (!make_path(path, sizeof(path), drive, "perf_big")) return fail("path");
    if (probe_enabled && !perf_reset(drive)) return fail("perf-reset-large-write");
    if (!clock_time(&before)) return fail("timer-large-write");
    handle = Open(path, MODE_NEWFILE);
    if (!handle) return fail("large-open-write");
    for (offset = 0; offset < LARGE_BYTES; offset += BUFFER_BYTES) {
        if (Write(handle, expected, BUFFER_BYTES) != BUFFER_BYTES) {
            Close(handle);
            return fail("large-write");
        }
    }
    {
        BOOL flushed = Flush(handle);
        BOOL closed = checked_close(handle);
        if (!flushed || !closed) return fail("large-close");
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return fail("timer-large-write");
    if (probe_enabled) {
        probe_phase = capture_probe(drive, &snapshot, &clock_hz);
        if (probe_phase) return fail(probe_phase);
    }
    metric("SEQ_WRITE_8M_US", elapsed);
    if (probe_enabled) emit_deep_counter_rows("SEQ_WRITE_8M", &snapshot);

    if (probe_enabled && !perf_reset(drive)) return fail("perf-reset-large-read");
    if (!clock_time(&before)) return fail("timer-large-read");
    handle = Open(path, MODE_OLDFILE);
    if (!handle) return fail("large-open-read");
    for (offset = 0; offset < LARGE_BYTES; offset += BUFFER_BYTES) {
        if (Read(handle, received, BUFFER_BYTES) != BUFFER_BYTES ||
            !equal_bytes(expected, received, BUFFER_BYTES)) {
            Close(handle);
            return fail("large-read-verify");
        }
    }
    {
        LONG trailing = Read(handle, received, 1);
        BOOL closed = checked_close(handle);
        if (trailing != 0 || !closed) return fail("large-read-close");
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return fail("timer-large-read");
    if (probe_enabled) {
        probe_phase = capture_probe(drive, &snapshot, &clock_hz);
        if (probe_phase) return fail(probe_phase);
    }
    metric("SEQ_READ_8M_US", elapsed);
    if (probe_enabled) emit_deep_counter_rows("SEQ_READ_8M", &snapshot);

    if (probe_enabled && !perf_reset(drive)) return fail("perf-reset-delete");
    if (!clock_time(&before)) return fail("timer-delete");
    for (index = 0; index < SMALL_COUNT; index++) {
        if (!small_path(path, sizeof(path), drive, index)) return fail("small-path");
        if (!DeleteFile(path)) return fail("small-delete");
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed))
        return fail("timer-delete");
    if (probe_enabled) {
        probe_phase = capture_probe(drive, &snapshot, &clock_hz);
        if (probe_phase) return fail(probe_phase);
    }
    metric("SMALL_DELETE_40_US", elapsed);
    if (probe_enabled) {
        emit_deep_counter_rows("SMALL_DELETE_40", &snapshot);
        metric("CLOCK_HZ", clock_hz);
        metric("CRC_SAMPLE_STRIDE", BFS_PERF_CRC_SAMPLE_STRIDE);
    }
    emit("PASS\t1\n");
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    ULONG index;
    BOOL deep_mode = argc == 3 && argv[2] && text_equal(argv[2], "deep");
    BOOL probe_enabled;

    if ((argc != 2 && !deep_mode) || !argv[1] || !*argv[1] ||
        argv[1][text_length(argv[1]) - 1] != ':') {
        emit("Usage: fs-compare-bench DRIVE: [deep]\n");
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
        for (index = 0; index < BUFFER_BYTES; index++)
            expected[index] = (UBYTE)((index * 31UL + 17UL) & 0xff);
        result = run(argv[1], deep_mode, probe_enabled);
    }
    if (received) FreeVec(received);
    if (expected) FreeVec(expected);
    stop_timer();
    return result;
}
