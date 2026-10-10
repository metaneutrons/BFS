/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode write-back within a transaction: a published inode stays
 * pending until the commit after its first publication, every reader sees the
 * pending copy, other writers supersede it, and reload, crash, fsck and
 * snapshots see a consistent tree.
 */

#include "test_harness.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_internal.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <unistd.h>

#define WB_IMAGE "test_inode_write_back.img"
#define WB_BLOCK_SIZE 4096
#define WB_BLOCK_COUNT 4096
#define WB_CHUNK 4096

typedef struct {
    bfs_fs_t fs;
    bfs_bio_t *bio;
} wb_fixture_t;

static uint8_t chunk[WB_CHUNK];
static uint8_t readback[8 * WB_CHUNK];

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

static bool fixture_open(wb_fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    (void)unlink(WB_IMAGE);
    f->bio = bio_emu_create(WB_IMAGE, WB_BLOCK_SIZE, WB_BLOCK_COUNT);
    if (!f->bio) return false;
    if (bfs_fs_format(f->bio, "WriteBack", 0) != BFS_OK) return false;
    return bfs_fs_mount(&f->fs, f->bio) == BFS_OK;
}

static void fixture_close(wb_fixture_t *f)
{
    if (f->fs.mounted) (void)bfs_fs_unmount(&f->fs);
    if (f->bio) bfs_bio_close(f->bio);
    f->bio = NULL;
    (void)unlink(WB_IMAGE);
}

static void fill_chunk(uint32_t index)
{
    for (uint32_t i = 0; i < WB_CHUNK; i++) chunk[i] = (uint8_t)(index * 31u + i);
}

static uint64_t inode_size(const bfs_inode_t *inode)
{
    return ((uint64_t)bfs_be32(inode->size_hi) << 32) | bfs_be32(inode->size_lo);
}

/* The size the API reports, which includes a pending copy. */
static uint64_t read_size(bfs_fs_t *fs, uint32_t ino)
{
    bfs_inode_t inode;
    if (bfs_inode_read(&fs->inode_tree, ino, &inode) != BFS_OK) return UINT64_MAX;
    return inode_size(&inode);
}

/* The size stored in the tree itself, without the pending copy. */
static uint64_t tree_size(bfs_fs_t *fs, uint32_t ino)
{
    bfs_inode_t inode;
    uint32_t key = bfs_be32(ino);
    if (bfs_btree_search(&fs->inode_tree, &key, &inode) != BFS_OK) return UINT64_MAX;
    return inode_size(&inode);
}

static uint32_t pending_count(const bfs_fs_t *fs, bool dirty_only)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++) {
        const bfs_inode_pending_slot_t *slot = &fs->inode_pending.slot[i];
        if (slot->ino != 0 && (!dirty_only || slot->dirty)) count++;
    }
    return count;
}

/* The table's count of occupied slots matches its slots. */
static void check_used(const bfs_fs_t *fs)
{
    TEST_ASSERT_EQ(fs->inode_pending.used, pending_count(fs, false));
}

/* The new file's inode number, 0 on failure. */
static uint32_t create_file(wb_fixture_t *f, const char *name)
{
    uint32_t ino = 0;
    if (bfs_fs_create_file(&f->fs, BFS_ROOT_INO, name, (uint8_t)strlen(name),
                           &ino) != BFS_OK)
        return 0;
    return ino;
}

static void write_chunks(bfs_file_t *file, uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        fill_chunk(first + i);
        TEST_ASSERT_EQ(bfs_file_write(file, chunk, WB_CHUNK), WB_CHUNK);
    }
}

static void check_contents(bfs_fs_t *fs, uint32_t ino, uint32_t chunks)
{
    bfs_file_t reader;
    TEST_ASSERT_EQ(bfs_file_open(&reader, fs, ino), BFS_OK);
    TEST_ASSERT_EQ(reader.size, (uint64_t)chunks * WB_CHUNK);
    TEST_ASSERT_EQ(bfs_file_read(&reader, readback, chunks * WB_CHUNK),
                   (int32_t)(chunks * WB_CHUNK));
    for (uint32_t i = 0; i < chunks; i++) {
        fill_chunk(i);
        TEST_ASSERT_MEM_EQ(readback + i * WB_CHUNK, chunk, WB_CHUNK);
    }
}

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
    for (uint32_t i = 0; i < OTHERS; i++) {
        char name[8] = { 'p', (char)('a' + i / 26 % 26), (char)('a' + i % 26), 0 };
        uint32_t other = 0;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, 3, &other), BFS_OK);
        if (i == OTHERS / 3) ino = other;
    }
    TEST_ASSERT(fs.inode_tree.height > 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    write_chunks(&file, 0, 3);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* First publication, then enough other changes to write dirty nodes back,
     * then a pending copy. */
    write_chunks(&file, 3, 1);
    for (uint32_t i = 0; i < 400; i++) {
        char name[8] = { 'q', (char)('a' + i / 26), (char)('a' + i % 26), 0 };
        uint32_t other = 0;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, 3, &other), BFS_OK);
    }
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
    TEST_RUN(test_reload_discards_pending_copies);
    TEST_RUN(test_crash_keeps_committed_state);
    TEST_RUN(test_fsck_reads_pending_copies);
    TEST_RUN(test_full_table_writes_through);
    TEST_RUN(test_snapshot_includes_pending_copies);
    TEST_RUN(test_failed_flush_blocks_the_commit);
    TEST_RUN(test_copy_from_an_older_transaction_is_not_reused);
    TEST_RUN(test_failed_reload_still_discards_the_table);
    TEST_RUN(test_failed_other_write_keeps_pending_copy);
    TEST_RUN(test_commit_with_a_full_free_queue);
    TEST_RUN(test_sorted_reads_return_pending_copies);
    TEST_RUN(test_written_back_leaf_on_the_node_cache);
    TEST_RUN(test_unlinked_handle_leaves_the_table);
TEST_SUITE_END()
