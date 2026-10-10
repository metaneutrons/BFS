/*
 * BFS — Directory records: the all-or-nothing contract of multi-record
 * changes when an allocation or the release of a retired node fails.
 */

#define TEST_IMG "test_dir_records_atomicity.img"
#include "test_harness.h"
#include "dir_records_fixture.h"

typedef enum {
    OP_INSERT,
    OP_REMOVE,
    OP_REKEY,
    OP_COMMENT_INSERT,
    OP_COMMENT_REMOVE,
} record_op_t;

static const char *op_label(record_op_t op)
{
    switch (op) {
    case OP_INSERT: return "insert";
    case OP_REMOVE: return "remove";
    case OP_REKEY: return "rekey";
    case OP_COMMENT_INSERT: return "comment insert";
    default: return "comment remove";
    }
}

/* A directory of fill short entries, plus the long entry and its comment
 * when the operation removes or changes them. */
static bool build_case(fixture_t *f, uint32_t fill, record_op_t op, char *long_name)
{
    if (!fixture_open(f, 1024, 4096)) return false;
    for (uint32_t i = 0; i < fill; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "s%04u", (unsigned)i);
        if (bfs_dir_insert(&f->dt, DIR_INO, name, (uint8_t)len, 500u + i, BFS_INODE_FILE) != BFS_OK)
            return false;
        if (i % 3 == 0 && bfs_dir_comment_insert(&f->dt, 500u + i, "c", 1) != BFS_OK)
            return false;
    }
    memset(long_name, 'L', BFS_NAME_MAX);
    if (op == OP_REMOVE || op == OP_REKEY)
        return bfs_dir_insert(&f->dt, DIR_INO, long_name, BFS_NAME_MAX, 77, BFS_INODE_FILE) == BFS_OK;
    if (op == OP_COMMENT_REMOVE)
        return bfs_dir_comment_insert(&f->dt, 77, long_name, 79) == BFS_OK;
    return true;
}

static bfs_err_t run_op(fixture_t *f, record_op_t op, const char *long_name)
{
    char lower[BFS_NAME_MAX];
    switch (op) {
    case OP_INSERT:
        return bfs_dir_insert(&f->dt, DIR_INO, long_name, BFS_NAME_MAX, 77, BFS_INODE_FILE);
    case OP_REMOVE:
        return bfs_dir_remove(&f->dt, DIR_INO, long_name, BFS_NAME_MAX);
    case OP_REKEY:
        memset(lower, 'l', sizeof(lower));
        return bfs_dir_rekey_case(&f->dt, DIR_INO, long_name, BFS_NAME_MAX, lower, BFS_NAME_MAX);
    case OP_COMMENT_INSERT:
        return bfs_dir_comment_insert(&f->dt, 77, long_name, 79);
    default:
        return bfs_dir_comment_remove(&f->dt, 77);
    }
}

/* Fail the k-th allocation of the operation, once or from then on, for every
 * k it makes when it succeeds. A failed operation leaves every record as it
 * was, or, only when undoing also fails, sets sticky_err. */
static void check_failure_atomicity(record_op_t op)
{
    static const uint32_t fills[] = { 10, 25, 40, 55 };
    bool later_record_failed = false;
    for (size_t fi = 0; fi < sizeof(fills) / sizeof(fills[0]); fi++) {
        fixture_t f;
        char long_name[BFS_NAME_MAX];
        TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
        arm_failure(&f, UINT32_MAX, false);
        TEST_ASSERT_EQ(run_op(&f, op, long_name), BFS_OK);
        uint32_t calls = f.alloc.calls;
        uint32_t height = f.dt.tree.height;
        fixture_close(&f);
        TEST_ASSERT(calls > 0);
        if (calls > height) later_record_failed = true;

        for (uint32_t k = 0; k < calls; k++) {
            for (int after = 0; after < 2; after++) {
                TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
                snapshot_t before = {0}, now = {0};
                TEST_ASSERT(take_snapshot(&f.dt, &before));
                arm_failure(&f, k, after != 0);
                bfs_err_t err = run_op(&f, op, long_name);
                arm_failure(&f, UINT32_MAX, false);
                if (err == BFS_OK) {
                    fprintf(stderr, "    %s fill %u: allocation %u failure ignored\n",
                            op_label(op), (unsigned)fills[fi], (unsigned)k);
                    TEST_ASSERT(err != BFS_OK);
                } else if (f.dt.sticky_err == BFS_OK) {
                    TEST_ASSERT(take_snapshot(&f.dt, &now));
                    if (!snapshots_equal(&before, &now))
                        fprintf(stderr, "    %s fill %u: allocation %u%s changed records\n",
                                op_label(op), (unsigned)fills[fi], (unsigned)k,
                                after ? "+" : "");
                    TEST_ASSERT(snapshots_equal(&before, &now));
                } else {
                    /* Only a failing undo latches, and only if failures persist. */
                    TEST_ASSERT(after != 0);
                }
                free(before.data);
                free(now.data);
                fixture_close(&f);
            }
        }
    }
    /* Some fill made a record after the first allocate, so failures hit the
     * middle of an entry and its undo ran. */
    TEST_ASSERT(later_record_failed);
}

/* A failed release of a retired node leaves the change applied but reports
 * an error. A failed operation must then either leave every record as it was
 * or set sticky_err; it must never report failure with a changed tree and no
 * latched error, because the change could be committed. */
static void check_release_failure(record_op_t op)
{
    static const uint32_t fills[] = { 10, 40 };
    uint32_t injected = 0;
    for (size_t fi = 0; fi < sizeof(fills) / sizeof(fills[0]); fi++) {
        fixture_t f;
        char long_name[BFS_NAME_MAX];
        TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
        arm_failure(&f, UINT32_MAX, false);
        TEST_ASSERT_EQ(run_op(&f, op, long_name), BFS_OK);
        uint32_t releases = f.alloc.dealloc_calls;
        fixture_close(&f);
        for (uint32_t k = 0; k < releases; k++) {
            TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
            snapshot_t before = {0}, now = {0};
            TEST_ASSERT(take_snapshot(&f.dt, &before));
            arm_dealloc_failure(&f, k);
            bfs_err_t err = run_op(&f, op, long_name);
            arm_failure(&f, UINT32_MAX, false);
            TEST_ASSERT(err != BFS_OK);
            injected++;
            TEST_ASSERT(take_snapshot(&f.dt, &now));
            bool unchanged = snapshots_equal(&before, &now);
            if (!unchanged && f.dt.sticky_err == BFS_OK)
                fprintf(stderr, "    %s fill %u: release %u failure left a silent change\n",
                        op_label(op), (unsigned)fills[fi], (unsigned)k);
            TEST_ASSERT(unchanged || f.dt.sticky_err != BFS_OK);
            free(before.data);
            free(now.data);
            fixture_close(&f);
        }
    }
    TEST_ASSERT(injected > 0);
}

static void test_failure_insert(void) { check_failure_atomicity(OP_INSERT); }
static void test_failure_remove(void) { check_failure_atomicity(OP_REMOVE); }
static void test_failure_rekey(void) { check_failure_atomicity(OP_REKEY); }
static void test_failure_comment_insert(void) { check_failure_atomicity(OP_COMMENT_INSERT); }
static void test_failure_comment_remove(void) { check_failure_atomicity(OP_COMMENT_REMOVE); }
static void test_release_failure_insert(void) { check_release_failure(OP_INSERT); }
static void test_release_failure_remove(void) { check_release_failure(OP_REMOVE); }
static void test_release_failure_rekey(void) { check_release_failure(OP_REKEY); }
static void test_release_failure_comments(void)
{
    check_release_failure(OP_COMMENT_INSERT);
    check_release_failure(OP_COMMENT_REMOVE);
}

TEST_SUITE_BEGIN("Directory record atomicity")
    TEST_RUN(test_failure_insert);
    TEST_RUN(test_failure_remove);
    TEST_RUN(test_failure_rekey);
    TEST_RUN(test_failure_comment_insert);
    TEST_RUN(test_failure_comment_remove);
    TEST_RUN(test_release_failure_insert);
    TEST_RUN(test_release_failure_remove);
    TEST_RUN(test_release_failure_rekey);
    TEST_RUN(test_release_failure_comments);
TEST_SUITE_END()
