/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode write-back across reload, crash, fsck and failed writes: what
 * a pending copy leaves behind when the transaction does not commit normally.
 */

#define WB_IMAGE "test_inode_write_back_recovery.img"
#include "inode_write_back_fixture.h"
#include "bfs_cache.h"
#include "bfs_fsck.h"

/* Node reads fail while fail_reads is set. */
static bfs_bio_ops_t faulty_ops;
static bfs_err_t (*real_read)(bfs_bio_t *, bfs_blk_t, void *);
static bool fail_reads;

static bfs_err_t faulty_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    return fail_reads ? BFS_ERR_IO : real_read(bio, blk, buf);
}

static void inject_read_faults(bfs_bio_t *bio)
{
    faulty_ops = *bio->ops;
    real_read = bio->ops->read_block;
    faulty_ops.read_block = faulty_read;
    bio->ops = &faulty_ops;
    fail_reads = false;
}

static void test_reload_discards_pending_copies(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "reload");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 2);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    write_chunks(&file, 2, 3);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    TEST_ASSERT_EQ(bfs_fs_reload_committed_unlocked(&f.fs), BFS_OK);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    TEST_ASSERT_EQ(read_size(&f.fs, ino), 2 * WB_CHUNK);
    check_contents(&f.fs, ino, 2);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&f.fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    fixture_close(&f);
}

static void test_crash_keeps_committed_state(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "crash");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    write_chunks(&file, 3, 4);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    /* Power loss: nothing after the last commit is written. */
    bfs_fs_abandon(&f.fs);
    bfs_bio_close(f.bio);
    f.bio = bio_emu_open(WB_IMAGE, WB_BLOCK_SIZE);
    TEST_ASSERT(f.bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount(&f.fs, f.bio), BFS_OK);
    check_contents(&f.fs, ino, 3);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&f.fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    fixture_close(&f);
}

static void test_fsck_reads_pending_copies(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "fsck");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 6);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    /* Blocks mapped after the first publication are referenced only by the
     * pending copy; the check must see them. Before a commit, only the
     * deferred frees of the transaction count as unreferenced. */
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&f.fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    TEST_ASSERT_EQ(report.leaked_blocks, f.fs.pending_count);
    /* The check reads the pending copy; it does not change the tree. */
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);
    TEST_ASSERT(tree_size(&f.fs, ino) < 6 * WB_CHUNK);
    fixture_close(&f);
}

static void test_failed_flush_blocks_the_commit(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "fail");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    write_chunks(&file, 1, 2);

    /* A pending copy that the tree rejects stands for any failed write. */
    bfs_inode_pending_slot_t *dirty = NULL;
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++)
        if (f.fs.inode_pending.slot[i].dirty) dirty = &f.fs.inode_pending.slot[i];
    TEST_ASSERT(dirty != NULL);
    dirty->inode.inode_nr = bfs_be32(ino + 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(f.fs.recovery_error, BFS_ERR_INVAL);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_ERR_INVAL);

    /* Recovery returns to the last commit. */
    TEST_ASSERT_EQ(bfs_fs_reload_committed_unlocked(&f.fs), BFS_OK);
    check_contents(&f.fs, ino, 1);
    fixture_close(&f);
}

static void test_failed_reload_still_discards_the_table(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "noreload");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    /* The reload stops before it reopens the trees. */
    f.fs.recovery_generation = UINT64_MAX;
    TEST_ASSERT_EQ(bfs_fs_reload_committed_unlocked(&f.fs), BFS_ERR_NOSPC);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    bfs_fs_abandon(&f.fs);
    fixture_close(&f);
}

/* A write through another path that fails without changing the tree keeps the
 * pending copy, which is still the newest state; the commit then stores it. */
static void test_failed_other_write_keeps_pending_copy(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    inject_read_faults(f.bio);
    uint32_t ino = create_file(&f, "keep");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    write_chunks(&file, 3, 1);
    TEST_ASSERT_EQ(bfs_file_truncate(&file, 0), BFS_OK);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    /* The handler's read, stamp and write; the write's descent cannot read. */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&f.fs.inode_tree, ino, &inode), BFS_OK);
    inode.protection = bfs_be32(0x10u);
    fail_reads = true;
    TEST_ASSERT_EQ(bfs_inode_write(&f.fs.inode_tree, ino, &inode), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_inode_delete(&f.fs.inode_tree, ino), BFS_ERR_IO);
    fail_reads = false;
    TEST_ASSERT_EQ(f.fs.recovery_error, BFS_OK);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);
    check_used(&f.fs);
    TEST_ASSERT_EQ(read_size(&f.fs, ino), 0);

    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 0);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&f.fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    fixture_close(&f);
}

/* Writing a pending copy needs no deferred-free headroom: its path was
 * written in the same transaction, so a commit forced by a short queue still
 * succeeds. */
static void test_commit_with_a_full_free_queue(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "full");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 2);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    write_chunks(&file, 2, 3);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);
    TEST_ASSERT(f.fs.pending_count > 0);

    uint32_t saved = f.fs.pending_frees_cap;
    f.fs.pending_frees_cap = f.fs.pending_count;
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    f.fs.pending_frees_cap = saved;
    TEST_ASSERT_EQ(f.fs.recovery_error, BFS_OK);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 5 * WB_CHUNK);
    check_contents(&f.fs, ino, 5);
    fixture_close(&f);
}

/* Create count files named prefix and two letters in the root; picked
 * receives the inode number of the one at index pick. */
static bool create_named_files(bfs_fs_t *fs, char prefix, uint32_t count, uint32_t pick,
                               uint32_t *picked)
{
    for (uint32_t i = 0; i < count; i++) {
        char name[4] = { prefix, (char)('a' + i / 26 % 26), (char)('a' + i % 26), 0 };
        uint32_t ino = 0;
        if (bfs_fs_create_file(fs, BFS_ROOT_INO, name, 3, &ino) != BFS_OK) return false;
        if (i == pick && picked) *picked = ino;
    }
    return true;
}

/* On the Amiga the node cache writes owned leaves back before the commit, so
 * the in-place update is not available when the copy is written. The commit
 * must still succeed with a full free queue, and the result must hold. */
static void test_written_back_leaf_on_the_node_cache(void)
{
    enum { OTHERS = 200 };
    (void)unlink(WB_IMAGE);
    bfs_bio_t *device = bio_emu_create(WB_IMAGE, WB_BLOCK_SIZE, WB_BLOCK_COUNT);
    TEST_ASSERT(device != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(device, "Cached", 0), BFS_OK);
    static bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, device, 16), BFS_OK);
    bfs_cache_set_deferred_node_limit(&cache, 8);
    static bfs_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);

    uint32_t ino = 0;
    TEST_ASSERT(create_named_files(&fs, 'p', OTHERS, OTHERS / 3, &ino));
    TEST_ASSERT(fs.inode_tree.height > 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* First publication, then enough other changes to write dirty nodes back,
     * then a pending copy. */
    write_chunks(&file, 3, 1);
    TEST_ASSERT(create_named_files(&fs, 'q', 400, UINT32_MAX, NULL));
    TEST_ASSERT_EQ(bfs_file_truncate(&file, WB_CHUNK), BFS_OK);
    TEST_ASSERT_EQ(pending_count(&fs, true), 1);
    TEST_ASSERT_EQ(read_size(&fs, ino), WB_CHUNK);

    TEST_ASSERT(fs.pending_count > 0);
    uint32_t saved = fs.pending_frees_cap;
    fs.pending_frees_cap = fs.pending_count;
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    fs.pending_frees_cap = saved;
    TEST_ASSERT_EQ(tree_size(&fs, ino), WB_CHUNK);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);
    check_contents(&fs, ino, 1);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_cache_destroy(&cache);
    bfs_bio_close(device);
    (void)unlink(WB_IMAGE);
}

TEST_SUITE_BEGIN("Inode write-back recovery")
    TEST_RUN(test_reload_discards_pending_copies);
    TEST_RUN(test_crash_keeps_committed_state);
    TEST_RUN(test_fsck_reads_pending_copies);
    TEST_RUN(test_failed_flush_blocks_the_commit);
    TEST_RUN(test_failed_reload_still_discards_the_table);
    TEST_RUN(test_failed_other_write_keeps_pending_copy);
    TEST_RUN(test_commit_with_a_full_free_queue);
    TEST_RUN(test_written_back_leaf_on_the_node_cache);
TEST_SUITE_END()
