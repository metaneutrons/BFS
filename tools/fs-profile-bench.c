/* SPDX-License-Identifier: MPL-2.0 */
/* Phase timings for an 8 MiB sequential AmigaDOS write workload. */

#include <exec/memory.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#define BUFFER_BYTES 65536UL
#define FILE_BYTES (8UL * 1024UL * 1024UL)
#define WRITE_COUNT (FILE_BYTES / BUFFER_BYTES)

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

static void emit_uquad(unsigned long long value)
{
    char digits[20];
    unsigned int position = sizeof(digits);

    do {
        digits[--position] = (char)('0' + value % 10ULL);
        value /= 10ULL;
    } while (value != 0ULL && position != 0);
    Write(Output(), (APTR)&digits[position], (LONG)(sizeof(digits) - position));
}

static void metric(const char *name, unsigned long long microseconds)
{
    emit(name);
    emit("\t");
    emit_uquad(microseconds);
    emit("\n");
}

static int fail(const char *phase)
{
    emit("FAIL\t");
    emit(phase);
    emit("\t");
    emit_uquad((unsigned long long)(ULONG)IoErr());
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
                      unsigned long long *elapsed)
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
    *elapsed = (unsigned long long)seconds * 1000000ULL +
               (unsigned long long)(ULONG)microseconds;
    return TRUE;
}

static BOOL make_path(char *path, ULONG capacity, const char *drive, const char *suffix)
{
    ULONG drive_length = text_length(drive);
    ULONG suffix_length = text_length(suffix);
    if (drive_length + suffix_length + 1 > capacity) return FALSE;
    copy_bytes(path, drive, drive_length);
    copy_bytes(path + drive_length, suffix, suffix_length + 1);
    return TRUE;
}

static BOOL valid_drive(const char *drive)
{
    ULONG length = text_length(drive);
    ULONG index;

    if (length < 2 || drive[length - 1] != ':') return FALSE;
    for (index = 0; index < length; index++) {
        if (drive[index] == '\t' || drive[index] == '\r' || drive[index] == '\n')
            return FALSE;
    }
    return TRUE;
}

static BOOL timed_writes(BPTR handle, unsigned long long *duration,
                         const char **failure_phase)
{
    struct timeval before, after;
    unsigned long long total = 0ULL;
    unsigned long long elapsed;
    ULONG index;

    for (index = 0; index < WRITE_COUNT; index++) {
        LONG written;
        if (!clock_time(&before)) {
            *failure_phase = "timer-write";
            return FALSE;
        }
        written = Write(handle, expected, (LONG)BUFFER_BYTES);
        if (!clock_time(&after) || !elapsed_us(&before, &after, &elapsed)) {
            *failure_phase = "timer-write";
            return FALSE;
        }
        total += elapsed;
        if (written != (LONG)BUFFER_BYTES) {
            *failure_phase = "write";
            return FALSE;
        }
    }
    *duration = total;
    return TRUE;
}

static BOOL timed_flush(BPTR handle, unsigned long long *duration,
                        const char **failure_phase)
{
    struct timeval before, after;
    BOOL flushed;

    if (!clock_time(&before)) {
        *failure_phase = "timer-flush";
        return FALSE;
    }
    flushed = Flush(handle);
    if (!clock_time(&after) || !elapsed_us(&before, &after, duration)) {
        *failure_phase = "timer-flush";
        return FALSE;
    }
    if (!flushed) {
        *failure_phase = "flush";
        return FALSE;
    }
    return TRUE;
}

static BOOL timed_close(BPTR handle, unsigned long long *duration,
                        const char **failure_phase)
{
    struct timeval before, after;
    BOOL close_started = clock_time(&before);
    BOOL closed = Close(handle) != 0;

    if (!close_started || !clock_time(&after) || !elapsed_us(&before, &after, duration)) {
        *failure_phase = "timer-close";
        return FALSE;
    }
    if (!closed) {
        *failure_phase = "close";
        return FALSE;
    }
    return TRUE;
}

static BOOL write_pass(BPTR handle, const char *write_metric,
                       const char *flush_metric, const char *close_metric,
                       const char **failure_phase)
{
    unsigned long long write_us, flush_us, close_us;
    BOOL writes_ok = timed_writes(handle, &write_us, failure_phase);
    BOOL flush_ok = FALSE;
    BOOL close_ok;

    if (!writes_ok) {
        Close(handle);
        return FALSE;
    }
    metric(write_metric, write_us);
    flush_ok = timed_flush(handle, &flush_us, failure_phase);
    if (flush_ok) metric(flush_metric, flush_us);
    close_ok = timed_close(handle, &close_us, failure_phase);
    if (close_ok) metric(close_metric, close_us);
    return flush_ok && close_ok;
}

static int run(const char *drive)
{
    char path[128];
    BPTR handle, lock;
    struct timeval before, after;
    unsigned long long read_us;
    ULONG offset;
    const char *phase;

    if (!make_path(path, sizeof(path), drive, "fsprofile.bin")) return fail("path");
    lock = Lock(path, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return fail("fresh-exists");
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND) return fail("fresh-check");

    handle = Open(path, MODE_NEWFILE);
    if (!handle) return fail("fresh-open");
    phase = "fresh-write";
    if (!write_pass(handle, "FRESH_WRITE_US", "FRESH_FLUSH_US",
                    "FRESH_CLOSE_US", &phase)) return fail(phase);

    handle = Open(path, MODE_READWRITE);
    if (!handle) return fail("overwrite-open");
    if (Seek(handle, 0, OFFSET_BEGINNING) < 0) {
        Close(handle);
        return fail("overwrite-seek");
    }
    phase = "overwrite-write";
    if (!write_pass(handle, "OVERWRITE_WRITE_US", "OVERWRITE_FLUSH_US",
                    "OVERWRITE_CLOSE_US", &phase)) return fail(phase);

    handle = Open(path, MODE_OLDFILE);
    if (!handle) return fail("read-open");
    if (!clock_time(&before)) {
        Close(handle);
        return fail("timer-read");
    }
    for (offset = 0; offset < FILE_BYTES; offset += BUFFER_BYTES) {
        if (Read(handle, received, (LONG)BUFFER_BYTES) != (LONG)BUFFER_BYTES ||
            !equal_bytes(expected, received, BUFFER_BYTES)) {
            Close(handle);
            return fail("read-verify");
        }
    }
    if (Read(handle, received, 1) != 0) {
        Close(handle);
        return fail("read-eof");
    }
    if (!clock_time(&after) || !elapsed_us(&before, &after, &read_us)) {
        Close(handle);
        return fail("timer-read");
    }
    if (Close(handle) == 0) return fail("read-close");
    metric("READ_VERIFY_8M_US", read_us);

    emit("PASS\t1\n");
    return 0;
}

int main(int argc, char **argv)
{
    ULONG index;
    int result;

    emit("FS_PROFILE_BENCH\t1\n");
    if (argc != 2 || !argv[1] || !*argv[1]) return fail("usage");
    if (!valid_drive(argv[1])) return fail("drive");
    emit("DRIVE\t");
    emit(argv[1]);
    emit("\n");
    if (!start_timer()) {
        emit("FAIL\ttimer-open\t0\n");
        stop_timer();
        return 20;
    }
    expected = AllocVec(BUFFER_BYTES, MEMF_ANY);
    received = AllocVec(BUFFER_BYTES, MEMF_ANY);
    if (!expected || !received) {
        result = fail("allocation");
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
