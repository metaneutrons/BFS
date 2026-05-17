/*
 * BFS Filesystem Stress Test — Amiga m68k binary
 *
 * Compiled with: m68k-amigaos-gcc -noixemul -m68020 -O2
 * Usage: bfs-stresstest DH0:
 * Output: TEST <name> PASS/FAIL <details>
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* ── Constants ─────────────────────────────────────────────── */

#define PATTERN_XOR   0xDEADBEEF
#define BUF_SIZE      8192
#define NUM_SMALL     200
#define NUM_DIRS      5
#define FILES_PER_DIR 20
#define FRAG_FILES    30

/* ── Static buffers (BSS) ──────────────────────────────────── */

static UBYTE buf[BUF_SIZE];
static UBYTE vbuf[BUF_SIZE];
static char pathbuf[256];
static char pathbuf2[256];
static char outbuf[128];

static LONG pass_count;
static LONG fail_count;

/* ── Helpers ───────────────────────────────────────────────── */

static ULONG checksum(UBYTE *data, ULONG len) {
    ULONG crc = 0;
    ULONG i;
    for (i = 0; i < len; i++) {
        crc = (crc << 1) | (crc >> 31);
        crc ^= data[i];
    }
    return crc;
}

static void str_copy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void str_cat(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void uint_to_hex(ULONG val, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 7; i >= 0; i--) {
        out[i] = hex[val & 0xF];
        val >>= 4;
    }
    out[8] = 0;
}

static void int_to_dec(LONG val, char *out) {
    char tmp[12];
    int i = 0, j = 0;
    if (val == 0) { out[0] = '0'; out[1] = 0; return; }
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

static void emit(const char *s) {
    BPTR out = Output();
    LONG len = 0;
    const char *p = s;
    while (*p++) len++;
    Write(out, (APTR)s, len);
}

static void report(const char *name, BOOL ok, ULONG crc) {
    char hexbuf[9];
    emit("TEST ");
    emit(name);
    if (ok) { emit(" PASS"); pass_count++; }
    else    { emit(" FAIL"); fail_count++; }
    if (crc) {
        emit(" crc=");
        uint_to_hex(crc, hexbuf);
        emit(hexbuf);
    }
    emit("\n");
}

static void fill_pattern(UBYTE *b, ULONG offset, ULONG len) {
    ULONG *p = (ULONG *)b;
    ULONG words = len >> 2;
    ULONG i;
    for (i = 0; i < words; i++)
        p[i] = (offset + i) ^ PATTERN_XOR;
}

static void make_path(const char *base, const char *name) {
    str_copy(pathbuf, base);
    str_cat(pathbuf, name);
}

static void make_filename(const char *base, const char *prefix, int n) {
    char numbuf[8];
    int_to_dec(n, numbuf);
    str_copy(pathbuf, base);
    str_cat(pathbuf, prefix);
    str_cat(pathbuf, numbuf);
}

/* ── Phase 1: Large files ──────────────────────────────────── */

static void phase1(const char *base) {
    ULONG sizes[2] = { 1048576, 4194304 };
    const char *names[2] = { "phase1_1mb", "phase1_4mb" };
    int f;

    for (f = 0; f < 2; f++) {
        ULONG size = sizes[f];
        ULONG written = 0, offset = 0;
        ULONG wcrc = 0;
        BPTR fh;
        char tname[32];

        make_path(base, names[f]);
        fh = Open(pathbuf, MODE_NEWFILE);
        if (!fh) { report(names[f], FALSE, 0); continue; }

        while (written < size) {
            ULONG chunk = (size - written < BUF_SIZE) ? size - written : BUF_SIZE;
            fill_pattern(buf, offset, chunk);
            wcrc = checksum(buf, chunk) ^ wcrc;
            if (Write(fh, buf, chunk) != (LONG)chunk) { Close(fh); report(names[f], FALSE, 0); goto next; }
            written += chunk;
            offset += chunk >> 2;
        }
        Close(fh);

        str_copy(tname, names[f]);
        str_cat(tname, "_write");
        report(tname, TRUE, wcrc);

        /* Read back and verify */
        fh = Open(pathbuf, MODE_OLDFILE);
        if (!fh) { str_copy(tname, names[f]); str_cat(tname, "_read"); report(tname, FALSE, 0); continue; }

        {
            ULONG rcrc = 0, readtotal = 0;
            offset = 0;
            while (readtotal < size) {
                ULONG chunk = (size - readtotal < BUF_SIZE) ? size - readtotal : BUF_SIZE;
                LONG got = Read(fh, vbuf, chunk);
                if (got != (LONG)chunk) { Close(fh); str_copy(tname, names[f]); str_cat(tname, "_read"); report(tname, FALSE, 0); goto next; }
                /* Verify pattern */
                fill_pattern(buf, offset, chunk);
                rcrc = checksum(vbuf, chunk) ^ rcrc;
                if (rcrc != (checksum(buf, chunk) ^ (rcrc ^ checksum(vbuf, chunk)))) {
                    /* Just compare buffers directly */
                }
                readtotal += chunk;
                offset += chunk >> 2;
            }
            Close(fh);
            str_copy(tname, names[f]);
            str_cat(tname, "_read");
            report(tname, rcrc == wcrc, rcrc);
        }
        next:;
    }
}

/* ── Phase 2: Many small files ─────────────────────────────── */

static void phase2(const char *base) {
    int i;
    ULONG crcs[NUM_SMALL];
    BOOL ok = TRUE;

    /* Create 200 files */
    for (i = 0; i < NUM_SMALL; i++) {
        ULONG size = 1024 + ((i * 37) % 8) * 1024;
        BPTR fh;
        make_filename(base, "sf", i);
        fh = Open(pathbuf, MODE_NEWFILE);
        if (!fh) { ok = FALSE; break; }
        fill_pattern(buf, i * 256, (size < BUF_SIZE) ? size : BUF_SIZE);
        Write(fh, buf, (size < BUF_SIZE) ? size : BUF_SIZE);
        crcs[i] = checksum(buf, (size < BUF_SIZE) ? size : BUF_SIZE);
        Close(fh);
    }
    report("phase2_create", ok, 0);

    /* Verify all */
    ok = TRUE;
    for (i = 0; i < NUM_SMALL; i++) {
        ULONG size = 1024 + ((i * 37) % 8) * 1024;
        ULONG rsize = (size < BUF_SIZE) ? size : BUF_SIZE;
        BPTR fh;
        make_filename(base, "sf", i);
        fh = Open(pathbuf, MODE_OLDFILE);
        if (!fh) { ok = FALSE; break; }
        Read(fh, vbuf, rsize);
        Close(fh);
        if (checksum(vbuf, rsize) != crcs[i]) { ok = FALSE; break; }
    }
    report("phase2_verify", ok, 0);

    /* Delete even-numbered files */
    ok = TRUE;
    for (i = 0; i < NUM_SMALL; i += 2) {
        make_filename(base, "sf", i);
        if (!DeleteFile(pathbuf)) { ok = FALSE; break; }
    }
    report("phase2_delete", ok, 0);

    /* Verify odd files still intact */
    ok = TRUE;
    for (i = 1; i < NUM_SMALL; i += 2) {
        ULONG size = 1024 + ((i * 37) % 8) * 1024;
        ULONG rsize = (size < BUF_SIZE) ? size : BUF_SIZE;
        BPTR fh;
        make_filename(base, "sf", i);
        fh = Open(pathbuf, MODE_OLDFILE);
        if (!fh) { ok = FALSE; break; }
        Read(fh, vbuf, rsize);
        Close(fh);
        if (checksum(vbuf, rsize) != crcs[i]) { ok = FALSE; break; }
    }
    report("phase2_survivors", ok, 0);
}

/* ── Phase 3: Directory stress ─────────────────────────────── */

static void phase3(const char *base) {
    int d, f;
    BOOL ok = TRUE;
    char dirbuf[64];

    /* Create 5 dirs × 20 files */
    for (d = 0; d < NUM_DIRS; d++) {
        char dname[16];
        BPTR lock;
        int_to_dec(d, dname);
        str_copy(pathbuf, base);
        str_cat(pathbuf, "dir");
        str_cat(pathbuf, dname);
        lock = CreateDir(pathbuf);
        if (!lock) { ok = FALSE; break; }
        UnLock(lock);

        str_copy(dirbuf, pathbuf);
        str_cat(dirbuf, "/");

        for (f = 0; f < FILES_PER_DIR; f++) {
            BPTR fh;
            char fname[16];
            int_to_dec(f, fname);
            str_copy(pathbuf, dirbuf);
            str_cat(pathbuf, "f");
            str_cat(pathbuf, fname);
            fh = Open(pathbuf, MODE_NEWFILE);
            if (!fh) { ok = FALSE; break; }
            fill_pattern(buf, d * 100 + f, 512);
            Write(fh, buf, 512);
            Close(fh);
        }
        if (!ok) break;
    }
    report("phase3_create", ok, 0);

    /* Rename 50 files across dirs */
    ok = TRUE;
    for (f = 0; f < 50; f++) {
        int src_d = f % NUM_DIRS;
        int dst_d = (f + 1) % NUM_DIRS;
        int src_f = f / NUM_DIRS;
        char num1[8], num2[8], num3[8], num4[8];

        int_to_dec(src_d, num1);
        int_to_dec(src_f, num2);
        int_to_dec(dst_d, num3);
        int_to_dec(f, num4);

        str_copy(pathbuf, base);
        str_cat(pathbuf, "dir"); str_cat(pathbuf, num1);
        str_cat(pathbuf, "/f"); str_cat(pathbuf, num2);

        str_copy(pathbuf2, base);
        str_cat(pathbuf2, "dir"); str_cat(pathbuf2, num3);
        str_cat(pathbuf2, "/r"); str_cat(pathbuf2, num4);

        if (!Rename(pathbuf, pathbuf2)) { ok = FALSE; break; }
    }
    report("phase3_rename", ok, 0);

    /* Count entries in all dirs */
    {
        LONG total = 0;
        struct FileInfoBlock fib;
        for (d = 0; d < NUM_DIRS; d++) {
            char dname[8];
            BPTR lock;
            int_to_dec(d, dname);
            str_copy(pathbuf, base);
            str_cat(pathbuf, "dir"); str_cat(pathbuf, dname);
            lock = Lock(pathbuf, SHARED_LOCK);
            if (!lock) continue;
            if (Examine(lock, &fib)) {
                while (ExNext(lock, &fib)) total++;
            }
            UnLock(lock);
        }
        ok = (total == (NUM_DIRS * FILES_PER_DIR));
        report("phase3_count", ok, (ULONG)total);
    }
}

/* ── Phase 4: Fragmentation ───────────────────────────────── */

static void phase4(const char *base) {
    int i;
    BOOL ok = TRUE;
    ULONG crcs[FRAG_FILES];

    /* Create files of varying sizes */
    for (i = 0; i < FRAG_FILES; i++) {
        ULONG size = 2048 + (i * 1024);
        ULONG wsize = (size < BUF_SIZE) ? size : BUF_SIZE;
        BPTR fh;
        make_filename(base, "frag", i);
        fh = Open(pathbuf, MODE_NEWFILE);
        if (!fh) { ok = FALSE; break; }
        fill_pattern(buf, i * 512, wsize);
        Write(fh, buf, wsize);
        crcs[i] = checksum(buf, wsize);
        Close(fh);
    }
    report("phase4_create", ok, 0);

    /* Delete every 3rd file to fragment */
    ok = TRUE;
    for (i = 0; i < FRAG_FILES; i += 3) {
        make_filename(base, "frag", i);
        if (!DeleteFile(pathbuf)) { ok = FALSE; break; }
    }
    report("phase4_delete", ok, 0);

    /* Create new files in the gaps */
    ok = TRUE;
    for (i = 0; i < FRAG_FILES; i += 3) {
        ULONG size = 4096 + (i * 512);
        ULONG wsize = (size < BUF_SIZE) ? size : BUF_SIZE;
        BPTR fh;
        make_filename(base, "frag", i);
        fh = Open(pathbuf, MODE_NEWFILE);
        if (!fh) { ok = FALSE; break; }
        fill_pattern(buf, i * 512 + 1, wsize);
        Write(fh, buf, wsize);
        crcs[i] = checksum(buf, wsize);
        Close(fh);
    }
    report("phase4_refill", ok, 0);

    /* Verify survivors (non-deleted originals) */
    ok = TRUE;
    for (i = 1; i < FRAG_FILES; i++) {
        ULONG size, wsize;
        BPTR fh;
        if ((i % 3) == 0) continue;
        size = 2048 + (i * 1024);
        wsize = (size < BUF_SIZE) ? size : BUF_SIZE;
        make_filename(base, "frag", i);
        fh = Open(pathbuf, MODE_OLDFILE);
        if (!fh) { ok = FALSE; break; }
        Read(fh, vbuf, wsize);
        Close(fh);
        if (checksum(vbuf, wsize) != crcs[i]) { ok = FALSE; break; }
    }
    report("phase4_verify", ok, 0);
}

/* ── Main ──────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    char base[64];
    char numbuf[8];

    if (argc < 2) {
        emit("Usage: bfs-stresstest DH0:\n");
        return 5;
    }

    str_copy(base, argv[1]);
    /* Ensure trailing slash/colon */
    {
        char *p = base;
        while (*p) p++;
        if (p > base && *(p-1) != ':' && *(p-1) != '/') {
            *p++ = '/'; *p = 0;
        }
    }

    pass_count = 0;
    fail_count = 0;

    phase1(base);
    phase2(base);
    phase3(base);
    phase4(base);

    /* Summary */
    emit("SUMMARY ");
    int_to_dec(pass_count, numbuf);
    emit(numbuf);
    emit("/");
    int_to_dec(pass_count + fail_count, numbuf);
    emit(numbuf);
    emit(" PASS\n");

    return (fail_count > 0) ? 10 : 0;
}
