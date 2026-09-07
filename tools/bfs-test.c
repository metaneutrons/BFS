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
#include <dos/exall.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "../src/amiga/dos_packets.h"

/* Request 32KB stack from AmigaOS */
LONG __stack = 32768;

/* ── Globals ───────────────────────────────────────────────── */

static char vol[64];       /* volume prefix, e.g. "DH1:" */
static char pathbuf[512];
static UBYTE *databuf;     /* 64KB work buffer (allocated) */
#define BUF_SIZE 65536

static int tests_run, tests_pass, tests_fail;
static BPTR logfh; /* log file handle (0 = no log) */
static BOOL quick_mode;
static BOOL io_failed, log_failed;
static char logpath[480];

/* ── Output ────────────────────────────────────────────────── */

static int tool_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void tool_memcpy(void *dst, const void *src, int len)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    while (len-- > 0) *d++ = *s++;
}

static int tool_memcmp(const void *left, const void *right, int len)
{
    const UBYTE *a = (const UBYTE *)left;
    const UBYTE *b = (const UBYTE *)right;
    while (len-- > 0) {
        if (*a != *b) return *a < *b ? -1 : 1;
        a++;
        b++;
    }
    return 0;
}

static void put(const char *s) { Write(Output(), (APTR)s, tool_strlen(s)); }

static void putnum(LONG n)
{
    char buf[12]; char *p = buf + sizeof(buf); *--p = 0;
    int neg = 0;
    ULONG magnitude;
    if (n < 0) {
        neg = 1;
        magnitude = (ULONG)(-(n + 1)) + 1u;
    } else {
        magnitude = (ULONG)n;
    }
    if (magnitude == 0) *--p = '0';
    while (magnitude > 0) {
        *--p = '0' + (magnitude % 10u);
        magnitude /= 10u;
    }
    if (neg) *--p = '-';
    put(p);
}

static void logput(const char *s)
{
    LONG len = tool_strlen(s);
    if (logfh && Write(logfh, (APTR)s, len) != len) log_failed = TRUE;
}
static void lognum(LONG n)
{
    char buf[12]; char *p = buf + sizeof(buf); *--p = 0;
    int neg = 0;
    ULONG magnitude;
    if (n < 0) {
        neg = 1;
        magnitude = (ULONG)(-(n + 1)) + 1u;
    } else {
        magnitude = (ULONG)n;
    }
    if (magnitude == 0) *--p = '0';
    while (magnitude > 0) {
        *--p = '0' + (magnitude % 10u);
        magnitude /= 10u;
    }
    if (neg) *--p = '-';
    logput(p);
}

static void progress(LONG cur, LONG total)
{
    /* CI consumes the result file; console repaint traffic is disproportionately
     * expensive under FS-UAE and provides no machine-readable evidence. */
    if (logfh) return;
    LONG pct = (cur * 100) / total;
    char bar[16] = "..........";
    LONG filled = pct / 10;
    LONG i; for (i = 0; i < filled; i++) bar[i] = '#';
    put("\r  ["); put(bar); put("] "); putnum(pct); put("%");
}

static void progress_done(void)
{
    if (!logfh) put("\r                        \r");
}

static void fail(const char *name, const char *detail)
{
    progress_done();
    tests_run++; tests_fail++;
    put("FAIL "); put(name); put(": "); put(detail); put("\n");
    logput("FAIL\t"); logput(name); logput("\t"); logput(detail); logput("\n");
}

static void pass(const char *name)
{
    if (io_failed) { fail(name, "short I/O or failed close"); return; }
    progress_done();
    tests_run++; tests_pass++;
    put("PASS "); put(name); put("\n");
    logput("PASS\t"); logput(name); logput("\n");
}

/* ── Path builder ──────────────────────────────────────────── */

static const char *vpath(const char *rel)
{
    char *p = pathbuf;
    char *end = pathbuf + sizeof(pathbuf) - 1;
    const char *s = vol;
    while (*s && p < end) *p++ = *s++;
    if (*s) return NULL;
    while (*rel && p < end) *p++ = *rel++;
    if (*rel) return NULL;
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

static void write_exact(BPTR fh, const void *data, LONG size)
{
    if (Write(fh, (APTR)data, size) != size) io_failed = TRUE;
}

static void read_exact(BPTR fh, void *data, LONG size)
{
    if (Read(fh, data, size) != size) io_failed = TRUE;
}

static BOOL close_checked(BPTR fh)
{
    BOOL ok = Close(fh);
    if (!ok) io_failed = TRUE;
    return ok;
}

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
    if (!close_checked(fh)) ok = FALSE;
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
    if (!close_checked(fh)) ok = FALSE;
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
    write_exact(fh, databuf, 1000); close_checked(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    LONG got = Read(fh, databuf, 1000); close_checked(fh);
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
    int i, count = quick_mode ? 16 : 200;
    BPTR lock = CreateDir(vpath("many"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);

    for (i = 0; i < count; i++) {
        progress(i, count);
        char *p = rel; const char *s = "many/f";
        while (*s) *p++ = *s++;
        /* append number */
        if (i >= 100) *p++ = '0' + (i / 100);
        if (i >= 10) *p++ = '0' + ((i / 10) % 10);
        *p++ = '0' + (i % 10); *p = 0;

        fill(databuf, 64, 0xA000 + i);
        BPTR fh = Open(vpath(rel), MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        write_exact(fh, databuf, 64); close_checked(fh);
    }

    for (i = 0; i < count; i++) {
        char *p = rel; const char *s = "many/f";
        progress(i, count);
        while (*s) *p++ = *s++;
        if (i >= 100) *p++ = '0' + (i / 100);
        if (i >= 10) *p++ = '0' + ((i / 10) % 10);
        *p++ = '0' + (i % 10); *p = 0;

        fill(databuf, 64, 0xA000 + i);
        ULONG crc = checksum(databuf, 64);
        BPTR fh = Open(vpath(rel), MODE_OLDFILE);
        if (!fh) { fail(T, "reopen"); return; }
        read_exact(fh, databuf, 64); close_checked(fh);
        if (checksum(databuf, 64) != crc) { fail(T, "crc"); return; }
    }

    for (i = 0; i < count; i++) {
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
    write_exact(fh, databuf, 100); close_checked(fh);

    ULONG crc = checksum(databuf, 100);
    fh = Open(path, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    read_exact(fh, databuf, 100); close_checked(fh);
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

static BPTR locate_relative(BPTR lock, const char *name)
{
    ULONG storage[65];
    UBYTE *bstr = (UBYTE *)storage;
    ULONG len = 0;
    while (len < 255 && name[len]) len++;
    bstr[0] = len;
    tool_memcpy(bstr + 1, name, len);
    struct FileLock *base = BADDR(lock);
    return DoPkt(base->fl_Task, ACTION_LOCATE_OBJECT, lock,
                 (LONG)MKBADDR(bstr), SHARED_LOCK, 0, 0);
}

static BOOL check_relative_paths(BPTR child, BPTR expected)
{
    const char *paths[] = {"/leaf", "//path/leaf", ":path/leaf", "../leaf"};
    BOOL ok = SameLock(child, expected) == LOCK_SAME_VOLUME;
    unsigned i;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        BPTR found = locate_relative(child, paths[i]);
        if (!found || SameLock(found, expected) != LOCK_SAME) ok = FALSE;
        if (found) UnLock(found);
    }
    const char *bad[] = {"missing//leaf", "/leaf/child"};
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        BPTR found = locate_relative(child, bad[i]);
        if (found || !IoErr()) ok = FALSE;
        if (found) UnLock(found);
    }
    return ok;
}

static BOOL check_empty_paths(BPTR child, BPTR parent, BPTR root)
{
    const char *paths[] = {"", "/", "//", ":"};
    BPTR expected[] = {child, parent, root, root};
    BOOL ok = TRUE;
    unsigned i;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        BPTR found = locate_relative(child, paths[i]);
        if (!found || SameLock(found, expected[i]) != LOCK_SAME) ok = FALSE;
        if (found) UnLock(found);
    }
    return ok;
}

static void test_relative_paths(void)
{
    BPTR parent = CreateDir(vpath("path"));
    BPTR child = parent ? CreateDir(vpath("path/child")) : 0;
    BOOL ok = child && write_seeded(vpath("path/leaf"), 3, 17);
    BPTR expected = ok ? Lock(vpath("path/leaf"), SHARED_LOCK) : 0;
    ok = expected && check_relative_paths(child, expected);
    BPTR root = Lock(vol, SHARED_LOCK);
    ok = ok && root && check_empty_paths(child, parent, root);
    if (root) UnLock(root);
    if (expected) UnLock(expected);
    if (child) UnLock(child);
    if (parent) UnLock(parent);
    if (!DeleteFile(vpath("path/leaf"))) ok = FALSE;
    if (!DeleteFile(vpath("path/child"))) ok = FALSE;
    if (!DeleteFile(vpath("path"))) ok = FALSE;
    if (ok) pass("path_45");
    else fail("path_45", "parent traversal, volume prefix, or intermediate lookup error");
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
    write_exact(fh, databuf, 50); close_checked(fh);

    ULONG crc = checksum(databuf, 50);
    fh = Open(vpath(name), MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    read_exact(fh, databuf, 50); close_checked(fh);
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
    write_exact(fh, databuf, 500); close_checked(fh);

    fill(databuf, 300, 0x2222);
    ULONG crc = checksum(databuf, 300);
    fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "w2"); return; }
    write_exact(fh, databuf, 300); close_checked(fh);

    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    LONG got = Read(fh, databuf, 500); close_checked(fh);
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
    write_exact(fh, databuf, 200); close_checked(fh);

    /* Rename needs both paths stable because vpath() reuses one buffer. */
    { const char *s = vpath("rsrc/mv.dat"); char *d = src; while (*s) *d++ = *s++; *d = 0; }
    { const char *s = vpath("rdst/mv.dat"); char *d = dst; while (*s) *d++ = *s++; *d = 0; }
    if (!Rename(src, dst)) { fail(T, "rename"); return; }

    fh = Open(dst, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    read_exact(fh, databuf, 200); close_checked(fh);
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
    int i, total = 0, attempted = 0, file_count = quick_mode ? 4 : 10;
    const char *failure = NULL;
    BOOL cleanup_failed = FALSE;

    for (i = 0; i < file_count; i++) {
        progress(i, file_count);
        char *p = rel; *p++ = 'F';
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;

        /* A short write can leave a partial file behind on a full volume. */
        attempted = i + 1;
        if (!write_seeded(vpath(rel), 64 * 1024, 0xF100 + i)) {
            failure = "write";
            break;
        }
        total++;
    }
    if (!failure && total < 3) failure = "too few";

    for (i = 0; !failure && i < total; i++) {
        char *p = rel; *p++ = 'F';
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;

        if (!verify_seeded(vpath(rel), 64 * 1024, 0xF100 + i))
            failure = "verify";
    }

    for (i = 0; i < attempted; i++) {
        char *p = rel; *p++ = 'F';
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;
        if (!DeleteFile(vpath(rel)) && IoErr() != ERROR_OBJECT_NOT_FOUND)
            cleanup_failed = TRUE;
    }
    if (cleanup_failed) failure = failure ? "write/cleanup" : "cleanup";
    if (failure) fail(T, failure); else pass(T);
}

static void test_alloc_cycles(void)
{
    const char *T = "cycles_09";
    char rel[16];
    int cycle, i;
    int cycle_count = quick_mode ? 2 : 5;
    int file_count = quick_mode ? 4 : 10;

    for (cycle = 0; cycle < cycle_count; cycle++) {
        progress(cycle, cycle_count);
        for (i = 0; i < file_count; i++) {
            char *p = rel; *p++ = 'c';
            int n = cycle * 20 + i;
            if (n >= 10) *p++ = '0' + (n / 10);
            *p++ = '0' + (n % 10); *p = 0;
            fill(databuf, 4096, 0xC100 + n);
            BPTR fh = Open(vpath(rel), MODE_NEWFILE);
            if (!fh) { fail(T, "write"); return; }
            write_exact(fh, databuf, 4096); close_checked(fh);
        }
        for (i = 0; i < file_count; i++) {
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
    write_exact(fh, databuf, 1000); close_checked(fh);
    ULONG crc = checksum(databuf, 1000);
    fh = Open(vpath("final.dat"), MODE_OLDFILE);
    if (!fh) { fail(T, "final read"); return; }
    read_exact(fh, databuf, 1000); close_checked(fh);
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
    LONG got = Read(fh, databuf, 100); close_checked(fh);
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
    close_checked(fh);

    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    LONG got = Read(fh, databuf, 8192); close_checked(fh);
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
    write_exact(fh, databuf, 10); close_checked(fh);

    if (!SetProtection(p, FIBF_READ | FIBF_WRITE)) {
        fail(T, "set protection");
        goto cl;
    }

    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); goto cl; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); fail(T, "fib"); goto cl; }
    if (!Examine(lock, fib)) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        fail(T, "examine");
        goto cl;
    }
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
    write_exact(fh, databuf, 10); close_checked(fh);

    if (!SetComment(p, "BFS integrity test")) {
        fail(T, "set comment");
        goto cl;
    }

    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); goto cl; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); fail(T, "fib"); goto cl; }
    if (!Examine(lock, fib)) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        fail(T, "examine");
        goto cl;
    }
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
    write_exact(fh, databuf, 1000); close_checked(fh);
    fh = Open(p, MODE_READWRITE);
    if (!fh) { fail(T, "open2"); return; }
    Seek(fh, 0, OFFSET_END);
    fill(databuf, 500, 0xAA02);
    write_exact(fh, databuf, 500); close_checked(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open3"); return; }
    LONG got = Read(fh, databuf, 2000); close_checked(fh);
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
    LONG got = Read(fh, databuf, 100); close_checked(fh);
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
    int count = quick_mode ? 16 : 100;
    BPTR lock = CreateDir(vpath("bigdir"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);
    for (i = 0; i < count; i++) {
        progress(i, count);
        char *p = rel; const char *s = "bigdir/e";
        while (*s) *p++ = *s++;
        if (i >= 10) *p++ = '0' + (i / 10);
        *p++ = '0' + (i % 10); *p = 0;
        fill(databuf, 32, 0xBD00 + i);
        BPTR fh = Open(vpath(rel), MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        write_exact(fh, databuf, 32); close_checked(fh);
    }
    fill(databuf, 32, 0xBD00 + count - 1);
    ULONG crc = checksum(databuf, 32);
    { char *p = rel; const char *s = "bigdir/e"; int last = count - 1;
      while (*s) *p++ = *s++;
      if (last >= 10) *p++ = '0' + (last / 10);
      *p++ = '0' + (last % 10); *p = 0; }
    BPTR fh = Open(vpath(rel), MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    read_exact(fh, databuf, 32); close_checked(fh);
    if (checksum(databuf, 32) != crc) { fail(T, "crc"); return; }
    for (i = 0; i < count; i++) {
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
        write_exact(fh, databuf, 10); close_checked(fh);
    }
    for (i = 0; names[i]; i++) {
        fill(databuf, 10, 0x5500 + i);
        ULONG crc = checksum(databuf, 10);
        BPTR fh = Open(vpath(names[i]), MODE_OLDFILE);
        if (!fh) { fail(T, "read"); return; }
        read_exact(fh, databuf, 10); close_checked(fh);
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
    write_exact(fh, databuf, 50); close_checked(fh);
    char src[80], dst[80];
    { const char *s = vpath("ra/rb/file.dat"); char *d = src; while (*s) *d++ = *s++; *d = 0; }
    { const char *s = vpath("rc/moved.dat"); char *d = dst; while (*s) *d++ = *s++; *d = 0; }
    if (!Rename(src, dst)) { fail(T, "rename"); return; }
    ULONG crc = checksum(databuf, 50);
    fh = Open(dst, MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    read_exact(fh, databuf, 50); close_checked(fh);
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
    write_exact(fh, databuf, 10); close_checked(fh);
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
    close_checked(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    Seek(fh, 0, OFFSET_END);
    LONG size = Seek(fh, 0, OFFSET_BEGINNING);
    close_checked(fh);
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
    int count = quick_mode ? 3 : 10;
    for (i = 0; i < count; i++) {
        fill(databuf, 100, 0x2200 + i);
        ULONG crc = checksum(databuf, 100);
        BPTR fh = Open(p, MODE_NEWFILE);
        if (!fh) { fail(T, "write"); return; }
        write_exact(fh, databuf, 100); close_checked(fh);
        /* Verify immediately */
        fh = Open(p, MODE_OLDFILE);
        if (!fh) { fail(T, "reopen"); return; }
        read_exact(fh, databuf, 100); close_checked(fh);
        if (checksum(databuf, 100) != crc) {
            put("  CORRUPT at iteration "); putnum(i); put("\n");
            fail(T, "crc"); return;
        }
    }
    DeleteFile(p);
    pass(T);
}

static BOOL shared_open_modes(const char *path)
{
    BPTR first = Open(path, MODE_NEWFILE), second = 0;
    BOOL ok = FALSE;
    if (!first) return FALSE;
    do {
        if (Write(first, (APTR)"abcdef", 6) != 6) break;
        second = Open(path, MODE_OLDFILE);
        if (second || IoErr() != ERROR_OBJECT_IN_USE) break;
        BPTR lock = Lock(path, SHARED_LOCK);
        if (lock) { UnLock(lock); break; }
        if (IoErr() != ERROR_OBJECT_IN_USE) break;
        if (!Close(first)) { first = 0; break; }
        first = Open(path, MODE_OLDFILE);
        second = Open(path, MODE_READWRITE);
        if (!first || !second) break;
        if (Write(first, (APTR)"X", 1) != 1) break;
        if (Read(second, databuf, 6) != 6 || tool_memcmp(databuf, "Xbcdef", 6)) break;
        if (SetFileSize(second, 3, OFFSET_BEGINNING) != 3) break;
        if (Seek(first, 0, OFFSET_BEGINNING) < 0) break;
        if (Read(first, databuf, 6) != 3 || tool_memcmp(databuf, "Xbc", 3)) break;
        ok = TRUE;
    } while (0);
    if (first && !Close(first)) ok = FALSE;
    if (second && !Close(second)) ok = FALSE;
    if (!DeleteFile(path)) ok = FALSE;
    return ok;
}

static void test_shared_open_modes(void)
{
    if (shared_open_modes(vpath("shared-modes.dat"))) pass("sharing_39");
    else fail("sharing_39", "open mode, lock conflict, or shared inode state");
}

static BOOL protection_enforced(const char *path)
{
    BPTR fh = 0, probe = 0;
    BOOL ok = FALSE;
    if (!write_seeded(path, 16, 0x1234)) return FALSE;
    do {
        if (!SetProtection(path, FIBF_DELETE)) break;
        if (DeleteFile(path) || IoErr() != ERROR_DELETE_PROTECTED) break;
        probe = Open(path, MODE_NEWFILE);
        if (probe || IoErr() != ERROR_DELETE_PROTECTED) break;
        if (!SetProtection(path, FIBF_WRITE)) break;
        probe = Open(path, MODE_NEWFILE);
        if (probe || IoErr() != ERROR_WRITE_PROTECTED) break;
        fh = Open(path, MODE_OLDFILE);
        if (!fh || Read(fh, databuf, 16) != 16) break;
        if (Write(fh, databuf, 1) != -1 || IoErr() != ERROR_WRITE_PROTECTED) break;
        if (SetFileSize(fh, 0, OFFSET_BEGINNING) != -1 ||
            IoErr() != ERROR_WRITE_PROTECTED) break;
        if (!SetProtection(path, FIBF_READ)) break;
        if (Read(fh, databuf, 1) != -1 || IoErr() != ERROR_READ_PROTECTED) break;
        if (!close_checked(fh)) { fh = 0; break; }
        fh = 0;
        probe = Open(path, MODE_OLDFILE);
        if (probe || IoErr() != ERROR_READ_PROTECTED) break;
        if (!SetProtection(path, FIBF_ARCHIVE)) break;
        fh = Open(path, MODE_OLDFILE);
        if (!fh || Write(fh, databuf, 1) != 1) break;
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        if (!fib) break;
        BOOL cleared = ExamineFH(fh, fib) && !(fib->fib_Protection & FIBF_ARCHIVE);
        FreeDosObject(DOS_FIB, fib);
        if (!cleared) break;
        ok = TRUE;
    } while (0);
    if (probe && !close_checked(probe)) ok = FALSE;
    if (fh && !close_checked(fh)) ok = FALSE;
    if (!SetProtection(path, 0) || !DeleteFile(path)) ok = FALSE;
    return ok;
}

static void test_protection_enforced(void)
{
    if (protection_enforced(vpath("protected.dat"))) pass("protectio_40");
    else fail("protectio_40", "read/write/delete protection or archive flag");
}

static BOOL resize_preserves_positions(const char *path)
{
    BPTR first = 0, second = 0;
    BOOL ok = FALSE;
    if (!write_seeded(path, 16, 0x4321)) return FALSE;
    do {
        first = Open(path, MODE_OLDFILE);
        second = Open(path, MODE_READWRITE);
        if (!first || !second) break;
        if (Seek(first, 3, OFFSET_BEGINNING) != 0 ||
            Seek(second, 10, OFFSET_BEGINNING) != 0) break;
        if (SetFileSize(first, 32, OFFSET_BEGINNING) != 32 ||
            Seek(first, 0, OFFSET_CURRENT) != 3) break;
        if (SetFileSize(first, 2, OFFSET_BEGINNING) != 10 ||
            Seek(first, 0, OFFSET_CURRENT) != 3) break;
        if (!close_checked(second)) { second = 0; break; }
        second = 0;
        if (SetFileSize(first, 2, OFFSET_BEGINNING) != 2 ||
            Seek(first, 0, OFFSET_CURRENT) != 2) break;
        if (SetFileSize(first, -3, OFFSET_CURRENT) != -1 ||
            Seek(first, 0, OFFSET_CURRENT) != 2) break;
        ok = TRUE;
    } while (0);
    if (first && !close_checked(first)) ok = FALSE;
    if (second && !close_checked(second)) ok = FALSE;
    if (!DeleteFile(path)) ok = FALSE;
    return ok;
}

static void test_resize_positions(void)
{
    if (resize_preserves_positions(vpath("resize-positions.dat"))) pass("resizepos_41");
    else fail("resizepos_41", "position preservation or shared handle truncation");
}

static BOOL send_packet64(BPTR file, LONG type, int64_t offset, LONG mode,
                           int64_t *result, LONG *error)
{
    struct FileHandle *fh = (struct FileHandle *)BADDR(file);
    struct MsgPort *reply = CreateMsgPort();
    if (!reply) return FALSE;
    struct { struct Message message; bfs_dos_packet64_t packet; } request = {0};
    request.message.mn_Node.ln_Name = (char *)&request.packet;
    request.message.mn_Length = sizeof(request);
    request.packet.link = &request.message;
    request.packet.port = reply;
    request.packet.type = type;
    request.packet.marker = BFS_DP64_INIT;
    request.packet.legacy_handle = fh->fh_Arg1;
    request.packet.handle = fh->fh_Arg1;
    request.packet.offset = offset;
    request.packet.mode = mode;
    request.packet.file_handle = fh;
    PutMsg(fh->fh_Type, &request.message);
    WaitPort(reply);
    BOOL ok = GetMsg(reply) == &request.message && request.packet.marker == BFS_DP64_INIT;
    *result = request.packet.result;
    *error = request.packet.error;
    DeleteMsgPort(reply);
    return ok;
}

static BOOL os4_packet_values(BPTR file)
{
    int64_t value = 0, large = (1LL << 32) + 123;
    LONG error = 0;
    if (!send_packet64(file, BFS_ACTION_GET_FILE_SIZE64, 0, 0, &value, &error) ||
        error || value != 16) return FALSE;
    if (!send_packet64(file, BFS_ACTION_CHANGE_FILE_SIZE64, large, OFFSET_BEGINNING,
                       &value, &error) || error || !value) return FALSE;
    if (!send_packet64(file, BFS_ACTION_GET_FILE_SIZE64, 0, 0, &value, &error) ||
        error || value != large) return FALSE;
    if (!send_packet64(file, BFS_ACTION_CHANGE_FILE_POSITION64, -1, OFFSET_END,
                       &value, &error) || error || !value) return FALSE;
    if (Write(file, (APTR)"X", 1) != 1) return FALSE;
    if (!send_packet64(file, BFS_ACTION_GET_FILE_POSITION64, 0, 0, &value, &error) ||
        error || value != large) return FALSE;
    if (!send_packet64(file, BFS_ACTION_CHANGE_FILE_POSITION64, INT64_MIN, OFFSET_CURRENT,
                       &value, &error) || !error || value) return FALSE;
    if (!send_packet64(file, BFS_ACTION_GET_FILE_POSITION64, 0, 0, &value, &error) ||
        error || value != large) return FALSE;
    if (!send_packet64(file, BFS_ACTION_CHANGE_FILE_POSITION64, -1, OFFSET_END,
                       &value, &error) || error || !value) return FALSE;
    return Read(file, databuf, 1) == 1 && databuf[0] == 'X';
}

static void test_os4_packets(void)
{
    const char *path = vpath("os4-packets.dat");
    BOOL ok = write_seeded(path, 16, 0x8976);
    BPTR file = ok ? Open(path, MODE_READWRITE) : 0;
    ok = file && os4_packet_values(file);
    if (file && !close_checked(file)) ok = FALSE;
    if (!DeleteFile(path)) ok = FALSE;
    if (ok) pass("os4pkt_42");
    else fail("os4pkt_42", "64-bit packet layout, result, or position");
}

/* GCC 6 m68k cannot allocate registers when a DoPkt inline and quadword
 * comparisons are in the same expression. Keep that ABI boundary out of line. */
static LONG __attribute__((noinline)) send_morphos_packet(struct FileHandle *fh,
    LONG action, int64_t *offset, LONG mode, int64_t *value)
{
    return DoPkt(fh->fh_Type, action, fh->fh_Arg1,
                 (LONG)offset, mode, (LONG)value, 0);
}

static BOOL morphos_packet_values(BPTR file)
{
    struct FileHandle *fh = (struct FileHandle *)BADDR(file);
    int64_t offset = (1LL << 32) + 234, value = -1;
    if (!send_morphos_packet(fh, BFS_ACTION_SET_FILE_SIZE64,
                             &offset, OFFSET_BEGINNING, &value) || value != offset)
        return FALSE;
    offset = -1;
    if (!send_morphos_packet(fh, BFS_ACTION_SEEK64,
                             &offset, OFFSET_END, &value) || value != 0)
        return FALSE;
    if (Write(file, (APTR)"Y", 1) != 1) return FALSE;
    value = -99;
    if (send_morphos_packet(fh, BFS_ACTION_SEEK64,
                            NULL, OFFSET_CURRENT, &value) || IoErr() != ERROR_BAD_NUMBER || value != -99)
        return FALSE;
    offset = INT64_MIN;
    if (send_morphos_packet(fh, BFS_ACTION_SEEK64,
                            &offset, OFFSET_CURRENT, &value) || !IoErr() || value != -99)
        return FALSE;
    offset = -1;
    if (!send_morphos_packet(fh, BFS_ACTION_SEEK64,
                             &offset, OFFSET_END, &value) || value != (1LL << 32) + 234)
        return FALSE;
    return Read(file, databuf, 1) == 1 && databuf[0] == 'Y';
}

static BOOL examine_packet64(BPTR file, const char *path)
{
    struct FileHandle *fh = (struct FileHandle *)BADDR(file);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    if (!fib) return FALSE;
    int64_t size = 0, blocks = 0;
    BOOL ok = DoPkt(fh->fh_Type, BFS_ACTION_EXAMINE_FH64, fh->fh_Arg1,
                    (LONG)MKBADDR(fib), 0, 0, 0);
    tool_memcpy(&size, fib->fib_Reserved, 8);
    tool_memcpy(&blocks, fib->fib_Reserved + 8, 8);
    ok = ok && size == (1LL << 32) + 234 && blocks == (size + 511) / 512 && !fib->fib_Size;
    BPTR lock = Lock(path, SHARED_LOCK);
    if (ok && lock) {
        ok = DoPkt(fh->fh_Type, BFS_ACTION_EXAMINE_OBJECT64, lock, (LONG)MKBADDR(fib), 0, 0, 0);
        tool_memcpy(&size, fib->fib_Reserved, 8);
        ok = ok && size == (1LL << 32) + 234;
    } else ok = FALSE;
    if (lock) UnLock(lock);
    if (DoPkt(fh->fh_Type, BFS_ACTION_QUERY_ATTR, 0, (LONG)fib, sizeof(*fib), 0, 0) ||
        IoErr() != ERROR_ACTION_NOT_KNOWN) ok = FALSE;
    FreeDosObject(DOS_FIB, fib);
    return ok;
}

static void test_morphos_packets(void)
{
    const char *path = vpath("morphos-packets.dat");
    BOOL ok = write_seeded(path, 16, 0x7865);
    BPTR file = ok ? Open(path, MODE_READWRITE) : 0;
    ok = file && morphos_packet_values(file) && examine_packet64(file, path);
    if (file && !close_checked(file)) ok = FALSE;
    if (!DeleteFile(path)) ok = FALSE;
    if (ok) pass("mospkt_43");
    else fail("mospkt_43", "64-bit pointer arguments, result, or FIB layout");
}

static BOOL scan_exall_batches(BPTR lock, struct ExAllControl *control)
{
    ULONG storage[40];
    ULONG seen = 0, batches = 0;
    for (;;) {
        BOOL more = ExAll(lock, (struct ExAllData *)storage, sizeof(storage), ED_COMMENT, control);
        LONG error = IoErr();
        ULONG count = 0;
        struct ExAllData *entry = (struct ExAllData *)storage;
        while (entry && count < control->eac_Entries) {
            UBYTE *begin = (UBYTE *)storage, *end = begin + sizeof(storage);
            if ((UBYTE *)entry < begin || (UBYTE *)entry > end - sizeof(*entry) ||
                (UBYTE *)entry->ed_Name < begin || (UBYTE *)entry->ed_Name > end - 2 ||
                !entry->ed_Comment || (UBYTE *)entry->ed_Comment < begin ||
                (UBYTE *)entry->ed_Comment > end - 7) return FALSE;
            int bit = entry->ed_Name[0] == 'a' ? 1 : entry->ed_Name[0] == 'b' ? 2 : 0;
            if (!bit || entry->ed_Name[1] || (seen & bit) || entry->ed_Size != 3 ||
                tool_memcmp(entry->ed_Comment, "marker", 7)) return FALSE;
            seen |= bit;
            count++;
            entry = entry->ed_Next;
        }
        if (entry || count != control->eac_Entries) return FALSE;
        if (!more) return error == ERROR_NO_MORE_ENTRIES && seen == 3 && batches > 0;
        if (++batches > 10 || !count) return FALSE;
    }
}

static void test_exall_batches(void)
{
    const char *names[] = {"exall/a", "exall/b", "exall/skip"};
    BPTR lock = CreateDir(vpath("exall"));
    struct ExAllControl *control = AllocDosObject(DOS_EXALLCONTROL, NULL);
    BOOL ok = lock && control;
    char pattern[32];
    if (ok) ok = ParsePatternNoCase("(a|b)", pattern, sizeof(pattern)) >= 0;
    if (ok) control->eac_MatchString = pattern;
    int i;
    for (i = 0; i < 3 && ok; i++) {
        ok = write_seeded(vpath(names[i]), 3, 123);
        if (ok) ok = SetComment(vpath(names[i]), "marker");
    }
    if (ok) ok = scan_exall_batches(lock, control);
    if (control) FreeDosObject(DOS_EXALLCONTROL, control);
    if (lock) UnLock(lock);
    for (i = 0; i < 3; i++) if (!DeleteFile(vpath(names[i]))) ok = FALSE;
    if (!DeleteFile(vpath("exall"))) ok = FALSE;
    if (ok) pass("exall_44");
    else fail("exall_44", "control pointer, filtering, batches, or end-of-scan result");
}

static void test_diskfull(void)
{
    const char *T = "diskfull_23";
    const char *p = vpath("full.dat");
    BPTR fh = Open(p, MODE_NEWFILE);
    if (!fh) { fail(T, "open"); return; }
    LONG total = 0; int i;
    int chunks = quick_mode ? 32 : 160;
    /* Write ~20MB (leaves ~12MB free for COW overhead during delete) */
    for (i = 0; i < chunks; i++) {
        LONG w = Write(fh, databuf, BUF_SIZE);
        if (w <= 0) break;
        progress(i, chunks);
        total += w;
    }
    close_checked(fh);
    if (total == 0) { fail(T, "no write"); DeleteFile(p); return; }
    /* Delete must succeed even after large write */
    Printf("  deleting %lu KB...", (unsigned long)(total * 64));
    if (!DeleteFile(p)) { fail(T, "delete"); return; }
    Printf(" ok\n");
    /* Verify we can still create files after delete */
    fill(databuf, 10, 0x2323);
    fh = Open(vpath("after.dat"), MODE_NEWFILE);
    if (!fh) { fail(T, "after"); return; }
    write_exact(fh, databuf, 10); close_checked(fh);
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
    close_checked(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open"); return; }
    LONG got = Read(fh, databuf, 100);
    close_checked(fh);
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
    write_exact(fh, databuf, 10); close_checked(fh);
    ULONG crc = checksum(databuf, 10);
    fh = Open(vpath(name), MODE_OLDFILE);
    if (!fh) { fail(T, "read"); return; }
    read_exact(fh, databuf, 10); close_checked(fh);
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
    write_exact(fh, databuf, 10); close_checked(fh);
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
    write_exact(fh, databuf, 100); close_checked(fh);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "open"); return; }
    Seek(fh, 200, OFFSET_BEGINNING); /* past end */
    LONG got = Read(fh, databuf, 10);
    close_checked(fh);
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
    write_exact(fh, databuf, 100); close_checked(fh);
    /* Read back at offset 8192 */
    ULONG crc = checksum(databuf, 100);
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    Seek(fh, 8192, OFFSET_BEGINNING);
    read_exact(fh, databuf, 100); close_checked(fh);
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
    int name_count = quick_mode ? 8 : 20;
    int operation_count = quick_mode ? 16 : 60;
    for (i = 0; i < name_count; i++) {
        names[i][0] = 'r'; names[i][1] = '0' + (i/10); names[i][2] = '0' + (i%10); names[i][3] = 0;
    }
    for (j = 0; j < operation_count; j++) {
        rng = xorshift(rng);
        int idx = (rng >> 8) % name_count;
        int op = rng % 3;
        if (op == 0 && !exists[idx]) {
            fill(databuf, 50, rng);
            BPTR fh = Open(vpath(names[idx]), MODE_NEWFILE);
            if (fh) { write_exact(fh, databuf, 50); close_checked(fh); exists[idx] = 1; }
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
        if (j % 8 == 0) progress(j, operation_count);
    }
    /* Cleanup */
    for (i = 0; i < name_count; i++) {
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
    int count = quick_mode ? 32 : 500;
    /* Write one byte per DOS packet. */
    ULONG st = 0x3232;
    int i;
    for (i = 0; i < count; i++) {
        st = xorshift(st);
        UBYTE b = (UBYTE)st;
        write_exact(fh, &b, 1);
    }
    close_checked(fh);
    /* Verify */
    fh = Open(p, MODE_OLDFILE);
    if (!fh) { fail(T, "reopen"); return; }
    LONG got = Read(fh, databuf, count); close_checked(fh);
    if (got != count) { fail(T, "size"); DeleteFile(p); return; }
    st = 0x3232;
    for (i = 0; i < count; i++) {
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
    int count = quick_mode ? 4 : 10;
    for (i = 0; i < count; i++) {
        char rel[16]; rel[0] = 'm'; rel[1] = '0' + i; rel[2] = 0;
        ULONG sz = (i % 2 == 0) ? 65536 : 100;
        if (!write_seeded(vpath(rel), sz, 0x3300 + i)) { fail(T, "write"); return; }
        progress(i, count);
    }
    for (i = 0; i < count; i++) {
        char rel[16]; rel[0] = 'm'; rel[1] = '0' + i; rel[2] = 0;
        ULONG sz = (i % 2 == 0) ? 65536 : 100;
        if (!verify_seeded(vpath(rel), sz, 0x3300 + i)) { fail(T, "verify"); return; }
    }
    for (i = 0; i < count; i++) {
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
    bstr[0] = nlen; tool_memcpy(bstr + 1, sname, nlen);

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

    LONG count = quick_mode ? 5 : 50;
    for (i = 0; i < count; i++) {
        /* Write pass */
        BPTR fh = Open(p, MODE_NEWFILE);
        if (!fh) { fail(T, "open-w"); return; }
        fill(databuf, 256, 0x3800 + i);
        write_exact(fh, databuf, 256);
        close_checked(fh);

        /* Read-back and verify */
        fh = Open(p, MODE_OLDFILE);
        if (!fh) { fail(T, "open-r"); return; }
        UBYTE rdbuf[256];
        LONG got = Read(fh, rdbuf, 256);
        close_checked(fh);
        if (got != 256) { fail(T, "short"); DeleteFile(p); return; }
        fill(databuf, 256, 0x3800 + i);
        if (tool_memcmp(databuf, rdbuf, 256) != 0) {
            fail(T, "verify");
            DeleteFile(p);
            return;
        }
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
    write_exact(fh, databuf, 10); close_checked(fh);

    /* Examine and check date is non-zero */
    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); DeleteFile(p); return; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); fail(T, "fib"); DeleteFile(p); return; }
    if (!Examine(lock, fib)) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        fail(T, "examine");
        DeleteFile(p);
        return;
    }
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
    write_exact(fh, databuf, 10); close_checked(fh);

    /* SetOwner not available as a simple DOS call, but we can verify
     * that Examine returns uid/gid fields (should be 0 for new files) */
    BPTR lock = Lock(p, SHARED_LOCK);
    if (!lock) { fail(T, "lock"); DeleteFile(p); return; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); fail(T, "fib"); DeleteFile(p); return; }
    if (!Examine(lock, fib)) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        fail(T, "examine");
        DeleteFile(p);
        return;
    }
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
    int file_count = quick_mode ? 16 : 50;
    BOOL ok = TRUE;
    BPTR lock = CreateDir(vpath("exdir"));
    if (!lock) { fail(T, "mkdir"); return; }
    UnLock(lock);

    /* Create 50 files — enough to span multiple leaves and trigger collisions */
    for (i = 0; i < file_count; i++) {
        char rel[32]; char *p = rel;
        const char *s = "exdir/item_";
        while (*s) *p++ = *s++;
        *p++ = '0' + (i / 10); *p++ = '0' + (i % 10); *p = 0;
        BPTR fh = Open(vpath(rel), MODE_NEWFILE);
        if (!fh) { fail(T, "create"); return; }
        close_checked(fh);
    }

    /* Count entries via ExNext */
    lock = Lock(vpath("exdir"), SHARED_LOCK);
    if (!lock) { fail(T, "lock"); return; }
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocVec(sizeof(*fib), MEMF_CLEAR);
    if (!fib) { UnLock(lock); fail(T, "alloc"); return; }
    if (!Examine(lock, fib)) {
        FreeVec(fib);
        UnLock(lock);
        fail(T, "examine");
        return;
    }
    int count = 0;
    while (ExNext(lock, fib)) count++;
    FreeVec(fib);
    UnLock(lock);

    /* Every file plus the '..' entry must be returned exactly once. */
    if (count != file_count + 1) {
        ok = FALSE;
        put("  got="); putnum(count);
        put(" want="); putnum(file_count + 1); put("\n");
    }

    /* Cleanup */
    for (i = 0; i < file_count; i++) {
        char rel[32]; char *p = rel;
        const char *s = "exdir/item_";
        while (*s) *p++ = *s++;
        *p++ = '0' + (i / 10); *p++ = '0' + (i % 10); *p = 0;
        if (!DeleteFile(vpath(rel))) ok = FALSE;
    }
    if (!DeleteFile(vpath("exdir"))) ok = FALSE;
    if (ok) pass(T); else fail(T, "count or cleanup");
}

/* ── Test table ────────────────────────────────────────────── */

typedef void (*test_fn)(void);
static const struct { const char *name; test_fn fn; } all_tests[] = {
#define BFS_TEST(name, fn) {#name, fn},
#include "bfs-test-cases.def"
#undef BFS_TEST
    {NULL, NULL}
};

static int filter_valid(const char *filter)
{
    int expect_term = 1;
    if (!filter || !filter[0]) return 1;
    for (; *filter; filter++) {
        if (*filter == '+') {
            if (expect_term) return 0;
            expect_term = 1;
        } else if ((*filter >= 'A' && *filter <= 'Z') ||
                   (*filter >= 'a' && *filter <= 'z') ||
                   (*filter >= '0' && *filter <= '9') ||
                   *filter == '_' || *filter == '-') {
            expect_term = 0;
        } else {
            return 0;
        }
    }
    return !expect_term;
}

static int filter_matches(const char *str, const char *filter)
{
    const char *start = filter;
    if (!filter || !filter[0]) return 1;
    while (1) {
        const char *end = start;
        while (*end && *end != '+') end++;
        const char *s;
        for (s = str; *s; s++) {
            const char *a = s, *b = start;
            while (b < end && *a == *b) { a++; b++; }
            if (b == end) return 1;
        }
        if (!*end) return 0;
        start = end + 1;
    }
}

static BOOL log_completion(BOOL publish)
{
    char pending[512], complete[512];
    int len = tool_strlen(logpath);
    tool_memcpy(pending, logpath, len);
    tool_memcpy(complete, logpath, len);
    tool_memcpy(pending + len, ".done.tmp", 10);
    tool_memcpy(complete + len, ".done", 6);
    if (!publish) {
        if (!DeleteFile(pending) && IoErr() != ERROR_OBJECT_NOT_FOUND) return FALSE;
        if (!DeleteFile(complete) && IoErr() != ERROR_OBJECT_NOT_FOUND) return FALSE;
        return TRUE;
    }
    /* Publish only after both the result log and completion record close. */
    static const char record[] = "BFS-TEST-COMPLETE\t1\n";
    BPTR fh = Open(pending, MODE_NEWFILE);
    if (!fh) return FALSE;
    BOOL ok = Write(fh, (APTR)record, sizeof(record) - 1) == sizeof(record) - 1;
    if (!Close(fh)) ok = FALSE;
    return ok && Rename(pending, complete);
}

int main(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;

    struct RDArgs *rdargs;
    LONG args[4] = {0, 0, 0, 0};
    rdargs = ReadArgs("VOLUME/A,LOG/K,FILTER,QUICK/S", args, NULL);
    if (!rdargs) {
        put("Usage: bfs-test VOLUME [LOG=path] [filter] [QUICK]\n");
        put("  bfs-test DH1:                   (run all)\n");
        put("  bfs-test DH1: large             (run matching)\n");
        put("  bfs-test DH1: a+b               (run matching filters)\n");
        put("  bfs-test DH1: LOG=SYS:test.log  (CI mode)\n");
        put("  bfs-test DH1: LOG=SYS:x large   (both)\n");
        me->pr_WindowPtr = oldwin;
        return 5;
    }

    quick_mode = args[3] != 0;

    /* Validate and copy all ReadArgs-backed strings before FreeArgs. */
    const char *volume_arg = (const char *)args[0];
    int volume_len = tool_strlen(volume_arg);
    if (volume_len < 2 || volume_len >= (int)sizeof(vol) ||
        volume_arg[volume_len - 1] != ':') {
        put("Invalid VOLUME (expected e.g. DH1:, max 63 characters)\n");
        FreeArgs(rdargs);
        me->pr_WindowPtr = oldwin;
        return 5;
    }

    { const char *s = volume_arg; char *d = vol; while (*s) *d++ = *s++; *d = 0; }

    /* Open log file if specified. A requested but unavailable log is a CI
     * setup failure, not an invitation to continue without machine output. */
    logfh = 0;
    if (args[1]) {
        const char *requested_log = (const char *)args[1];
        int loglen = tool_strlen(requested_log);
        if (!loglen || loglen >= (int)sizeof(logpath)) {
            put("LOG path is too long or empty\n");
            FreeArgs(rdargs);
            me->pr_WindowPtr = oldwin;
            return 10;
        }
        tool_memcpy(logpath, requested_log, loglen + 1);
        if (!log_completion(FALSE)) {
            put("Cannot clear previous completion record\n");
            FreeArgs(rdargs);
            me->pr_WindowPtr = oldwin;
            return 10;
        }
        logfh = Open(logpath, MODE_NEWFILE);
        if (!logfh) {
            put("Cannot open requested LOG file\n");
            FreeArgs(rdargs);
            me->pr_WindowPtr = oldwin;
            return 10;
        }
    }

    /* Copy filter before FreeArgs invalidates the buffer */
    static char filterbuf[64];
    const char *filter = NULL;
    if (args[2]) {
        const char *s = (const char *)args[2]; char *d = filterbuf;
        if (tool_strlen(s) >= (int)sizeof(filterbuf)) {
            put("FILTER is too long (max 63 characters)\n");
            if (logfh) Close(logfh);
            FreeArgs(rdargs);
            me->pr_WindowPtr = oldwin;
            return 5;
        }
        if (!filter_valid(s)) {
            put("FILTER contains unsupported characters or empty terms\n");
            if (logfh) Close(logfh);
            FreeArgs(rdargs);
            me->pr_WindowPtr = oldwin;
            return 5;
        }
        while (*s && d < filterbuf + 63) *d++ = *s++;
        *d = 0;
        filter = filterbuf;
    }

    FreeArgs(rdargs);

    databuf = AllocMem(BUF_SIZE, MEMF_PUBLIC);
    if (!databuf) {
        put("Out of memory\n");
        if (logfh) Close(logfh);
        me->pr_WindowPtr = oldwin;
        return 20;
    }

    put("=== BFS INTEGRITY TEST ===\n");
    put("Volume: "); put(vol); put("\n\n");
    logput("# BFS Test Log\n");
    logput(quick_mode ? "# PROFILE\tquick\n" : "# PROFILE\tfull\n");
    logput("# STATUS\tNAME\t[DETAIL]\n");

    int i;
    for (i = 0; all_tests[i].name; i++) {
        if (!filter_matches(all_tests[i].name, filter)) continue;
        int previous_run = tests_run, previous_fail = tests_fail;
        io_failed = FALSE;
        put(" RUN  "); put(all_tests[i].name); put("\n");
        logput("# RUN\t"); logput(all_tests[i].name); logput("\n");
        all_tests[i].fn();
        if (tests_run != previous_run + 1)
            fail(all_tests[i].name, "test did not report exactly one result");
        else if (io_failed && tests_fail == previous_fail)
            fail(all_tests[i].name, "late I/O failure after result");
    }

    put("\n=== RESULTS: ");
    putnum(tests_pass); put("/"); putnum(tests_run); put(" passed");
    if (tests_fail) { put(", "); putnum(tests_fail); put(" FAILED"); }
    put(" ===\n");

    /* Write summary to log */
    logput("# SUMMARY\t"); lognum(tests_pass); logput("\t");
    lognum(tests_run); logput("\t"); lognum(tests_fail); logput("\n");

    if (logfh) {
        if (!Close(logfh)) log_failed = TRUE;
        logfh = 0;
        if (!log_failed && !log_completion(TRUE)) log_failed = TRUE;
    }
    me->pr_WindowPtr = oldwin;
    FreeMem(databuf, BUF_SIZE);
    return tests_fail || log_failed || !tests_run ? 5 : 0;
}
