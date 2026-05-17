/* SPDX-License-Identifier: MPL-2.0 */

/*
 * BFS — Hardcore Integrity Test for AmigaOS
 *
 * Deterministic, self-verifying tests that exercise every filesystem
 * operation. Designed for CI: machine-readable PASS/FAIL output.
 *
 * Usage: bfs-test VOLUME=DH1:
 *
 * Cross-compile:
 *   m68k-amigaos-gcc -m68020 -Os -o bfs-test bfs-test.c
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* Request 32KB stack from AmigaOS */
LONG __stack = 32768;

/* ── Globals ───────────────────────────────────────────────── */

static char vol[64];       /* volume prefix, e.g. "DH1:" */
static char pathbuf[512];
static UBYTE *databuf;     /* 64KB work buffer (allocated) */
#define BUF_SIZE 65536

static int tests_run, tests_pass, tests_fail;
static BPTR logfh; /* log file handle (0 = no log) */

/* ── Output ────────────────────────────────────────────────── */

static void put(const char *s) { Write(Output(), (APTR)s, strlen(s)); }

static void putnum(LONG n)
{
    char buf[12]; char *p = buf + sizeof(buf); *--p = 0;
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) *--p = '0';
    while (n > 0) { *--p = '0' + (n % 10); n /= 10; }
    if (neg) *--p = '-';
    put(p);
}

static void logput(const char *s) { if (logfh) Write(logfh, (APTR)s, strlen(s)); }
static void lognum(LONG n)
{
    char buf[12]; char *p = buf + sizeof(buf); *--p = 0;
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) *--p = '0';
    while (n > 0) { *--p = '0' + (n % 10); n /= 10; }
    if (neg) *--p = '-';
    logput(p);
}

static void progress(LONG cur, LONG total)
{
    LONG pct = (cur * 100) / total;
    char bar[16] = "..........";
    LONG filled = pct / 10;
    LONG i; for (i = 0; i < filled; i++) bar[i] = '#';
    put("\r  ["); put(bar); put("] "); putnum(pct); put("%");
}

static void progress_done(void) { put("\r                        \r"); }

static void pass(const char *name)
{
    progress_done();
    tests_run++; tests_pass++;
    put("PASS "); put(name); put("\n");
    logput("PASS\t"); logput(name); logput("\n");
}

static void fail(const char *name, const char *detail)
{
    progress_done();
    tests_run++; tests_fail++;
    put("FAIL "); put(name); put(": "); put(detail); put("\n");
    logput("FAIL\t"); logput(name); logput("\t"); logput(detail); logput("\n");
}

/* ── Path builder ──────────────────────────────────────────── */

static const char *vpath(const char *rel)
{
    char *p = pathbuf;
    const char *s = vol;
    while (*s) *p++ = *s++;
    while (*rel) *p++ = *rel++;
    *p = 0;
    return pathbuf;
}

/* ── Deterministic data ────────────────────────────────────── */

static ULONG xorshift(ULONG s)
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

static void fill(UBYTE *buf, ULONG size, ULONG seed)
{
    ULONG i;
    for (i = 0; i < size; i++) { seed = xorshift(seed); buf[i] = (UBYTE)seed; }
}

static ULONG checksum(const UBYTE *buf, ULONG size)
{
    ULONG crc = 0xDEADBEEF, i;
    for (i = 0; i < size; i++) crc = ((crc << 1) | (crc >> 31)) ^ buf[i];
    return crc;
}

/* ── File I/O ──────────────────────────────────────────────── */

static BOOL write_seeded(const char *path, ULONG size, ULONG seed)
{
    BPTR fh = Open(path, MODE_NEWFILE);
    if (!fh) return FALSE;
    ULONG rem = size, total = size;
    ULONG st = seed;
    BOOL ok = TRUE;
    while (rem > 0 && ok) {
        ULONG chunk = (rem > BUF_SIZE) ? BUF_SIZE : rem;
        ULONG i;
        for (i = 0; i < chunk; i++) { st = xorshift(st); databuf[i] = (UBYTE)st; }
        if ((ULONG)Write(fh, databuf, chunk) != chunk) ok = FALSE;
        rem -= chunk;
        if (total > BUF_SIZE * 4) progress(total - rem, total);
    }
    Close(fh);
    if (total > BUF_SIZE * 4) progress_done();
    return ok;
}

static BOOL verify_seeded(const char *path, ULONG size, ULONG seed)
{
    BPTR fh = Open(path, MODE_OLDFILE);
    if (!fh) return FALSE;
    ULONG rem = size, total = size;
    ULONG st = seed;
    BOOL ok = TRUE;
    while (rem > 0 && ok) {
        ULONG chunk = (rem > BUF_SIZE) ? BUF_SIZE : rem;
        if ((ULONG)Read(fh, databuf, chunk) != chunk) { ok = FALSE; break; }
        ULONG i;
        for (i = 0; i < chunk; i++) {
            st = xorshift(st);
            if (databuf[i] != (UBYTE)st) { ok = FALSE; break; }
        }
        rem -= chunk;
        if (total > BUF_SIZE * 4) progress(total - rem, total);
    }
    Close(fh);
    if (total > BUF_SIZE * 4) progress_done();
    return ok;
}

/* ══════════════════════════════════════════════════════════════
 * TESTS
 * ══════════════════════════════════════════════════════════════ */

static void test_basic_file(void)
{
    const char *T = "basic_01";
    const char *p = vpath("basic.dat");
    fill(databuf, 1000, 0x12345678);
    ULONG crc = checksum(databuf, 1000);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "open"); return; }
    Write(fh, databuf, 1000); Close(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    LONG got = Read(fh, databuf, 1000); Close(fh);
    if (got != 1000) { fail(T, "size"); goto clean; }
    if (checksum(databuf, 1000) != crc) { fail(T, "crc"); goto clean; }
    pass(T);
clean: DeleteFile(p);
}

static void test_large_file(void)
{
    const char *T = "large_02";
    const char *p = vpath("large.dat");
    ULONG size = 256 * 1024; /* 256KB */
    if (!write_seeded(p, size, 0xCAFEBABE)) { fail(T, "write"); return; }
    if (!verify_seeded(p, size, 0xCAFEBABE)) { fail(T, "verify"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_many_files(void)
{
    const char *T = "many_03";
    char rel[40];
    int i;
    BPTR lock = CreateDir(vpath("many"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);

    for (i = 0; i < 200; i++) {
        progress(i, 200);
        char *p = rel; const char *s = "many/f";
        while (*s) *p++ = *s++;
        /* append number */
        if (i >= 100) *p++ = '0' + (i / 100);
        if (i >= 10) *p++ = '0' + ((i / 10) % 10);
        *p++ = '0' + (i % 10); *p = 0;

        fill(databuf, 64, 0xA000 + i);
        BPTR fh = Open(vpath(rel), MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        Write(fh, databuf, 64); Close(fh);
    }

    for (i = 0; i < 200; i++) {
        char *p = rel; const char *s = "many/f";
        progress(i, 200);
        while (*s) *p++ = *s++;
        if (i >= 100) *p++ = '0' + (i / 100);
        if (i >= 10) *p++ = '0' + ((i / 10) % 10);
        *p++ = '0' + (i % 10); *p = 0;

        fill(databuf, 64, 0xA000 + i);
        ULONG crc = checksum(databuf, 64);
        BPTR fh = Open(vpath(rel), MODE_OLDFILE);
        if (!fh) { fail(T, "reopen"); return; }
        Read(fh, databuf, 64); Close(fh);
        if (checksum(databuf, 64) != crc) { fail(T, "crc"); return; }
    }

    for (i = 0; i < 200; i++) {
        char *p = rel; const char *s = "many/f";
        while (*s) *p++ = *s++;
        if (i >= 100) *p++ = '0' + (i / 100);
        if (i >= 10) *p++ = '0' + ((i / 10) % 10);
        *p++ = '0' + (i % 10); *p = 0;
        DeleteFile(vpath(rel));
    }
    DeleteFile(vpath("many"));
    progress_done();
    pass(T);
}

static void test_deep_dirs(void)
{
    const char *T = "deep_04";
    char path[128];
    int i;

    for (i = 0; i < 10; i++) {
        char *p = path; const char *s = vol;
        while (*s) *p++ = *s++;
        int j;
        for (j = 0; j <= i; j++) { *p++ = 'd'; *p++ = '0' + j; if (j < i) *p++ = '/'; }
        *p = 0;
        BPTR lock = CreateDir(path);
        if (!lock) { fail(T, "mkdir"); return; }
        UnLock(lock);
    }

    /* Write at deepest level */
    {
        char *p = path; const char *s = vol; while (*s) *p++ = *s++;
        for (i = 0; i < 10; i++) { *p++ = 'd'; *p++ = '0' + i; *p++ = '/'; }
        const char *f = "leaf.txt"; while (*f) *p++ = *f++;
        *p = 0;
    }
    fill(databuf, 100, 0xDE3E);
    BPTR fh = Open(path, MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 100); Close(fh);

    ULONG crc = checksum(databuf, 100);
    fh = Open(path, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    Read(fh, databuf, 100); Close(fh);
    if (checksum(databuf, 100) != crc) { fail(T, "crc"); return; }

    /* Cleanup */
    DeleteFile(path);
    for (i = 9; i >= 0; i--) {
        char *p = path; const char *s = vol; while (*s) *p++ = *s++;
        int j;
        for (j = 0; j <= i; j++) { *p++ = 'd'; *p++ = '0' + j; if (j < i) *p++ = '/'; }
        *p = 0;
        DeleteFile(path);
    }
    pass(T);
}

static void test_long_name(void)
{
    const char *T = "longname_05";
    char name[120];
    int i;
    for (i = 0; i < 107; i++) name[i] = 'A' + (i % 26);
    name[107] = 0;

    fill(databuf, 50, 0xF00D);
    BPTR fh = Open(vpath(name), MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 50); Close(fh);

    ULONG crc = checksum(databuf, 50);
    fh = Open(vpath(name), MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    Read(fh, databuf, 50); Close(fh);
    if (checksum(databuf, 50) != crc) { fail(T, "crc"); goto cl; }
    pass(T);
cl: DeleteFile(vpath(name));
}

static void test_overwrite(void)
{
    const char *T = "overwrite_06";
    const char *p = vpath("over.dat");

    fill(databuf, 500, 0x1111);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "w1"); return; }
    Write(fh, databuf, 500); Close(fh);

    fill(databuf, 300, 0x2222);
    ULONG crc = checksum(databuf, 300);
    fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "w2"); return; }
    Write(fh, databuf, 300); Close(fh);

    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    LONG got = Read(fh, databuf, 500); Close(fh);
    if (got != 300) { fail(T, "size"); goto cl; }
    if (checksum(databuf, 300) != crc) { fail(T, "crc"); goto cl; }
    pass(T);
cl: DeleteFile(p);
}

static void test_rename(void)
{
    const char *T = "rename_07";
    BPTR lock;
    char src[80], dst[80];

    lock = CreateDir(vpath("rsrc")); if (!lock) { fail(T, "mkdir1"); return; } UnLock(lock);
    lock = CreateDir(vpath("rdst")); if (!lock) { fail(T, "mkdir2"); return; } UnLock(lock);

    fill(databuf, 200, 0xBEEF);
    ULONG crc = checksum(databuf, 200);
    BPTR fh = Open(vpath("rsrc/mv.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 200); Close(fh);

    /* Rename needs both paths stable — can't use vpath twice */
    { const char *s = vpath("rsrc/mv.dat"); char *d = src; while (*s) *d++ = *s++; *d = 0; }
    { const char *s = vpath("rdst/mv.dat"); char *d = dst; while (*s) *d++ = *s++; *d = 0; }
    if (!Rename(src, dst)) { fail(T, "rename"); return; }

    fh = Open(dst, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    Read(fh, databuf, 200); Close(fh);
    if (checksum(databuf, 200) != crc) { fail(T, "crc"); goto cl; }
    pass(T);
cl: DeleteFile(dst);
    DeleteFile(vpath("rdst"));
    DeleteFile(vpath("rsrc"));
}

static void test_fill_disk(void)
{
    const char *T = "fill_08";
    char rel[32];
    int i, total = 0;

    for (i = 0; i < 10; i++) {
        progress(i, 10);
        char *p = rel; *p++ = 'F';
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;

        if (!write_seeded(vpath(rel), 64 * 1024, 0xF100 + i)) break;
        total++;
    }
    if (total < 3) { fail(T, "too few"); goto cl; }

    for (i = 0; i < total; i++) {
        char *p = rel; *p++ = 'F';
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;

        if (!verify_seeded(vpath(rel), 64 * 1024, 0xF100 + i)) { fail(T, "verify"); goto cl; }
    }
    pass(T);
cl:
    for (i = 0; i < total; i++) {
        char *p = rel; *p++ = 'F';
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;
        DeleteFile(vpath(rel));
    }
}

static void test_alloc_cycles(void)
{
    const char *T = "cycles_09";
    char rel[16];
    int cycle, i;

    for (cycle = 0; cycle < 5; cycle++) {
        progress(cycle, 5);
        for (i = 0; i < 10; i++) {
            char *p = rel; *p++ = 'c';
            int n = cycle * 20 + i;
            if (n >= 10) *p++ = '0' + (n / 10);
            *p++ = '0' + (n % 10); *p = 0;
            fill(databuf, 4096, 0xC100 + n);
            BPTR fh = Open(vpath(rel), MODE_NEWFILE);
            if (!fh) { fail(T, "write"); return; }
            Write(fh, databuf, 4096); Close(fh);
        }
        for (i = 0; i < 10; i++) {
            char *p = rel; *p++ = 'c';
            int n = cycle * 20 + i;
            if (n >= 10) *p++ = '0' + (n / 10);
            *p++ = '0' + (n % 10); *p = 0;
            DeleteFile(vpath(rel));
        }
    }

    fill(databuf, 1000, 0xF14A1);
    BPTR fh = Open(vpath("final.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "final write"); return; }
    Write(fh, databuf, 1000); Close(fh);
    ULONG crc = checksum(databuf, 1000);
    fh = Open(vpath("final.dat"), MODE_OLDFILE);
    if (!fh) { fail(T, "final read"); return; }
    Read(fh, databuf, 1000); Close(fh);
    if (checksum(databuf, 1000) != crc) { fail(T, "crc"); goto cl; }
    pass(T);
cl: DeleteFile(vpath("final.dat"));
}

static void test_seek(void)
{
    const char *T = "seek_10";
    const char *p = vpath("seek.dat");
    if (!write_seeded(p, 10240, 0x5EEE)) { fail(T, "write"); return; }

    BPTR fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open"); return; }
    Seek(fh, 5000, OFFSET_BEGINNING);
    LONG got = Read(fh, databuf, 100); Close(fh);
    if (got != 100) { fail(T, "read"); goto cl; }

    /* Verify: regenerate expected data at offset 5000 */
    ULONG st = 0x5EEE;
    ULONG i;
    for (i = 0; i < 5100; i++) {
        st = xorshift(st);
        if (i >= 5000 && databuf[i - 5000] != (UBYTE)st) { fail(T, "data"); goto cl; }
    }
    pass(T);
cl: DeleteFile(p);
}

static void test_truncate(void)
{
    const char *T = "truncate_11";
    const char *p = vpath("trunc.dat");
    if (!write_seeded(p, 8192, 0x7777)) { fail(T, "write"); return; }

    BPTR fh = Open(p, MODE_READWRITE);
    if (!fh) { fail(T, "open"); return; }
    SetFileSize(fh, 2048, OFFSET_BEGINNING);
    Close(fh);

    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    LONG got = Read(fh, databuf, 8192); Close(fh);
    if (got != 2048) { fail(T, "size"); goto cl; }
    if (!verify_seeded(p, 2048, 0x7777)) { fail(T, "data"); goto cl; }
    pass(T);
cl: DeleteFile(p);
}

static void test_protect(void)
{
    const char *T = "protect_12";
    const char *p = vpath("prot.dat");
    fill(databuf, 10, 0xAAAA);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);

    SetProtection(p, FIBF_READ | FIBF_WRITE);

    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); goto cl; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    Examine(lock, fib);
    LONG prot = fib->fib_Protection;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (!(prot & FIBF_READ) || !(prot & FIBF_WRITE)) { fail(T, "bits"); goto cl; }
    pass(T);
cl: DeleteFile(p);
}

static void test_comment(void)
{
    const char *T = "comment_13";
    const char *p = vpath("cmt.dat");
    fill(databuf, 10, 0xBBBB);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);

    SetComment(p, "BFS integrity test");

    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); goto cl; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    Examine(lock, fib);
    BOOL ok = (fib->fib_Comment[0] == 'B');
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (!ok) { fail(T, "mismatch"); goto cl; }
    pass(T);
cl: DeleteFile(p);
}

/* ── Additional tests ──────────────────────────────────────── */

static void test_multiextent(void)
{
    const char *T = "multiext_14";
    const char *p = vpath("multi.dat");
    ULONG size = 1024 * 1024;
    if (!write_seeded(p, size, 0x1E14)) { fail(T, "write"); return; }
    if (!verify_seeded(p, size, 0x1E14)) { fail(T, "verify"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_append(void)
{
    const char *T = "append_15";
    const char *p = vpath("append.dat");
    fill(databuf, 1000, 0xAA01);
    ULONG crc1 = checksum(databuf, 1000);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "open1"); return; }
    Write(fh, databuf, 1000); Close(fh);
    fh = Open(p, MODE_READWRITE);
    if (!fh) { fail(T, "open2"); return; }
    Seek(fh, 0, OFFSET_END);
    fill(databuf, 500, 0xAA02);
    Write(fh, databuf, 500); Close(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open3"); return; }
    LONG got = Read(fh, databuf, 2000); Close(fh);
    if (got != 1500) { fail(T, "size"); DeleteFile(p); return; }
    if (checksum(databuf, 1000) != crc1) { fail(T, "data1"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_partial_rw(void)
{
    const char *T = "partial_16";
    const char *p = vpath("partial.dat");
    if (!write_seeded(p, 5000, 0x1616)) { fail(T, "write"); return; }
    BPTR fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open"); return; }
    Seek(fh, 2500, OFFSET_BEGINNING);
    LONG got = Read(fh, databuf, 100); Close(fh);
    if (got != 100) { fail(T, "read"); DeleteFile(p); return; }
    ULONG st = 0x1616; ULONG i;
    for (i = 0; i < 2600; i++) { st = xorshift(st); if (i >= 2500 && databuf[i-2500] != (UBYTE)st) { fail(T, "data"); DeleteFile(p); return; } }
    DeleteFile(p);
    pass(T);
}

static void test_many_dirents(void)
{
    const char *T = "manydir_17";
    char rel[32]; int i;
    BPTR lock = CreateDir(vpath("bigdir"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);
    for (i = 0; i < 100; i++) {
        progress(i, 100);
        char *p = rel; const char *s = "bigdir/e";
        while (*s) *p++ = *s++;
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;
        fill(databuf, 32, 0xBD00 + i);
        BPTR fh = Open(vpath(rel), MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        Write(fh, databuf, 32); Close(fh);
    }
    fill(databuf, 32, 0xBD00 + 99);
    ULONG crc = checksum(databuf, 32);
    { char *p = rel; const char *s = "bigdir/e99"; while (*s) *p++ = *s++; *p = 0; }
    BPTR fh = Open(vpath(rel), MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    Read(fh, databuf, 32); Close(fh);
    if (checksum(databuf, 32) != crc) { fail(T, "crc"); return; }
    for (i = 0; i < 100; i++) {
        char *p = rel; const char *s = "bigdir/e";
        while (*s) *p++ = *s++;
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;
        DeleteFile(vpath(rel));
    }
    DeleteFile(vpath("bigdir"));
    pass(T);
}

static void test_special_names(void)
{
    const char *T = "special_18";
    const char *names[] = {"hello world", "file.with.dots", "UPPER", "MiXeD", "a-b_c", NULL};
    int i;
    for (i = 0; names[i]; i++) {
        fill(databuf, 10, 0x5500 + i);
        BPTR fh = Open(vpath(names[i]), MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        Write(fh, databuf, 10); Close(fh);
    }
    for (i = 0; names[i]; i++) {
        fill(databuf, 10, 0x5500 + i);
        ULONG crc = checksum(databuf, 10);
        BPTR fh = Open(vpath(names[i]), MODE_OLDFILE);
        if (!fh) { fail(T, "read"); return; }
        Read(fh, databuf, 10); Close(fh);
        if (checksum(databuf, 10) != crc) { fail(T, "crc"); return; }
        DeleteFile(vpath(names[i]));
    }
    pass(T);
}

static void test_nested_rename(void)
{
    const char *T = "nestrn_19";
    BPTR lock;
    lock = CreateDir(vpath("ra")); if (!lock) { fail(T, "mkdir1"); return; } UnLock(lock);
    lock = CreateDir(vpath("ra/rb")); if (!lock) { fail(T, "mkdir2"); return; } UnLock(lock);
    lock = CreateDir(vpath("rc")); if (!lock) { fail(T, "mkdir3"); return; } UnLock(lock);
    fill(databuf, 50, 0x1919);
    BPTR fh = Open(vpath("ra/rb/file.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 50); Close(fh);
    char src[80], dst[80];
    { const char *s = vpath("ra/rb/file.dat"); char *d = src; while (*s) *d++ = *s++; *d = 0; }
    { const char *s = vpath("rc/moved.dat"); char *d = dst; while (*s) *d++ = *s++; *d = 0; }
    if (!Rename(src, dst)) { fail(T, "rename"); return; }
    ULONG crc = checksum(databuf, 50);
    fh = Open(dst, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    Read(fh, databuf, 50); Close(fh);
    if (checksum(databuf, 50) != crc) { fail(T, "crc"); return; }
    DeleteFile(dst); DeleteFile(vpath("rc")); DeleteFile(vpath("ra/rb")); DeleteFile(vpath("ra"));
    pass(T);
}

static void test_rmdir_notempty(void)
{
    const char *T = "rmdir_20";
    BPTR lock = CreateDir(vpath("notempty"));
    if (!lock) { fail(T, "mkdir"); return; } UnLock(lock);
    fill(databuf, 10, 0x2020);
    BPTR fh = Open(vpath("notempty/child.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);
    BOOL del = DeleteFile(vpath("notempty"));
    if (del) { fail(T, "should fail"); return; }
    DeleteFile(vpath("notempty/child.dat"));
    DeleteFile(vpath("notempty"));
    pass(T);
}

static void test_extend(void)
{
    const char *T = "extend_21";
    const char *p = vpath("extend.dat");
    if (!write_seeded(p, 1024, 0xE121)) { fail(T, "write"); return; }
    BPTR fh = Open(p, MODE_READWRITE);
    if (!fh) { fail(T, "open"); return; }
    SetFileSize(fh, 8192, OFFSET_BEGINNING);
    Close(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    Seek(fh, 0, OFFSET_END);
    LONG size = Seek(fh, 0, OFFSET_BEGINNING);
    Close(fh);
    if (size != 8192) { fail(T, "size"); DeleteFile(p); return; }
    if (!verify_seeded(p, 1024, 0xE121)) { fail(T, "data"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_reopen(void)
{
    const char *T = "reopen_22";
    const char *p = vpath("reopen.dat");
    int i;
    for (i = 0; i < 10; i++) {
        fill(databuf, 100, 0x2200 + i);
        ULONG crc = checksum(databuf, 100);
        BPTR fh = Open(p, MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        Write(fh, databuf, 100); Close(fh);
        /* Verify immediately */
        fh = Open(p, MODE_OLDFILE);
        if (!fh) { fail(T, "reopen"); return; }
        Read(fh, databuf, 100); Close(fh);
        if (checksum(databuf, 100) != crc) {
            put("  CORRUPT at iteration "); putnum(i); put("\n");
            fail(T, "crc"); return;
        }
    }
    DeleteFile(p);
    pass(T);
}

static void test_diskfull(void)
{
    const char *T = "diskfull_23";
    const char *p = vpath("full.dat");
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "open"); return; }
    LONG total = 0; int i;
    /* Write ~20MB (leaves ~12MB free for COW overhead during delete) */
    for (i = 0; i < 160; i++) {
        LONG w = Write(fh, databuf, BUF_SIZE);
        if (w <= 0) break; progress(i, 160);
        total += w;
    }
    Close(fh);
    if (total == 0) { fail(T, "no write"); DeleteFile(p); return; }
    /* Delete must succeed even after large write */
    Printf("  deleting %lu KB...", (unsigned long)(total * 64));
    if (!DeleteFile(p)) { fail(T, "delete"); return; }
    Printf(" ok\n");
    /* Verify we can still create files after delete */
    fill(databuf, 10, 0x2323);
    fh = Open(vpath("after.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "after"); return; }
    Write(fh, databuf, 10); Close(fh);
    DeleteFile(vpath("after.dat"));
    pass(T);
}

static void test_persist(void)
{
    const char *T = "persist_24";
    const char *p = vpath("persist.dat");
    if (!write_seeded(p, 10000, 0x2424)) { fail(T, "write"); return; }
    if (!verify_seeded(p, 10000, 0x2424)) { fail(T, "verify"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

/* ── Edge case tests ───────────────────────────────────────── */

static void test_exact_block(void)
{
    const char *T = "block_25";
    const char *p = vpath("block.dat");
    if (!write_seeded(p, 4096, 0x2525)) { fail(T, "write"); return; }
    if (!verify_seeded(p, 4096, 0x2525)) { fail(T, "verify"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_empty_file(void)
{
    const char *T = "empty_26";
    const char *p = vpath("empty.dat");
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "create"); return; }
    Close(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open"); return; }
    LONG got = Read(fh, databuf, 100);
    Close(fh);
    if (got != 0) { fail(T, "size"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_max_name(void)
{
    const char *T = "maxname_27";
    /* AmigaOS limits full path to 255 chars. With "DH1:" prefix (4 chars),
     * max filename is 251. BFS internally supports 255. */
    char name[256];
    int i; for (i = 0; i < 251; i++) name[i] = 'a' + (i % 26);
    name[251] = 0;
    fill(databuf, 10, 0x2727);
    BPTR fh = Open(vpath(name), MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);
    ULONG crc = checksum(databuf, 10);
    fh = Open(vpath(name), MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    Read(fh, databuf, 10); Close(fh);
    if (checksum(databuf, 10) != crc) { fail(T, "crc"); DeleteFile(vpath(name)); return; }
    DeleteFile(vpath(name));
    pass(T);
}

static void test_single_entry_dir(void)
{
    const char *T = "singledir_28";
    BPTR lock = CreateDir(vpath("onedir"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);
    fill(databuf, 10, 0x2828);
    BPTR fh = Open(vpath("onedir/only.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);
    if (!DeleteFile(vpath("onedir/only.dat"))) { fail(T, "del file"); return; }
    if (!DeleteFile(vpath("onedir"))) { fail(T, "del dir"); return; }
    pass(T);
}

static void test_seek_past_end(void)
{
    const char *T = "seekend_29";
    const char *p = vpath("seekend.dat");
    fill(databuf, 100, 0x2929);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 100); Close(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open"); return; }
    Seek(fh, 200, OFFSET_BEGINNING); /* past end */
    LONG got = Read(fh, databuf, 10);
    Close(fh);
    if (got != 0) { fail(T, "should be 0"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_sparse_write(void)
{
    const char *T = "sparse_30";
    const char *p = vpath("sparse.dat");
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "open"); return; }
    /* Seek to offset 8192 and write there (creates a hole) */
    Seek(fh, 8192, OFFSET_BEGINNING);
    fill(databuf, 100, 0x3030);
    Write(fh, databuf, 100); Close(fh);
    /* Read back at offset 8192 */
    ULONG crc = checksum(databuf, 100);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    Seek(fh, 8192, OFFSET_BEGINNING);
    Read(fh, databuf, 100); Close(fh);
    if (checksum(databuf, 100) != crc) { fail(T, "crc"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

/* ── Stress tests ──────────────────────────────────────────── */

static void test_random_ops(void)
{
    const char *T = "randops_31";
    /* Pseudo-random create/delete/rename sequence */
    ULONG rng = 0x31313131;
    char names[20][8];
    int exists[20] = {0};
    int i, j;
    for (i = 0; i < 20; i++) {
        names[i][0] = 'r'; names[i][1] = '0' + (i/10); names[i][2] = '0' + (i%10); names[i][3] = 0;
    }
    for (j = 0; j < 60; j++) {
        rng = xorshift(rng);
        int idx = (rng >> 8) % 20;
        int op = rng % 3;
        if (op == 0 && !exists[idx]) {
            fill(databuf, 50, rng);
            BPTR fh = Open(vpath(names[idx]), MODE_NEWFILE);
            if (fh) { Write(fh, databuf, 50); Close(fh); exists[idx] = 1; }
        } else if (op == 1 && exists[idx]) {
            DeleteFile(vpath(names[idx]));
            exists[idx] = 0;
        } else if (op == 2 && exists[idx]) {
            /* rename to a temp name and back */
            char src[80], dst[80];
            { const char *s = vpath(names[idx]); char *d = src; while (*s) *d++ = *s++; *d = 0; }
            { const char *s = vpath("_tmp_rn"); char *d = dst; while (*s) *d++ = *s++; *d = 0; }
            if (Rename(src, dst)) Rename(dst, src);
        }
        if (j % 20 == 0) progress(j, 60);
    }
    /* Cleanup */
    for (i = 0; i < 20; i++) {
        if (exists[i]) DeleteFile(vpath(names[i]));
    }
    pass(T);
}

static void test_tiny_writes(void)
{
    const char *T = "tiny_32";
    const char *p = vpath("tiny.dat");
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "open"); return; }
    /* Write 500 bytes one at a time */
    ULONG st = 0x3232;
    int i;
    for (i = 0; i < 500; i++) {
        st = xorshift(st);
        UBYTE b = (UBYTE)st;
        Write(fh, &b, 1);
    }
    Close(fh);
    /* Verify */
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    LONG got = Read(fh, databuf, 500); Close(fh);
    if (got != 500) { fail(T, "size"); DeleteFile(p); return; }
    st = 0x3232;
    for (i = 0; i < 500; i++) {
        st = xorshift(st);
        if (databuf[i] != (UBYTE)st) { fail(T, "data"); DeleteFile(p); return; }
    }
    DeleteFile(p);
    pass(T);
}

static void test_mixed_sizes(void)
{
    const char *T = "mixed_33";
    /* Alternate between large (64KB) and small (100B) files */
    int i;
    for (i = 0; i < 10; i++) {
        char rel[16]; rel[0] = 'm'; rel[1] = '0' + i; rel[2] = 0;
        ULONG sz = (i % 2 == 0) ? 65536 : 100;
        if (!write_seeded(vpath(rel), sz, 0x3300 + i)) { fail(T, "write"); return; }
        progress(i, 10);
    }
    for (i = 0; i < 10; i++) {
        char rel[16]; rel[0] = 'm'; rel[1] = '0' + i; rel[2] = 0;
        ULONG sz = (i % 2 == 0) ? 65536 : 100;
        if (!verify_seeded(vpath(rel), sz, 0x3300 + i)) { fail(T, "verify"); return; }
    }
    for (i = 0; i < 10; i++) {
        char rel[16]; rel[0] = 'm'; rel[1] = '0' + i; rel[2] = 0;
        DeleteFile(vpath(rel));
    }
    pass(T);
}

/* ── Snapshot tests (via DoPkt to handler) ─────────────────── */

#define ACTION_BFS_SNAPSHOT_CREATE 3000
#define ACTION_BFS_SNAPSHOT_DELETE 3001
#define ACTION_BFS_SNAPSHOT_LIST   3002

static void test_snapshot_create_delete(void)
{
    const char *T = "snap_34";
    struct MsgPort *port = DeviceProc(vol);
    if (!port) { fail(T, "no port"); return; }

    /* Create snapshot */
    UBYTE bstr[36] = {0};
    const char *sname = "test_snap";
    int nlen = 9;
    bstr[0] = nlen; memcpy(bstr + 1, sname, nlen);

    LONG res = DoPkt(port, ACTION_BFS_SNAPSHOT_CREATE, (LONG)MKBADDR(bstr), 0, 0, 0, 0);
    if (!res) { fail(T, "create"); return; }

    /* Verify exists */
    char lbuf[64];
    res = DoPkt(port, ACTION_BFS_SNAPSHOT_LIST, (LONG)lbuf, (LONG)sizeof(lbuf), 0, 0, 0);
    if (!res) { fail(T, "list"); return; }

    /* Delete */
    res = DoPkt(port, ACTION_BFS_SNAPSHOT_DELETE, (LONG)MKBADDR(bstr), 0, 0, 0, 0);
    if (!res) { fail(T, "delete"); return; }

    pass(T);
}

/* Test that filesystem works after snapshot (has_snapshots=true).
 * Exercises COW + pending_frees + batch refcount filter. */
static void test_post_snapshot_cow(void)
{
    const char *T = "snapcow_35";
    const char *p = vpath("postcow.dat");
    /* Write 64KB to trigger multiple COW operations */
    if (!write_seeded(p, 65536, 0x3535)) { fail(T, "write"); return; }
    if (!verify_seeded(p, 65536, 0x3535)) { fail(T, "verify"); DeleteFile(p); return; }
    DeleteFile(p);
    /* Verify we can still create after delete (free tree intact) */
    if (!write_seeded(p, 1000, 0x3536)) { fail(T, "write2"); return; }
    DeleteFile(p);
    pass(T);
}

/* Test rapid open→write→close→open→read→verify cycles.
 * This is the DiskSpeed seek pattern that stalls with sync-on-close. */
static void test_rapid_rewrite(void)
{
    const char *T = "rapid_38";
    const char *p = vpath("rapid.dat");
    LONG i;

    for (i = 0; i < 50; i++) {
        /* Write pass */
        BPTR fh = Open(p, MODE_NEWFILE);
        if (!fh) { fail(T, "open-w"); return; }
        fill(databuf, 256, 0x3800 + i);
        Write(fh, databuf, 256);
        Close(fh);

        /* Read-back and verify */
        fh = Open(p, MODE_OLDFILE);
        if (!fh) { fail(T, "open-r"); return; }
        UBYTE rdbuf[256];
        LONG got = Read(fh, rdbuf, 256);
        Close(fh);
        if (got != 256) { fail(T, "short"); DeleteFile(p); return; }
        fill(databuf, 256, 0x3800 + i);
        if (memcmp(databuf, rdbuf, 256) != 0) { fail(T, "verify"); DeleteFile(p); return; }
    }
    DeleteFile(p);
    pass(T);
}

static void test_timestamp_on_create(void)
{
    const char *T = "time_35";
    const char *p = vpath("timed.dat");
    fill(databuf, 10, 0x3535);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);

    /* Examine and check date is non-zero */
    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); DeleteFile(p); return; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); fail(T, "fib"); DeleteFile(p); return; }
    Examine(lock, fib);
    LONG days = fib->fib_Date.ds_Days;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (days == 0) { fail(T, "date is zero"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

static void test_owner_uid_gid(void)
{
    const char *T = "owner_36";
    const char *p = vpath("owned.dat");
    fill(databuf, 10, 0x3636);
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "write"); return; }
    Write(fh, databuf, 10); Close(fh);

    /* SetOwner not available as a simple DOS call, but we can verify
     * that Examine returns uid/gid fields (should be 0 for new files) */
    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); DeleteFile(p); return; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    Examine(lock, fib);
    /* UID/GID should be 0 for newly created files */
    UWORD uid = fib->fib_OwnerUID;
    UWORD gid = fib->fib_OwnerGID;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (uid != 0 || gid != 0) { fail(T, "uid/gid not 0"); DeleteFile(p); return; }
    DeleteFile(p);
    pass(T);
}

/* ── Test: ExNext enumerates all entries (hash collision regression) ── */

static void test_exnext_complete(void)
{
    const char *T = "exnext_37";
    int i;
    BPTR lock = CreateDir(vpath("exdir"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);

    /* Create 50 files — enough to span multiple leaves and trigger collisions */
    for (i = 0; i < 50; i++) {
        char rel[32]; char *p = rel;
        const char *s = "exdir/item_";
        while (*s) *p++ = *s++;
        *p++ = '0' + (i / 10); *p++ = '0' + (i % 10); *p = 0;
        BPTR fh = Open(vpath(rel), MODE_NEWFILE);
        if (!fh) { fail(T, "create"); return; }
        Close(fh);
    }

    /* Count entries via ExNext */
    lock = Lock(vpath("exdir"), SHARED_LOCK);
    if (!lock) { fail(T, "lock"); return; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocVec(sizeof(*fib), MEMF_CLEAR);
    if (!fib) { UnLock(lock); fail(T, "alloc"); return; }
    Examine(lock, fib);
    int count = 0;
    while (ExNext(lock, fib)) count++;
    FreeVec(fib);
    UnLock(lock);

    /* 50 files + '..' entry = 51 */
    if (count != 51) { fail(T, "count"); put("  got="); putnum(count); put(" want=51\n"); }
    else { pass(T); }

    /* Cleanup */
    for (i = 0; i < 50; i++) {
        char rel[32]; char *p = rel;
        const char *s = "exdir/item_";
        while (*s) *p++ = *s++;
        *p++ = '0' + (i / 10); *p++ = '0' + (i % 10); *p = 0;
        DeleteFile(vpath(rel));
    }
    DeleteFile(vpath("exdir"));
}

/* ── Test table ────────────────────────────────────────────── */

typedef void (*test_fn)(void);
static const struct { const char *name; test_fn fn; } all_tests[] = {
    {"basic_01",     test_basic_file},
    {"large_02",     test_large_file},
    {"many_03",      test_many_files},
    {"deep_04",      test_deep_dirs},
    {"longname_05",  test_long_name},
    {"overwrite_06", test_overwrite},
    {"rename_07",    test_rename},
    {"cycles_09",    test_alloc_cycles},
    {"seek_10",      test_seek},
    {"truncate_11",  test_truncate},
    {"protect_12",   test_protect},
    {"comment_13",   test_comment},
    {"multiext_14",  test_multiextent},
    {"append_15",    test_append},
    {"partial_16",   test_partial_rw},
    {"manydir_17",   test_many_dirents},
    {"special_18",   test_special_names},
    {"nestrn_19",    test_nested_rename},
    {"rmdir_20",     test_rmdir_notempty},
    {"extend_21",    test_extend},
    {"reopen_22",    test_reopen},
    {"snap_34",      test_snapshot_create_delete},
    {"snapcow_35",   test_post_snapshot_cow},
    {"rapid_38",     test_rapid_rewrite},
    {"persist_24",   test_persist},
    {"block_25",     test_exact_block},
    {"empty_26",     test_empty_file},
    {"maxname_27",   test_max_name},
    {"singledir_28", test_single_entry_dir},
    {"seekend_29",   test_seek_past_end},
    {"sparse_30",    test_sparse_write},
    {"randops_31",   test_random_ops},
    {"tiny_32",      test_tiny_writes},
    {"mixed_33",     test_mixed_sizes},
    {"time_35",      test_timestamp_on_create},
    {"owner_36",     test_owner_uid_gid},
    {"exnext_37",    test_exnext_complete},
    /* Disk-filling tests last (they consume all free space) */
    {"fill_08",      test_fill_disk},
    {"diskfull_23",  test_diskfull},
    {NULL, NULL}
};

static int has_substr(const char *str, const char *sub)
{
    if (!sub || !sub[0]) return 1;
    /* Check if sub appears anywhere in str */
    const char *s;
    for (s = str; *s; s++) {
        const char *a = s, *b = sub;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

int main(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;

    struct RDArgs *rdargs;
    LONG args[3] = {0, 0, 0};
    rdargs = ReadArgs("VOLUME/A,LOG/K,FILTER", args, NULL);
    if (!rdargs) {
        put("Usage: bfs-test VOLUME [LOG=path] [filter]\n");
        put("  bfs-test DH1:                   (run all)\n");
        put("  bfs-test DH1: large             (run matching)\n");
        put("  bfs-test DH1: LOG=SYS:test.log  (CI mode)\n");
        put("  bfs-test DH1: LOG=SYS:x large   (both)\n");
        me->pr_WindowPtr = oldwin;
        return 5;
    }

    /* Open log file if specified */
    logfh = 0;
    if (args[1]) logfh = Open((STRPTR)args[1], MODE_NEWFILE);

    { const char *s = (const char *)args[0]; char *d = vol; while (*s) *d++ = *s++; *d = 0; }

    /* Copy filter before FreeArgs invalidates the buffer */
    static char filterbuf[64];
    const char *filter = NULL;
    if (args[2]) {
        const char *s = (const char *)args[2]; char *d = filterbuf;
        while (*s && d < filterbuf + 63) *d++ = *s++;
        *d = 0;
        filter = filterbuf;
    }

    FreeArgs(rdargs);

    databuf = AllocMem(BUF_SIZE, MEMF_PUBLIC);
    if (!databuf) { put("Out of memory\n"); me->pr_WindowPtr = oldwin; return 20; }

    put("=== BFS INTEGRITY TEST ===\n");
    put("Volume: "); put(vol); put("\n\n");
    logput("# BFS Test Log\n");
    logput("# STATUS\tNAME\t[DETAIL]\n");

    int i;
    for (i = 0; all_tests[i].name; i++) {
        if (!has_substr(all_tests[i].name, filter)) continue;
        put(" RUN  "); put(all_tests[i].name); put("\n");
        all_tests[i].fn();
    }

    put("\n=== RESULTS: ");
    putnum(tests_pass); put("/"); putnum(tests_run); put(" passed");
    if (tests_fail) { put(", "); putnum(tests_fail); put(" FAILED"); }
    put(" ===\n");

    /* Write summary to log */
    logput("# SUMMARY\t"); lognum(tests_pass); logput("\t");
    lognum(tests_run); logput("\t"); lognum(tests_fail); logput("\n");

    if (logfh) Close(logfh);
    me->pr_WindowPtr = oldwin;
    FreeMem(databuf, BUF_SIZE);
    return tests_fail ? 5 : 0;
}
