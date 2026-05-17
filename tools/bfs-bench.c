/*
 * BFS — Amiga Test & Benchmark Tool
 *
 * Usage:
 *   bfs-bench DH2: --test          Integrity test (write+read+verify)
 *   bfs-bench DH2: --bench         Performance benchmark
 *   bfs-bench DH1: DH2: --compare  PFS3 vs BFS comparison
 *
 * All data is generated deterministically (seed-based).
 * Integrity verified via XOR-rotate checksum.
 *
 * Cross-compile: m68k-amigaos-gcc -noixemul -m68020 -O2 -o bfs-bench bfs-bench.c
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/filehandler.h>

static int str_eq(const char *a, const char *b);
#include <proto/timer.h>

#define BUF_SIZE 65536  /* 64KB buffer */

/* ── Deterministic data generator ──────────────────────────── */

static void fill_buffer(UBYTE *buf, ULONG size, ULONG seed)
{
    ULONG state = seed;
    ULONG i;
    for (i = 0; i < size; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        buf[i] = (UBYTE)(state & 0xFF);
    }
}

/* ── XOR-rotate checksum ───────────────────────────────────── */

static ULONG checksum(UBYTE *buf, ULONG size)
{
    ULONG crc = 0;
    ULONG i;
    for (i = 0; i < size; i++) {
        crc = (crc << 1) | (crc >> 31);
        crc ^= buf[i];
    }
    return crc;
}

/* ── Timer ─────────────────────────────────────────────────── */

static struct Library *MyTimerBase;
static struct MsgPort *timer_port;
static struct timerequest *timer_req;

static BOOL open_timer(void)
{
    timer_port = CreateMsgPort();
    if (!timer_port) return FALSE;
    timer_req = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_req));
    if (!timer_req) return FALSE;
    if (OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)timer_req, 0))
        return FALSE;
    MyTimerBase = (struct Library *)timer_req->tr_node.io_Device;
    return TRUE;
}

static void close_timer(void)
{
    if (timer_req) {
        CloseDevice((struct IORequest *)timer_req);
        DeleteIORequest((struct IORequest *)timer_req);
    }
    if (timer_port) DeleteMsgPort(timer_port);
}

static ULONG get_ms(struct timeval *start, struct timeval *end)
{
    ULONG sec = end->tv_secs - start->tv_secs;
    LONG usec = end->tv_micro - start->tv_micro;
    return sec * 1000 + (usec / 1000);
}

static void get_time(struct timeval *tv)
{
    struct timerequest tr = *timer_req;
    tr.tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *)&tr);
    *tv = tr.tr_time;
}

/* ── Output helpers ────────────────────────────────────────── */

static BPTR out;


static void format_drive(const char *drive, const char *volname)
{
    char cmd[128];
    int i = 0, j = 0;
    const char *p = "C:Format DRIVE ";
    while (p[j]) cmd[i++] = p[j++];
    j = 0; while (drive[j]) cmd[i++] = drive[j++];
    p = " NAME "; j = 0; while (p[j]) cmd[i++] = p[j++];
    j = 0; while (volname[j]) cmd[i++] = volname[j++];
    p = " NOICONS QUICK"; j = 0; while (p[j]) cmd[i++] = p[j++];
    cmd[i] = 0;
    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    Execute((STRPTR)cmd, nil, nil);
    if (nil) Close(nil);
}

static void print(const char *s)
{
    Write(out, (APTR)s, strlen(s));
}

static void print_num(ULONG n)
{
    char buf[12];
    int i = 11;
    buf[i] = 0;
    if (n == 0) { buf[--i] = '0'; }
    else while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    print(&buf[i]);
}

static void print_hex(ULONG n)
{
    char buf[9];
    int i;
    static const char hex[] = "0123456789ABCDEF";
    for (i = 7; i >= 0; i--) { buf[i] = hex[n & 0xF]; n >>= 4; }
    buf[8] = 0;
    print(buf);
}

static ULONG strlen(const char *s) { ULONG n=0; while(*s++){n++;} return n; }

/* ── File operations ───────────────────────────────────────── */

/* Write a file with deterministic content, return checksum */
static ULONG write_file(const char *path, ULONG size, ULONG seed, UBYTE *buf)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    ULONG crc = 0, written = 0, chunk, block = 0;
    if (!fh) return 0;
    while (written < size) {
        chunk = size - written;
        if (chunk > BUF_SIZE) chunk = BUF_SIZE;
        fill_buffer(buf, chunk, seed + block);
        crc ^= checksum(buf, chunk);
        Write(fh, buf, chunk);
        written += chunk;
        block++;
    }
    Close(fh);
    return crc;
}

/* Read a file and compute checksum */
static ULONG read_file(const char *path, UBYTE *buf)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    ULONG crc = 0;
    LONG n;
    if (!fh) return 0;
    while ((n = Read(fh, buf, BUF_SIZE)) > 0)
        crc ^= checksum(buf, (ULONG)n);
    Close(fh);
    return crc;
}

/* ── Test mode ─────────────────────────────────────────────── */

typedef struct {
    const char *name;
    ULONG size;
    ULONG seed;
} file_spec_t;

static file_spec_t test_files[] = {
    {"tiny_001",   1024,    1}, {"tiny_002",   1024,    2},
    {"tiny_003",   1024,    3}, {"tiny_004",   1024,    4},
    {"tiny_005",   1024,    5}, {"tiny_006",   1024,    6},
    {"tiny_007",   1024,    7}, {"tiny_008",   1024,    8},
    {"tiny_009",   1024,    9}, {"tiny_010",   1024,   10},
    {"small_01",   8192, 1001}, {"small_02",   8192, 1002},
    {"small_03",   8192, 1003}, {"small_04",   8192, 1004},
    {"small_05",   8192, 1005},
    {"medium_01", 65536, 2001}, {"medium_02", 65536, 2002},
    {"medium_03", 65536, 2003},
    {"large_01", 1048576, 3001}, {"large_02", 1048576, 3002},
    {"huge_01",  4194304, 4001},
    {NULL, 0, 0}
};

static int run_test(const char *drive, UBYTE *buf)
{
    int pass = 0, fail = 0, i;
    char path[256];
    ULONG wcrc, rcrc;

    print("=== BFS Integrity Test ===\n");
    print("Drive: "); print(drive); print("\n\n");

    for (i = 0; test_files[i].name; i++) {
        /* Build path */
        int j = 0, k = 0;
        while (drive[k]) path[j++] = drive[k++];
        k = 0;
        while (test_files[i].name[k]) path[j++] = test_files[i].name[k++];
        path[j] = 0;

        /* Write */
        wcrc = write_file(path, test_files[i].size, test_files[i].seed, buf);

        /* Read back */
        rcrc = read_file(path, buf);

        /* Verify */
        print("TEST "); print(test_files[i].name);
        print(" size="); print_num(test_files[i].size);
        if (wcrc == rcrc && wcrc != 0) {
            print(" PASS crc="); print_hex(wcrc); print("\n");
            pass++;
        } else {
            print(" FAIL wcrc="); print_hex(wcrc);
            print(" rcrc="); print_hex(rcrc); print("\n");
            fail++;
        }
    }

    /* Cleanup */
    for (i = 0; test_files[i].name; i++) {
        int j = 0, k = 0;
        while (drive[k]) path[j++] = drive[k++];
        k = 0;
        while (test_files[i].name[k]) path[j++] = test_files[i].name[k++];
        path[j] = 0;
        DeleteFile((STRPTR)path);
    }

    print("\nSUMMARY "); print_num(pass); print("/");
    print_num(pass + fail); print(" PASS\n");
    return fail;
}

/* ── Bench mode ────────────────────────────────────────────── */

static void bench_one(const char *drive, const char *label, UBYTE *buf)
{
    struct timeval t0, t1;
    char path[256];
    int i, j, k;
    ULONG ms;

    print("--- "); print(label); print(" ("); print(drive); print(") ---\n");

    /* Create 100 tiny files */
    get_time(&t0);
    for (i = 0; i < 100; i++) {
        j = 0; k = 0;
        while (drive[k]) path[j++] = drive[k++];
        path[j++] = 'b'; path[j++] = '_';
        path[j++] = '0' + (i/100)%10;
        path[j++] = '0' + (i/10)%10;
        path[j++] = '0' + i%10;
        path[j] = 0;
        write_file(path, 1024, 5000 + i, buf);
    }
    get_time(&t1);
    ms = get_ms(&t0, &t1);
    print("BENCH create_100   "); print_num(ms); print("ms\n");

    /* Lookup 100 files (open+close) */
    get_time(&t0);
    for (i = 0; i < 100; i++) {
        j = 0; k = 0;
        while (drive[k]) path[j++] = drive[k++];
        path[j++] = 'b'; path[j++] = '_';
        path[j++] = '0' + (i/100)%10;
        path[j++] = '0' + (i/10)%10;
        path[j++] = '0' + i%10;
        path[j] = 0;
        BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
        if (lock) UnLock(lock);
    }
    get_time(&t1);
    ms = get_ms(&t0, &t1);
    print("BENCH lookup_100   "); print_num(ms); print("ms\n");

    /* Write 1MB file */
    j = 0; k = 0;
    while (drive[k]) path[j++] = drive[k++];
    path[j++]='b'; path[j++]='i'; path[j++]='g'; path[j]=0;
    get_time(&t0);
    write_file(path, 1048576, 9999, buf);
    get_time(&t1);
    ms = get_ms(&t0, &t1);
    print("BENCH write_1mb    "); print_num(ms); print("ms\n");

    /* Read 1MB file */
    get_time(&t0);
    read_file(path, buf);
    get_time(&t1);
    ms = get_ms(&t0, &t1);
    print("BENCH read_1mb     "); print_num(ms); print("ms\n");

    /* Delete all */
    get_time(&t0);
    for (i = 0; i < 100; i++) {
        j = 0; k = 0;
        while (drive[k]) path[j++] = drive[k++];
        path[j++] = 'b'; path[j++] = '_';
        path[j++] = '0' + (i/100)%10;
        path[j++] = '0' + (i/10)%10;
        path[j++] = '0' + i%10;
        path[j] = 0;
        DeleteFile((STRPTR)path);
    }
    DeleteFile((STRPTR)path); /* big file path still set from above... rebuild */
    j = 0; k = 0;
    while (drive[k]) path[j++] = drive[k++];
    path[j++]='b'; path[j++]='i'; path[j++]='g'; path[j]=0;
    DeleteFile((STRPTR)path);
    get_time(&t1);
    ms = get_ms(&t0, &t1);
    print("BENCH delete_101   "); print_num(ms); print("ms\n");

    print("\n");
}

/* ── Main ──────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    UBYTE *buf;
    int result = 0;

    out = Output();
    if (argc < 3) {
        print("Usage: bfs-bench <drive:> --test|--bench\n");
        print("       bfs-bench <drv1:> <drv2:> --compare\n");
        return 5;
    }

    buf = AllocMem(BUF_SIZE, MEMF_ANY);
    if (!buf) { print("ERROR: No memory\n"); return 20; }

    if (!open_timer()) { print("ERROR: No timer\n"); FreeMem(buf, BUF_SIZE); return 20; }

    if (argc >= 3 && str_eq(argv[2], "--test") == 0) {
        result = run_test(argv[1], buf);
    }
    else if (argc >= 3 && str_eq(argv[2], "--bench") == 0) {
        bench_one(argv[1], "Benchmark", buf);
    }
    else if (argc >= 4 && str_eq(argv[3], "--compare") == 0) {
        bench_one(argv[1], "Drive 1", buf);
        bench_one(argv[2], "Drive 2", buf);
    }
    else {
        print("Unknown mode. Use --test, --bench, or --compare\n");
        result = 5;
    }

    close_timer();
    FreeMem(buf, BUF_SIZE);
    return result;
}


static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}
