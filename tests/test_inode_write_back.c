/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode write-back within a transaction: a published inode stays
 * pending until the commit after its first publication, every reader sees the
 * pending copy, other writers supersede it, and snapshots include it. Reload,
 * crash, fsck and failed writes are tested in
 * test_inode_write_back_recovery.c.
 */

#define WB_IMAGE "test_inode_write_back.img"
#include "inode_write_back_fixture.h"
#include "bfs_fsck.h"
#include "bfs_snapshot.h"

static void test_publication_stays_pending_until_commit(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "append");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);

    /* The first publication in the transaction writes the tree. */
    write_chunks(&file, 0, 1);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), WB_CHUNK);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 1);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 0);

    /* Later ones stay pending; readers see them. */
    write_chunks(&file, 1, 3);
    check_used(&f.fs);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), WB_CHUNK);
    TEST_ASSERT_EQ(read_size(&f.fs, ino), 4 * WB_CHUNK);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);
    check_contents(&f.fs, ino, 4);

    /* The commit writes the copy and empties the table. */
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 4 * WB_CHUNK);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    check_used(&f.fs);

    /* The next transaction starts with a tree write again. */
    write_chunks(&file, 4, 1);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 5 * WB_CHUNK);
    write_chunks(&file, 5, 1);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 5 * WB_CHUNK);
    TEST_ASSERT_EQ(read_size(&f.fs, ino), 6 * WB_CHUNK);

    /* Unmount commits as well. */
    TEST_ASSERT_EQ(bfs_fs_unmount(&f.fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&f.fs, f.bio), BFS_OK);
    check_contents(&f.fs, ino, 6);
    fixture_close(&f);
}

static void test_second_handle_sees_pending_state(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "shared");
    TEST_ASSERT(ino != 0);
    bfs_file_t writer, other;
    TEST_ASSERT_EQ(bfs_file_open(&writer, &f.fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&other, &f.fs, ino), BFS_OK);
    write_chunks(&writer, 0, 3);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    /* The other handle refreshes from the pending copy and appends after it. */
    TEST_ASSERT_EQ(bfs_file_seek(&other, 0, BFS_SEEK_END), 3 * WB_CHUNK);
    write_chunks(&other, 3, 1);
    TEST_ASSERT_EQ(read_size(&f.fs, ino), 4 * WB_CHUNK);
    check_contents(&f.fs, ino, 4);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    check_contents(&f.fs, ino, 4);
    fixture_close(&f);
}

static void test_other_writer_supersedes_pending_copy(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "protect");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 2);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    /* An adapter changes the protection bits through read and write, as the
     * AmigaDOS handler does. Its write replaces the pending copy. */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&f.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(inode_size(&inode), 2 * WB_CHUNK);
    inode.protection = bfs_be32(0x5au);
    TEST_ASSERT_EQ(bfs_inode_write(&f.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    check_used(&f.fs);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 2 * WB_CHUNK);

    write_chunks(&file, 2, 2);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&f.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.protection), 0x5au);
    TEST_ASSERT_EQ(inode_size(&inode), 4 * WB_CHUNK);
    check_contents(&f.fs, ino, 4);
    fixture_close(&f);
}

static void test_full_table_writes_through(void)
{
    enum { FILES = BFS_INODE_PENDING_SLOTS + 3, ROUNDS = 3 };
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t inos[FILES];
    bfs_file_t files[FILES];
    for (uint32_t i = 0; i < FILES; i++) {
        char name[8] = { 'f', (char)('a' + i), 0 };
        inos[i] = create_file(&f, name);
        TEST_ASSERT(inos[i] != 0);
        TEST_ASSERT_EQ(bfs_file_open(&files[i], &f.fs, inos[i]), BFS_OK);
    }
    for (uint32_t round = 0; round < ROUNDS; round++) {
        for (uint32_t i = 0; i < FILES; i++) {
            write_chunks(&files[i], round, 1);
            TEST_ASSERT(pending_count(&f.fs, false) <= BFS_INODE_PENDING_SLOTS);
            check_used(&f.fs);
        }
        for (uint32_t i = 0; i < FILES; i++)
            TEST_ASSERT_EQ(read_size(&f.fs, inos[i]), (uint64_t)(round + 1) * WB_CHUNK);
    }
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    check_used(&f.fs);
    for (uint32_t i = 0; i < FILES; i++) {
        TEST_ASSERT_EQ(tree_size(&f.fs, inos[i]), (uint64_t)ROUNDS * WB_CHUNK);
        check_contents(&f.fs, inos[i], ROUNDS);
    }
    fixture_close(&f);
}

static void test_snapshot_includes_pending_copies(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "snap");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 4);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);
    TEST_ASSERT_EQ(bfs_snapshot_create(&f.fs, "before"), BFS_OK);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 4 * WB_CHUNK);

    /* Writing on after the snapshot keeps both states intact. */
    write_chunks(&file, 4, 2);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    check_contents(&f.fs, ino, 6);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&f.fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    fixture_close(&f);
}

static void test_copy_from_an_older_transaction_is_not_reused(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "older");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 1);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 1);

    /* A slot whose leaf another transaction wrote must not take copies. */
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++)
        if (f.fs.inode_pending.slot[i].ino == ino) f.fs.inode_pending.slot[i].txn_id--;
    write_chunks(&file, 1, 1);
    TEST_ASSERT_EQ(tree_size(&f.fs, ino), 2 * WB_CHUNK);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 0);
    fixture_close(&f);
}

/* ExAll reads inodes in ascending batches; those return pending copies too. */
static void test_sorted_reads_return_pending_copies(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t a = create_file(&f, "sa");
    uint32_t b = create_file(&f, "sb");
    TEST_ASSERT(a != 0 && b != 0 && a < b);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, b), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);
    uint32_t inos[2] = { a, b };
    bfs_inode_t out[2];
    bfs_err_t results[2];
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&f.fs.inode_tree, inos, 2, out, results), BFS_OK);
    TEST_ASSERT_EQ(results[0], BFS_OK);
    TEST_ASSERT_EQ(results[1], BFS_OK);
    TEST_ASSERT_EQ(inode_size(&out[0]), 0);
    TEST_ASSERT_EQ(inode_size(&out[1]), 3 * WB_CHUNK);
    fixture_close(&f);
}

static void test_unlinked_handle_leaves_the_table(void)
{
    wb_fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    uint32_t ino = create_file(&f, "gone");
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &f.fs, ino), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(pending_count(&f.fs, true), 1);

    uint32_t orphan = 0;
    TEST_ASSERT_EQ(bfs_fs_unlink_open_file(&f.fs, BFS_ROOT_INO, "gone", 4, &orphan), BFS_OK);
    TEST_ASSERT_EQ(orphan, ino);
    TEST_ASSERT_EQ(bfs_file_mark_unlinked(&file), BFS_OK);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);

    /* The open handle keeps writing its retained inode, never through the table. */
    write_chunks(&file, 3, 1);
    TEST_ASSERT_EQ(pending_count(&f.fs, false), 0);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read_unlinked(&f.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(inode_size(&inode), 4 * WB_CHUNK);
    TEST_ASSERT_EQ(bfs_fs_sync(&f.fs), BFS_OK);
    fixture_close(&f);
}

TEST_SUITE_BEGIN("Inode write-back")
    TEST_RUN(test_publication_stays_pending_until_commit);
    TEST_RUN(test_second_handle_sees_pending_state);
    TEST_RUN(test_other_writer_supersedes_pending_copy);
    TEST_RUN(test_full_table_writes_through);
    TEST_RUN(test_snapshot_includes_pending_copies);
    TEST_RUN(test_copy_from_an_older_transaction_is_not_reused);
    TEST_RUN(test_sorted_reads_return_pending_copies);
    TEST_RUN(test_unlinked_handle_leaves_the_table);
TEST_SUITE_END()
