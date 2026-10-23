/* SPDX-License-Identifier: MPL-2.0 */
/* hitrate [DEVICE] [RESET]
 *
 * Prints the hit-rate counters of a debug build of the BFS handler as tab
 * separated key and value lines, or clears them. DEVICE defaults to BFS:. */
#include "hitrate-common.h"

static BOOL same_text(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return FALSE;
    }
    return *a == *b;
}

/* One line: the parts of the key joined by dots, a tab and the value. */
static void line(const char *a, const char *b, const char *c, const char *d, const char *e,
                 unsigned long value)
{
    hr_emit(a);
    if (b) { hr_emit("."); hr_emit(b); }
    if (c) { hr_emit("."); hr_emit(c); }
    if (d) { hr_emit("."); hr_emit(d); }
    if (e) { hr_emit("."); hr_emit(e); }
    hr_emit("\t");
    hr_emit_number(value);
    hr_emit("\n");
}

int main(int argc, char **argv)
{
    const char *device = "BFS:";
    BOOL reset = FALSE;
    for (int i = 1; i < argc; i++) {
        if (same_text(argv[i], "RESET")) reset = TRUE;
        else device = argv[i];
    }
    if (reset) {
        if (!hr_reset(device)) {
            hr_emit("hitrate: reset failed, is this the hit-rate build of the handler?\n");
            return 10;
        }
        return 0;
    }
    bfs_hitrate_report_t report;
    if (!hr_read(device, &report)) {
        hr_emit("hitrate: cannot read the counters, is this the hit-rate build of the handler?\n");
        return 10;
    }
    line("version", NULL, NULL, NULL, NULL, report.version);
    for (int kind = 0; kind < HR_KINDS; kind++)
        for (int test = 0; test < HR_TESTS; test++)
            for (int scope = 0; scope < HR_SCOPES; scope++)
                for (int cls = 0; cls < HR_CLASSES; cls++) {
                    line("starts", hr_kind_name[kind], hr_test_name[test], hr_scope_name[scope],
                         hr_class_name[cls], report.starts[kind][test][scope][cls]);
                    line("entries", hr_kind_name[kind], hr_test_name[test], hr_scope_name[scope],
                         hr_class_name[cls], report.entries[kind][test][scope][cls]);
                }
    for (int kind = 0; kind < HR_KINDS; kind++) {
        line("calls", hr_kind_name[kind], NULL, NULL, NULL, report.calls[kind]);
        line("entries_returned", hr_kind_name[kind], NULL, NULL, NULL, report.entries_returned[kind]);
        line("passes_complete", hr_kind_name[kind], NULL, NULL, NULL, report.passes_complete[kind]);
        line("passes_abandoned", hr_kind_name[kind], NULL, NULL, NULL, report.passes_abandoned[kind]);
        line("passes_changed", hr_kind_name[kind], NULL, NULL, NULL, report.passes_changed[kind]);
        line("orphan_entries", hr_kind_name[kind], NULL, NULL, NULL, report.orphan_entries[kind]);
    }
    line("pattern_starts", NULL, NULL, NULL, NULL, report.pattern_starts);
    line("evictions", NULL, NULL, NULL, NULL, report.evictions);
    line("directories", NULL, NULL, NULL, NULL, report.directories);
    line("volume_changes", NULL, NULL, NULL, NULL, report.volume_changes);
    return 0;
}
