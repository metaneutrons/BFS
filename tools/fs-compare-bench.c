/* SPDX-License-Identifier: MPL-2.0 */
/* Identical checked AmigaDOS workload for two filesystem handlers. */

#include <exec/memory.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

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

static void emit_number(ULONG value)
{
    char digits[11];
    unsigned int position = sizeof(digits);

    do {
        digits[--position] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    Write(Output(), (APTR)&digits[position], (LONG)(sizeof(digits) - position));
}

static void metric(const char *name, ULONG microseconds)
{
    emit(name);
    emit("\t");
    emit_number(microseconds);
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

static ULONG elapsed_us(const struct timeval *start, const struct timeval *end)
{
    return (ULONG)(end->tv_secs - start->tv_secs) * 1000000UL +
           (ULONG)(end->tv_micro - start->tv_micro);
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

static int run(const char *drive)
{
    char path[128];
    struct timeval before, after;
    BPTR handle;
    ULONG index, pass, offset;

    if (!make_path(path, sizeof(path), drive, "perf")) return fail("path");
    handle = CreateDir(path);
    if (!handle) return fail("mkdir");
    UnLock(handle);

    emit("FS_COMPARE_BENCH\t1\nDRIVE\t");
    emit(drive);
    emit("\n");

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
    if (!clock_time(&after)) return fail("timer-create");
    metric("SMALL_CREATE_40_US", elapsed_us(&before, &after));

    if (!clock_time(&before)) return fail("timer-lookup");
    for (pass = 0; pass < 10; pass++) {
        for (index = 0; index < SMALL_COUNT; index++) {
            if (!small_path(path, sizeof(path), drive, index)) return fail("small-path");
            handle = Lock(path, SHARED_LOCK);
            if (!handle) return fail("small-lock");
            UnLock(handle);
        }
    }
    if (!clock_time(&after)) return fail("timer-lookup");
    metric("LOOKUP_400_US", elapsed_us(&before, &after));

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
    if (!clock_time(&after)) return fail("timer-small-read");
    metric("SMALL_READ_40_US", elapsed_us(&before, &after));

    if (!make_path(path, sizeof(path), drive, "perf_big")) return fail("path");
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
    if (!clock_time(&after)) return fail("timer-large-write");
    metric("SEQ_WRITE_8M_US", elapsed_us(&before, &after));

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
    if (!clock_time(&after)) return fail("timer-large-read");
    metric("SEQ_READ_8M_US", elapsed_us(&before, &after));

    if (!clock_time(&before)) return fail("timer-delete");
    for (index = 0; index < SMALL_COUNT; index++) {
        if (!small_path(path, sizeof(path), drive, index)) return fail("small-path");
        if (!DeleteFile(path)) return fail("small-delete");
    }
    if (!clock_time(&after)) return fail("timer-delete");
    metric("SMALL_DELETE_40_US", elapsed_us(&before, &after));
    emit("PASS\t1\n");
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    ULONG index;

    if (argc != 2 || !argv[1] || !*argv[1] || argv[1][text_length(argv[1]) - 1] != ':') {
        emit("Usage: fs-compare-bench DRIVE:\n");
        return 20;
    }
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
        result = run(argv[1]);
    }
    if (received) FreeVec(received);
    if (expected) FreeVec(expected);
    stop_timer();
    return result;
}
