/* SPDX-License-Identifier: MPL-2.0 */
/* hitrate-check [DEVICE]
 *
 * Scripted run of the hit-rate counter of a debug build of the BFS handler.
 * It lists directories in a fixed order, changes the volume in between, and
 * compares every counter with the value that follows from the definitions of
 * docs/plans/bfs-exall-result-cache-v1.md. The expected classes are written
 * down by hand below, not computed by the counter's own rules. It validates the
 * counter and is not evidence of real use. */
#include <exec/memory.h>
#include <dos/exall.h>
#include <dos/dosextens.h>

#include "hitrate-common.h"

#define FILES 12
#define BUFFER_BYTES 4096
#define SMALL_BUFFER_BYTES 200

static const char *device = "BFS:";
static ULONG failures;

typedef struct {
    int kind;
    ULONG entries;
    BOOL complete;
    int cls[HR_TESTS][HR_SCOPES]; /* expected class by test and scope */
} expectation_t;

static expectation_t expectations[32];
static int expectation_count;
static ULONG expected_calls[HR_KINDS];

static void fail(const char *what)
{
    hr_emit("FAIL ");
    hr_emit(what);
    hr_emit("\n");
    failures++;
}

static void say(const char *what, ULONG value)
{
    hr_emit(what);
    hr_emit(" ");
    hr_emit_number(value);
    hr_emit("\n");
}

static void path(char *out, const char *directory, const char *name)
{
    ULONG n = 0;
    for (const char *p = device; *p; p++) out[n++] = *p;
    for (const char *p = directory; *p; p++) out[n++] = *p;
    if (name) {
        out[n++] = '/';
        for (const char *p = name; *p; p++) out[n++] = *p;
    }
    out[n] = 0;
}

static BOOL make_file(const char *directory, const char *name)
{
    char full[96];
    path(full, directory, name);
    BPTR file = Open(full, MODE_NEWFILE);
    if (!file) return FALSE;
    return Close(file) != 0;
}

/* Commit now, so that a delayed commit cannot move the volume's stamps later. */
static BOOL flush_volume(void)
{
    struct MsgPort *port = DeviceProc(device);
    return port && DoPkt(port, ACTION_FLUSH, 0, 0, 0, 0, 0) == DOSTRUE;
}

static BOOL make_directory(const char *directory, int files)
{
    char full[96], name[8];
    path(full, directory, NULL);
    BPTR lock = CreateDir(full);
    if (!lock) return FALSE;
    UnLock(lock);
    for (int i = 1; i <= files; i++) {
        name[0] = 'f';
        name[1] = (char)('0' + i / 10);
        name[2] = (char)('0' + i % 10);
        name[3] = 0;
        if (!make_file(directory, name)) return FALSE;
    }
    return flush_volume();
}

/* One ExAll pass. Returns the entries counted and the calls made. With
 * stop_after_first the pass ends after one call with ExAllEnd. */
static void exall_pass(BPTR lock, ULONG buffer_bytes, const char *pattern, BOOL stop_after_first,
                       ULONG *entries, ULONG *calls, BOOL *complete)
{
    char parsed[32];
    UBYTE *buffer = AllocVec(buffer_bytes, MEMF_ANY);
    struct ExAllControl *control = AllocDosObject(DOS_EXALLCONTROL, NULL);
    *entries = *calls = 0;
    *complete = FALSE;
    if (!buffer || !control) {
        fail("allocation");
        goto done;
    }
    control->eac_LastKey = 0;
    control->eac_MatchFunc = NULL;
    control->eac_MatchString = NULL;
    if (pattern) {
        if (ParsePatternNoCase((STRPTR)pattern, (STRPTR)parsed, sizeof(parsed)) < 0) {
            fail("pattern");
            goto done;
        }
        control->eac_MatchString = (STRPTR)parsed;
    }
    BOOL more;
    do {
        more = ExAll(lock, (struct ExAllData *)buffer, (LONG)buffer_bytes, ED_COMMENT, control);
        (*calls)++;
        if (!more && IoErr() != ERROR_NO_MORE_ENTRIES) {
            fail("ExAll error");
            break;
        }
        *entries += control->eac_Entries;
        if (!more) *complete = TRUE;
        if (stop_after_first) break;
    } while (more);
    if (more) ExAllEnd(lock, (struct ExAllData *)buffer, (LONG)buffer_bytes, ED_COMMENT, control);
done:
    if (control) FreeDosObject(DOS_EXALLCONTROL, control);
    if (buffer) FreeVec(buffer);
}

static void exnext_pass(BPTR lock, ULONG *entries, ULONG *calls)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    *entries = *calls = 0;
    if (!fib) {
        fail("allocation");
        return;
    }
    if (!Examine(lock, fib)) {
        fail("Examine");
    } else {
        for (;;) {
            BOOL got = ExNext(lock, fib);
            (*calls)++;
            if (!got) break;
            (*entries)++;
        }
        if (IoErr() != ERROR_NO_MORE_ENTRIES) fail("ExNext error");
    }
    FreeDosObject(DOS_FIB, fib);
}

/* The expected classes: stamps and kind scope, stamps and any scope, counter and
 * kind scope, counter and any scope. */
static void expect(int kind, ULONG entries, ULONG calls, BOOL complete, int sk, int sa, int ck, int ca)
{
    expectation_t *e = &expectations[expectation_count++];
    e->kind = kind;
    e->entries = entries;
    e->complete = complete;
    e->cls[HR_TEST_STAMPS][HR_SCOPE_KIND] = sk;
    e->cls[HR_TEST_STAMPS][HR_SCOPE_ANY] = sa;
    e->cls[HR_TEST_COUNTER][HR_SCOPE_KIND] = ck;
    e->cls[HR_TEST_COUNTER][HR_SCOPE_ANY] = ca;
    expected_calls[kind] += calls;
}

static void check_count(const char *name, ULONG expected, ULONG actual)
{
    if (expected == actual) return;
    hr_emit("FAIL ");
    hr_emit(name);
    hr_emit(" expected ");
    hr_emit_number(expected);
    hr_emit(" actual ");
    hr_emit_number(actual);
    hr_emit("\n");
    failures++;
}

/* name = prefix.kind.test.scope.class */
static void counter_name(char *name, const char *prefix, int kind, int test, int scope, int cls)
{
    const char *parts[5] = {prefix, hr_kind_name[kind], hr_test_name[test], hr_scope_name[scope],
                            hr_class_name[cls]};
    ULONG n = 0;
    for (int part = 0; part < 5; part++) {
        for (const char *p = parts[part]; *p; p++) name[n++] = *p;
        if (part < 4) name[n++] = '.';
    }
    name[n] = 0;
}

static void check_entries(ULONG got, ULONG want, const char *pass)
{
    if (got == want) return;
    hr_emit("FAIL ");
    hr_emit(pass);
    hr_emit(" returned ");
    hr_emit_number(got);
    hr_emit(" entries, expected ");
    hr_emit_number(want);
    hr_emit("\n");
    failures++;
}

int main(int argc, char **argv)
{
    if (argc > 1) device = argv[1];
    char full[96];
    ULONG entries, calls, k = 0;
    BOOL complete;

    if (!hr_reset(device)) {
        hr_emit("FAIL cannot reset the counters, is this the hit-rate build of the handler?\n");
        return 10;
    }
    if (!make_directory("hrt", FILES) || !make_directory("hrt2", 5)) {
        hr_emit("FAIL cannot create the directories\n");
        return 10;
    }
    /* The directories were made after the reset, so the table is still empty. */

    path(full, "hrt", NULL);
    BPTR l1 = Lock(full, SHARED_LOCK);
    BPTR l2, l3, l4;
    if (!l1) { hr_emit("FAIL lock\n"); return 10; }

    /* P1, P2: ExAll twice on one lock, the volume unchanged. */
    exall_pass(l1, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES, "P1"); expect(HR_KIND_EXALL, entries, calls, TRUE, 0, 0, 0, 0);
    exall_pass(l1, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES, "P2"); expect(HR_KIND_EXALL, entries, calls, TRUE, 1, 1, 1, 1);
    UnLock(l1);

    /* P3: a second lock on the same directory, the volume unchanged. */
    l2 = Lock(full, SHARED_LOCK);
    exall_pass(l2, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES, "P3"); expect(HR_KIND_EXALL, entries, calls, TRUE, 2, 2, 2, 2);

    /* The volume changes: a file is added and committed. */
    if (!make_file("hrt", "extra") || !flush_volume()) fail("change the volume");

    /* P4: changed. P5: the same lock again, unchanged since P4. */
    exall_pass(l2, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES + 1, "P4"); expect(HR_KIND_EXALL, entries, calls, TRUE, 3, 3, 3, 3);
    exall_pass(l2, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES + 1, "P5"); expect(HR_KIND_EXALL, entries, calls, TRUE, 1, 1, 1, 1);

    /* P6: abandoned after one call with a small buffer, then P7 complete. The
     * abandoned pass must not have touched the record. */
    exall_pass(l2, SMALL_BUFFER_BYTES, NULL, TRUE, &k, &calls, &complete);
    if (k == 0 || k >= FILES + 1 || complete) fail("P6 should return some entries and not finish");
    expect(HR_KIND_EXALL, k, calls, FALSE, 1, 1, 1, 1);
    exall_pass(l2, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES + 1, "P7"); expect(HR_KIND_EXALL, entries, calls, TRUE, 1, 1, 1, 1);

    /* E1, E2: ExNext on the same lock. Its own kind has no record yet (A), the
     * latest pass of either kind does (B). */
    exnext_pass(l2, &entries, &calls);
    check_entries(entries, FILES + 1, "E1"); expect(HR_KIND_EXNEXT, entries, calls, TRUE, 0, 1, 0, 1);
    exnext_pass(l2, &entries, &calls);
    check_entries(entries, FILES + 1, "E2"); expect(HR_KIND_EXNEXT, entries, calls, TRUE, 1, 1, 1, 1);
    /* E3: ExNext through a third lock while the second is held. */
    l3 = Lock(full, SHARED_LOCK);
    exnext_pass(l3, &entries, &calls);
    check_entries(entries, FILES + 1, "E3"); expect(HR_KIND_EXNEXT, entries, calls, TRUE, 2, 2, 2, 2);

    /* A file is opened and closed only to read it. The packet counter rises,
     * the stamps of the trees must not. */
    {
        char name[96];
        char byte;
        path(name, "hrt", "f01");
        BPTR file = Open(name, MODE_OLDFILE);
        if (!file) fail("open for reading");
        else {
            Read(file, &byte, 1);
            Close(file);
        }
    }
    /* P8: stamps and own kind: same lock as P7, unchanged (B). Stamps and either
     * kind: the latest pass was E3 through the third lock (C). The counter
     * rose, so both counter classes are D. */
    exall_pass(l2, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, FILES + 1, "P8"); expect(HR_KIND_EXALL, entries, calls, TRUE, 1, 2, 3, 3);

    /* P9: another directory, never listed. */
    path(full, "hrt2", NULL);
    l4 = Lock(full, SHARED_LOCK);
    exall_pass(l4, BUFFER_BYTES, NULL, FALSE, &entries, &calls, &complete);
    check_entries(entries, 5, "P9"); expect(HR_KIND_EXALL, entries, calls, TRUE, 0, 0, 0, 0);

    /* P10: a pattern, f01 to f09. Unchanged since P8. */
    exall_pass(l2, BUFFER_BYTES, "f0#?", FALSE, &entries, &calls, &complete);
    check_entries(entries, 9, "P10"); expect(HR_KIND_EXALL, entries, calls, TRUE, 1, 1, 1, 1);

    UnLock(l4);
    UnLock(l3);
    UnLock(l2);

    bfs_hitrate_report_t report;
    if (!hr_read(device, &report)) {
        hr_emit("FAIL cannot read the counters\n");
        return 10;
    }

    /* The expected counters, summed from the list above. */
    ULONG starts[HR_KINDS][HR_TESTS][HR_SCOPES][HR_CLASSES] = {{{{0}}}};
    ULONG entry[HR_KINDS][HR_TESTS][HR_SCOPES][HR_CLASSES] = {{{{0}}}};
    ULONG returned[HR_KINDS] = {0, 0};
    ULONG complete_passes[HR_KINDS] = {0, 0};
    for (int i = 0; i < expectation_count; i++) {
        const expectation_t *e = &expectations[i];
        returned[e->kind] += e->entries;
        for (int test = 0; test < HR_TESTS; test++)
            for (int scope = 0; scope < HR_SCOPES; scope++) {
                starts[e->kind][test][scope][e->cls[test][scope]]++;
                entry[e->kind][test][scope][e->cls[test][scope]] += e->entries;
            }
        if (e->complete) complete_passes[e->kind]++;
    }
    for (int kind = 0; kind < HR_KINDS; kind++)
        for (int test = 0; test < HR_TESTS; test++)
            for (int scope = 0; scope < HR_SCOPES; scope++)
                for (int cls = 0; cls < HR_CLASSES; cls++) {
                    char name[64];
                    counter_name(name, "starts", kind, test, scope, cls);
                    check_count(name, starts[kind][test][scope][cls],
                                report.starts[kind][test][scope][cls]);
                    counter_name(name, "entries", kind, test, scope, cls);
                    check_count(name, entry[kind][test][scope][cls],
                                report.entries[kind][test][scope][cls]);
                }
    for (int kind = 0; kind < HR_KINDS; kind++) {
        check_count(kind ? "calls.exnext" : "calls.exall", expected_calls[kind], report.calls[kind]);
        check_count(kind ? "entries_returned.exnext" : "entries_returned.exall", returned[kind],
                    report.entries_returned[kind]);
        check_count(kind ? "passes_complete.exnext" : "passes_complete.exall",
                    complete_passes[kind], report.passes_complete[kind]);
        check_count(kind ? "passes_changed.exnext" : "passes_changed.exall", 0,
                    report.passes_changed[kind]);
        check_count(kind ? "orphan_entries.exnext" : "orphan_entries.exall", 0,
                    report.orphan_entries[kind]);
    }
    check_count("passes_abandoned.exall", 1, report.passes_abandoned[HR_KIND_EXALL]);
    check_count("passes_abandoned.exnext", 0, report.passes_abandoned[HR_KIND_EXNEXT]);
    check_count("pattern_starts", 1, report.pattern_starts);
    check_count("evictions", 0, report.evictions);
    check_count("directories", 2, report.directories);

    say("passes", (ULONG)expectation_count);
    say("failures", failures);
    hr_emit(failures ? "HITRATE-CHECK FAIL\n" : "HITRATE-CHECK PASS\n");
    hr_emit("HITRATE-CHECK-COMPLETE\n");
    return failures ? 5 : 0;
}
