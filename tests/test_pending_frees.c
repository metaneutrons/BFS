/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Deferred-free queue (pending_frees) regression tests for finding #41.
 *
 * #41: btree_free_node() used to silently DROP an old COW'd node block when the
 * pending-free queue was full, leaking it until offline fsck. The "just free it
 * immediately" shortcut is unsafe (a block freed mid-COW could be reused before
 * commit, corrupting the last committed state), and the queue cannot be drained
 * mid-mutation. The fix:
 *   - reserves queue headroom at safe fs-entry points (bfs_fs_ensure_free_headroom)
 *     so the in-COW defer() provably never overflows for bounded ops;
 *   - latches any residual overflow and surfaces it as a loud BFS_ERR_NOSPC
 *     instead of a silent drop;
 *   - reorders compaction to swap-root -> commit -> free-old-nodes, so its
 *     unbounded whole-old-tree free happens post-commit and can drain.
 *
 * These tests lower fs->pending_frees_cap so the reserve/drain paths are
 * exercised with modest churn, and assert: (1) no overflow under churn, (2) the
 * queue actually filled and drained (commits fired — the test isn't vacuous),
 * (3) no space leak (free count round-trips), (4) compaction's mass-free loses
 * nothing with the tree left intact, (5) a genuine overflow is LOUD, not silent.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_IMG  "test_pending_frees.img"
#define BLK_SIZE  4096
#define BLK_COUNT 8192     /* 32 MB */
#define SMALL_CAP 512      /* > BFS_FS_OP_FREE_RESERVE (325) so ops can still reserve */

static bfs_fs_t   g_fs;
static bfs_bio_t *g_bio;

static void setup(void)
{
    g_bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(g_bio, "Pending", 0);
    bfs_fs_mount(&g_fs, g_bio);
}

static void teardown(void)
{
    bfs_fs_unmount(&g_fs);
    bio_emu_create(TEST_IMG, BLK_SIZE, 0); /* delete image */
}

/* Create n files and COMMIT them, so their tree nodes belong to an OLDER
 * transaction. This matters: nodes created and freed within the same uncommitted
 * transaction are deallocated immediately and never touch the queue — only
 * old-txn nodes are deferred. So the deferred-free queue only fills when
 * *committed* data is modified, which is what the delete storm below drives. */
static void create_and_commit(int n)
{
    for (int i = 0; i < n; i++) {
        char name[24]; sprintf(name, "f%05d", i);
        TEST_ASSERT_EQ(bfs_fs_create_file(&g_fs, BFS_ROOT_INO, name, (uint8_t)strlen(name), NULL), BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);
}

/* Delete all n committed files under a small queue cap. Each delete COWs OLD-txn
 * dir/inode nodes, so the queue fills and the entry-point reserve must
 * commit-drain mid-storm. Asserts every delete succeeds (no overflow) and that
 * the reserve actually fired many commits (proof the queue truly filled — the
 * test isn't vacuous). Restores the full cap before returning. */
static void delete_storm(int n)
{
    g_fs.pending_frees_cap = SMALL_CAP;
    uint64_t txn_before = g_fs.live_txn_id;
    for (int i = 0; i < n; i++) {
        char name[24]; sprintf(name, "f%05d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&g_fs, BFS_ROOT_INO, name, (uint8_t)strlen(name)), BFS_OK);
    }
    /* At least one commit must have fired DURING the deletes (before the sync
     * below) — only the reserve commits there, so this proves the queue actually
     * reached the drain threshold rather than the test passing vacuously. */
    TEST_ASSERT(g_fs.live_txn_id > txn_before);
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);
    g_fs.pending_frees_cap = BFS_PENDING_FREES_MAX;
}

/* (1)+(2)+(3): a committed tree mass-deleted under a small cap fills the queue
 * and forces the reserve to drain repeatedly; no op overflows, and free space
 * round-trips across settled cycles — a dropped (#41) block on each storm would
 * make it strictly decrease cycle over cycle. */
static void test_small_cap_delete_storm_no_leak(void)
{
    setup();
    const int N = 2000;
    uint32_t prev_free = 0;
    for (int cycle = 0; cycle < 4; cycle++) {
        create_and_commit(N);
        delete_storm(N);
        uint32_t f = g_fs.freespace.total_free;   /* tree is empty again */
        if (cycle >= 2)                           /* let the free tree settle */
            TEST_ASSERT_EQ(f, prev_free);
        prev_free = f;
    }
    teardown();
}

/* (1)+(3) with snapshots enabled: the btree defer path is NOT has_snapshots
 * gated, so the fill/fix must hold on snapshot volumes too. */
static void test_small_cap_delete_storm_snapshots(void)
{
    setup();
    TEST_ASSERT_EQ(bfs_snapshot_create(&g_fs, "snap0"), BFS_OK);
    TEST_ASSERT(g_fs.has_snapshots);
    const int N = 1500;
    uint32_t prev_free = 0;
    for (int cycle = 0; cycle < 4; cycle++) {
        create_and_commit(N);
        delete_storm(N);
        uint32_t f = g_fs.freespace.total_free;
        if (cycle >= 2)
            TEST_ASSERT_EQ(f, prev_free);
        prev_free = f;
    }
    teardown();
}

/* Build a fragmented dir tree (N entries, every other deleted), compact it with
 * the given queue cap, sync, verify every survivor still resolves, and report
 * the resulting free-block count. With no leak the count is independent of the
 * cap — that is the differential oracle the test below relies on. */
static void compact_fragmented(uint32_t cap, uint32_t *free_after_out)
{
    setup();
    const int N = 800;
    for (int i = 0; i < N; i++) {
        char name[24]; sprintf(name, "c%05d", i);
        TEST_ASSERT_EQ(bfs_fs_create_file(&g_fs, BFS_ROOT_INO, name, (uint8_t)strlen(name), NULL), BFS_OK);
    }
    /* Delete every other entry to drop utilisation below the 90% threshold. */
    for (int i = 0; i < N; i += 2) {
        char name[24]; sprintf(name, "c%05d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&g_fs, BFS_ROOT_INO, name, (uint8_t)strlen(name)), BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    g_fs.pending_frees_cap = cap;
    TEST_ASSERT_EQ(bfs_fs_compact_tree(&g_fs, &g_fs.dir_tree.tree), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    /* Every surviving entry must still resolve through the rebuilt tree. */
    for (int i = 1; i < N; i += 2) {
        char name[24]; sprintf(name, "c%05d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&g_fs.dir_tree, BFS_ROOT_INO, name,
                                      (uint8_t)strlen(name), &ino, &type), BFS_OK);
    }
    *free_after_out = g_fs.freespace.total_free;
    teardown();
}

/* (4): compaction's post-commit old-node mass-free must reclaim EVERYTHING even
 * when the old tree has more nodes than the queue holds (forcing the mid-walk
 * drain). Differential oracle: a tiny cap must reclaim exactly as much as a full
 * cap that never drains mid-walk; a #41-style drop would leave the tiny-cap run
 * with strictly less free space. (Isolates the leak from compaction's own
 * constant COW overhead, which is identical in both runs.) */
static void test_compaction_mass_free_no_leak(void)
{
    uint32_t free_full = 0, free_small = 0;
    compact_fragmented(BFS_PENDING_FREES_MAX, &free_full);  /* never drains mid-walk */
    compact_fragmented(128, &free_small);                   /* must drain mid-walk repeatedly */
    TEST_ASSERT_EQ(free_small, free_full);
}

/* (5) negative control / oracle: with the reserve deliberately bypassed (direct
 * btree delete, not a namespace wrapper) and the queue full, a COW delete that
 * frees OLD nodes must report BFS_ERR_NOSPC via the latch — NOT return OK with a
 * silently dropped block. Proves the overflow is now loud, and that the harness
 * can actually drive the queue to full (so the other tests aren't vacuous). */
static void test_overflow_is_loud_not_silent(void)
{
    setup();

    /* Build an inode tree deep enough (>= 2 levels) that a delete COWs interior
     * nodes, then COMMIT so those nodes belong to an older transaction — only
     * old-txn nodes are deferred; current-txn nodes are freed immediately. */
    const int N = 2000;
    for (int i = 1; i <= N; i++) {
        bfs_inode_t in; memset(&in, 0, sizeof in);
        in.inode_nr   = bfs_be32((uint32_t)i);
        in.type       = bfs_be32(BFS_INODE_FILE);
        in.link_count = bfs_be32(1);
        TEST_ASSERT_EQ(bfs_inode_write(&g_fs.inode_tree, (uint32_t)i, &in), BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);   /* nodes are now old-txn */

    /* Simulate a full deferred-free queue (exactly the #41 condition), bypassing
     * the entry-point reserve. */
    g_fs.pending_frees_cap = 64;
    g_fs.pending_count     = 64;

    /* The delete COWs old interior nodes; every old-node defer hits the full
     * queue. The latch must surface NOSPC rather than dropping/leaking silently. */
    bfs_err_t r = bfs_inode_delete(&g_fs.inode_tree, 1000);
    TEST_ASSERT_EQ(r, BFS_ERR_NOSPC);

    /* Discard the intentionally-degraded queue state before teardown's sync so
     * it does not try to drain the dummy entries. */
    g_fs.pending_count = 0;
    teardown();
}

TEST_SUITE_BEGIN("Pending-Free Queue (#41)")
    TEST_RUN(test_small_cap_delete_storm_no_leak);
    TEST_RUN(test_small_cap_delete_storm_snapshots);
    TEST_RUN(test_compaction_mass_free_no_leak);
    TEST_RUN(test_overflow_is_loud_not_silent);
TEST_SUITE_END()
