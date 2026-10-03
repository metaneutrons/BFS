/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — deferred writes of transaction-owned B-tree nodes
 *
 * The node cache may keep nodes of the live transaction dirty until the
 * commit flushes them. These tests check the cache contract directly and the
 * filesystem guarantees on top of it: a crash at any device write, or a write
 * failure at any device write, leaves either the last committed state or the
 * complete new one, and a freed node's image never reaches its block.
 */

#include "test_harness.h"
#include "bfs_cache.h"
#include "bfs_dir.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <unistd.h>

#define TEST_IMG    "test_deferred_nodes.img"
#define BLK_SIZE    4096
#define BLK_COUNT   512
#define CACHE_SLOTS 30

/* ── Cache contract ────────────────────────────────────────── */

#define MEM_BLOCKS 64

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[MEM_BLOCKS][BLK_SIZE];
    uint32_t writes[MEM_BLOCKS];
    uint32_t total_writes;
    bool fail_writes;
} memory_bio_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    memory_bio_t *m = (memory_bio_t *)bio;
    if (blk >= MEM_BLOCKS) return BFS_ERR_INVAL;
    memcpy(buf, m->blocks[blk], BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    memory_bio_t *m = (memory_bio_t *)bio;
    if (blk >= MEM_BLOCKS) return BFS_ERR_INVAL;
    if (m->fail_writes) return BFS_ERR_IO;
    memcpy(m->blocks[blk], buf, BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    m->writes[blk]++;
    m->total_writes++;
    return BFS_OK;
}

static bfs_err_t memory_sync(bfs_bio_t *bio) { (void)bio; return BFS_OK; }
static void memory_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read, .write_block = memory_write,
    .sync = memory_sync, .close = memory_close,
};

static memory_bio_t memory;
static uint32_t finalize_calls;

/* Stands in for the B-tree CRC: the last byte records finalization. */
static bfs_err_t mark_final(const void *layout, uint32_t block_size, uint8_t *buf)
{
    (void)layout;
    finalize_calls++;
    buf[block_size - 1] = 0xF1;
    return BFS_OK;
}

static bool memory_cache(bfs_cache_t *cache, uint32_t slots, uint32_t limit)
{
    memset(&memory, 0, sizeof(memory));
    memory.bio.ops = &memory_ops;
    memory.bio.block_size = BLK_SIZE;
    memory.bio.block_count = MEM_BLOCKS;
    finalize_calls = 0;
    if (bfs_cache_init(cache, &memory.bio, slots) != BFS_OK) return false;
    bfs_cache_set_deferred_node_limit(cache, limit);
    return true;
}

static void image(uint8_t *buf, uint8_t fill)
{
    memset(buf, fill, BLK_SIZE);
}

static void test_cache_defers_until_flush(void)
{
    bfs_cache_t cache;
    TEST_ASSERT(memory_cache(&cache, 8, 4));
    uint8_t buf[BLK_SIZE], out[BLK_SIZE];
    image(buf, 0x11);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 5, buf, mark_final, NULL), BFS_OK);
    image(buf, 0x22);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 5, buf, mark_final, NULL), BFS_OK);
    TEST_ASSERT_EQ(memory.total_writes, 0);
    TEST_ASSERT_EQ(cache.dirty_count, 1);
    /* Reads see the image and trust it as an engine-written node. */
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, out), BFS_OK);
    TEST_ASSERT_EQ(out[0], 0x22);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 5));
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    TEST_ASSERT_EQ(finalize_calls, 1);
    TEST_ASSERT_EQ(memory.writes[5], 1);
    TEST_ASSERT_EQ(memory.blocks[5][0], 0x22);
    TEST_ASSERT_EQ(memory.blocks[5][BLK_SIZE - 1], 0xF1);
    TEST_ASSERT_EQ(cache.dirty_count, 0);
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    TEST_ASSERT_EQ(memory.writes[5], 1);
    bfs_cache_destroy(&cache);
}

static void test_cache_never_evicts_dirty_slots(void)
{
    bfs_cache_t cache;
    TEST_ASSERT(memory_cache(&cache, 8, 4));
    uint8_t buf[BLK_SIZE];
    for (bfs_blk_t blk = 1; blk <= 4; blk++) {
        image(buf, (uint8_t)blk);
        TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, blk, buf, mark_final, NULL),
                       BFS_OK);
    }
    /* Reads through every clean slot many times over. */
    for (uint32_t round = 0; round < 5; round++)
        for (bfs_blk_t blk = 10; blk < 30; blk++)
            TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, blk, buf), BFS_OK);
    TEST_ASSERT_EQ(memory.total_writes, 0);
    for (bfs_blk_t blk = 1; blk <= 4; blk++) {
        TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, blk, buf), BFS_OK);
        TEST_ASSERT_EQ(buf[0], (uint8_t)blk);
    }
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    for (bfs_blk_t blk = 1; blk <= 4; blk++) TEST_ASSERT_EQ(memory.writes[blk], 1);
    bfs_cache_destroy(&cache);
}

static void test_cache_limit_writes_oldest_first(void)
{
    bfs_cache_t cache;
    TEST_ASSERT(memory_cache(&cache, 8, 2));
    uint8_t buf[BLK_SIZE];
    image(buf, 1);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 7, buf, mark_final, NULL), BFS_OK);
    image(buf, 2);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 3, buf, mark_final, NULL), BFS_OK);
    image(buf, 3);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 9, buf, mark_final, NULL), BFS_OK);
    TEST_ASSERT_EQ(memory.writes[7], 1);
    TEST_ASSERT_EQ(memory.total_writes, 1);
    TEST_ASSERT_EQ(cache.dirty_count, 2);
    /* A limit above half of the slots is clamped. */
    bfs_cache_set_deferred_node_limit(&cache, 100);
    TEST_ASSERT_EQ(cache.dirty_limit, 4);
    bfs_cache_destroy(&cache);
}

static void test_cache_discard_and_write_through(void)
{
    bfs_cache_t cache;
    TEST_ASSERT(memory_cache(&cache, 8, 4));
    uint8_t buf[BLK_SIZE];
    image(buf, 0x31);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 4, buf, mark_final, NULL), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 6, buf, mark_final, NULL), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 8, buf, mark_final, NULL), BFS_OK);
    bfs_bio_discard_deferred(&cache.bio, 4);
    TEST_ASSERT_EQ(cache.dirty_count, 2);
    /* A direct write supersedes the deferred image. */
    image(buf, 0x77);
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 6, buf), BFS_OK);
    TEST_ASSERT_EQ(cache.dirty_count, 1);
    bfs_bio_discard_deferred(&cache.bio, BFS_BLK_NULL);
    TEST_ASSERT_EQ(cache.dirty_count, 0);
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    TEST_ASSERT_EQ(memory.writes[4], 0);
    TEST_ASSERT_EQ(memory.writes[8], 0);
    TEST_ASSERT_EQ(memory.writes[6], 1);
    TEST_ASSERT_EQ(memory.blocks[6][0], 0x77);
    TEST_ASSERT_EQ(finalize_calls, 0);
    bfs_cache_destroy(&cache);
}

static void test_cache_failed_write_back_keeps_image(void)
{
    bfs_cache_t cache;
    TEST_ASSERT(memory_cache(&cache, 8, 1));
    uint8_t buf[BLK_SIZE];
    image(buf, 0x41);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 2, buf, mark_final, NULL), BFS_OK);
    memory.fail_writes = true;
    /* Making room fails: the new image goes beyond the limit and the old
     * one stays dirty. */
    image(buf, 0x42);
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 3, buf, mark_final, NULL), BFS_OK);
    TEST_ASSERT_EQ(cache.dirty_count, 2);
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_ERR_IO);
    TEST_ASSERT_EQ(cache.dirty_count, 2);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, buf), BFS_OK);
    TEST_ASSERT_EQ(buf[0], 0x41);
    memory.fail_writes = false;
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    TEST_ASSERT_EQ(cache.dirty_count, 0);
    TEST_ASSERT_EQ(memory.blocks[2][0], 0x41);
    TEST_ASSERT_EQ(memory.blocks[3][0], 0x42);
    bfs_cache_destroy(&cache);
}

/* ── Filesystem guarantees ─────────────────────────────────── */

typedef struct {
    bfs_bio_t base;
    bfs_bio_t *inner;
    uint32_t writes;
    uint32_t crash_after;   /* 0: never; later writes are silently lost */
    uint32_t fail_at;       /* 0: never; this write returns BFS_ERR_IO */
    bool persist_failure;
    bool failing;
} faulty_bio_t;

static bfs_err_t faulty_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    return bfs_bio_read(((faulty_bio_t *)bio)->inner, blk, buf);
}

static bfs_err_t faulty_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    faulty_bio_t *f = (faulty_bio_t *)bio;
    f->writes++;
    if (f->crash_after && f->writes > f->crash_after) return BFS_OK;
    if (f->failing || (f->fail_at && f->writes == f->fail_at)) {
        f->failing = f->persist_failure;
        return BFS_ERR_IO;
    }
    return bfs_bio_write(f->inner, blk, buf);
}

static bfs_err_t faulty_sync(bfs_bio_t *bio)
{
    faulty_bio_t *f = (faulty_bio_t *)bio;
    if (f->crash_after && f->writes > f->crash_after) return BFS_OK;
    return bfs_bio_sync(f->inner);
}

static void faulty_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t faulty_ops = {
    .read_block = faulty_read, .write_block = faulty_write,
    .sync = faulty_sync, .close = faulty_close,
};

static void faulty_init(faulty_bio_t *f, bfs_bio_t *inner)
{
    memset(f, 0, sizeof(*f));
    f->base.ops = &faulty_ops;
    f->base.block_size = inner->block_size;
    f->base.block_count = inner->block_count;
    f->inner = inner;
}

static uint8_t payload[2 * BLK_SIZE + 100];

/* Committed inline files the workload changes: "keep" is truncated from three
 * blocks to one, "grow" gets a block behind a hole and becomes a tree. */
#define KEEP_OLD_SIZE (3u * BLK_SIZE)
#define KEEP_NEW_SIZE BLK_SIZE
#define GROW_OLD_SIZE BLK_SIZE
#define GROW_NEW_SIZE (3u * BLK_SIZE + 100u)
static uint8_t keep_data[KEEP_OLD_SIZE];

static bool write_new_file(bfs_fs_t *fs, const char *name, const uint8_t *data,
                           uint32_t size)
{
    uint32_t ino;
    bfs_file_t file;
    return bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)strlen(name), &ino) == BFS_OK &&
           bfs_file_open(&file, fs, ino) == BFS_OK &&
           bfs_file_write(&file, data, size) == (int32_t)size;
}

/* A snapshot makes every commit take the refcount-aware publication path
 * instead of the sealed single-leaf settlement. */
static bool baseline_snapshot;

static void make_baseline(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_t fs;
    uint32_t ino;
    bfs_fs_format(bio, "Deferred", 0);
    bfs_fs_mount(&fs, bio);
    for (int i = 0; i < 6; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "old%d", i);
        bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
    }
    for (uint32_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i * 13u + 5u);
    for (uint32_t i = 0; i < sizeof(keep_data); i++) keep_data[i] = (uint8_t)(i * 7u + 3u);
    write_new_file(&fs, "keep", keep_data, KEEP_OLD_SIZE);
    write_new_file(&fs, "grow", payload, GROW_OLD_SIZE);
    bfs_fs_sync(&fs);
    if (baseline_snapshot) bfs_snapshot_create(&fs, "base");
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
}

static bfs_err_t open_named(bfs_fs_t *fs, const char *name, bfs_file_t *file)
{
    uint32_t ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name,
                                   (uint8_t)strlen(name), &ino, &type);
    return err == BFS_OK ? bfs_file_open(file, fs, ino) : err;
}

/* Truncate an inline extent and convert another into a tree. */
static bfs_err_t change_inline_files(bfs_fs_t *fs)
{
    bfs_file_t file;
    bfs_err_t err = open_named(fs, "keep", &file);
    if (err == BFS_OK) err = bfs_file_truncate(&file, KEEP_NEW_SIZE);
    if (err == BFS_OK) err = open_named(fs, "grow", &file);
    if (err != BFS_OK) return err;
    if (bfs_file_seek(&file, 3u * BLK_SIZE, BFS_SEEK_SET) != 3 * BLK_SIZE ||
        bfs_file_write(&file, payload, 100) != 100)
        return BFS_ERR_IO;
    return BFS_OK;
}

/* 1: old content, 2: new content, 0: anything else. */
static int inline_files_state(bfs_fs_t *fs)
{
    static uint8_t back[GROW_NEW_SIZE + 1];
    bfs_file_t file;
    if (open_named(fs, "keep", &file) != BFS_OK) return 0;
    int32_t keep = bfs_file_read(&file, back, sizeof(back));
    bool keep_old = keep == (int32_t)KEEP_OLD_SIZE && !memcmp(back, keep_data, KEEP_OLD_SIZE);
    bool keep_new = keep == (int32_t)KEEP_NEW_SIZE && !memcmp(back, keep_data, KEEP_NEW_SIZE);
    if (open_named(fs, "grow", &file) != BFS_OK) return 0;
    int32_t grow = bfs_file_read(&file, back, sizeof(back));
    bool grow_old = grow == (int32_t)GROW_OLD_SIZE && !memcmp(back, payload, GROW_OLD_SIZE);
    bool grow_new = grow == (int32_t)GROW_NEW_SIZE && !memcmp(back, payload, BLK_SIZE) &&
                    !memcmp(back + 3u * BLK_SIZE, payload, 100);
    for (uint32_t i = BLK_SIZE; grow_new && i < 3u * BLK_SIZE; i++)
        grow_new = back[i] == 0;
    return keep_old && grow_old ? 1 : keep_new && grow_new ? 2 : 0;
}

/* Several namespace and data changes in one transaction, then one commit. */
static bfs_err_t run_workload(bfs_fs_t *fs)
{
    uint32_t ino;
    for (int i = 0; i < 8; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "new%d", i);
        bfs_err_t err = bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
        if (err != BFS_OK) return err;
        bfs_file_t file;
        err = bfs_file_open(&file, fs, ino);
        if (err != BFS_OK) return err;
        if (bfs_file_write(&file, payload, sizeof(payload)) != (int32_t)sizeof(payload))
            return BFS_ERR_IO;
    }
    for (int i = 0; i < 6; i += 2) {
        char name[16];
        int len = snprintf(name, sizeof(name), "old%d", i);
        bfs_err_t err = bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)len);
        if (err != BFS_OK) return err;
    }
    /* Created and deleted within the transaction: its nodes are freed while
     * still deferred. */
    bfs_err_t err = bfs_fs_create_file(fs, BFS_ROOT_INO, "temp", 4, &ino);
    if (err == BFS_OK) err = bfs_fs_delete_file(fs, BFS_ROOT_INO, "temp", 4);
    if (err == BFS_OK) err = change_inline_files(fs);
    if (err != BFS_OK) return err;
    return bfs_fs_sync(fs);
}

static bool name_exists(bfs_fs_t *fs, const char *name)
{
    uint32_t ino, type;
    return bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name,
                          (uint8_t)strlen(name), &ino, &type) == BFS_OK;
}

/* 1: old state, 2: complete new state, 0: anything else. */
static int committed_state(bfs_fs_t *fs)
{
    bool old_state = true, new_state = true;
    for (int i = 0; i < 6; i++) {
        char name[16];
        snprintf(name, sizeof(name), "old%d", i);
        bool exists = name_exists(fs, name);
        if (!exists) old_state = false;
        if (exists != (i % 2 == 1)) new_state = false;
    }
    for (int i = 0; i < 8; i++) {
        char name[16];
        snprintf(name, sizeof(name), "new%d", i);
        bool exists = name_exists(fs, name);
        if (exists) old_state = false;
        if (!exists) new_state = false;
        uint32_t ino, type;
        if (exists &&
            bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, 4, &ino, &type) == BFS_OK) {
            static uint8_t back[sizeof(payload)];
            bfs_file_t file;
            if (bfs_file_open(&file, fs, ino) != BFS_OK ||
                bfs_file_read(&file, back, sizeof(back)) != (int32_t)sizeof(back) ||
                memcmp(back, payload, sizeof(back)) != 0)
                new_state = false;
        }
    }
    if (name_exists(fs, "temp")) old_state = new_state = false;
    int files = inline_files_state(fs);
    if (files != 1) old_state = false;
    if (files != 2) new_state = false;
    return old_state ? 1 : new_state ? 2 : 0;
}

/* Mount the device image without a cache, check it and report its state.
 *
 * With a snapshot, commits take the refcount-aware path: blocks that become
 * free are reclaimed after the superblock publication. A crash in between
 * leaks them, which the format permits, so the snapshot variant accepts
 * leaked blocks in the new state. Checker errors are never accepted. */
static int verify_image(bfs_bio_t *bio)
{
    bfs_fs_t fs;
    if (bfs_fs_mount(&fs, bio) != BFS_OK) return -1;
    int state = committed_state(&fs);
    if (bfs_fs_unmount(&fs) != BFS_OK) return -4;
    bfs_fs_t check;
    bfs_fsck_report_t report = {0};
    if (bfs_fs_mount_readonly(&check, bio) != BFS_OK) return -2;
    bfs_err_t err = bfs_fs_check(&check, false, &report);
    bfs_fs_unmount(&check);
    bool leak_allowed = baseline_snapshot && state == 2;
    bool clean = err == BFS_OK && !report.errors &&
                 (report.leaked_blocks ? leak_allowed && report.warnings == 1
                                       : !report.warnings);
    if (!clean) fprintf(stderr, "  fsck: state %d errors %u warnings %u leaked %u\n",
                        state, report.errors, report.warnings, report.leaked_blocks);
    if (!clean) return -3;
    return state;
}

/* Dirty node limit of the cache in cached_run; small values force early
 * write-back of deferred nodes in the middle of mutations. */
static uint32_t dirty_limit = CACHE_SLOTS / 2;

/* Run the workload over a deferring cache on top of the faulty device. */
static bfs_err_t cached_run(faulty_bio_t *f, uint32_t *device_writes)
{
    bfs_cache_t cache;
    bfs_fs_t fs;
    if (bfs_cache_init(&cache, &f->base, CACHE_SLOTS) != BFS_OK) return BFS_ERR_NOMEM;
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, dirty_limit);
    bfs_err_t err = bfs_fs_mount(&fs, &cache.bio);
    if (err != BFS_OK) {
        bfs_cache_destroy(&cache);
        return err;
    }
    uint32_t before = f->writes;
    err = run_workload(&fs);
    if (device_writes) *device_writes = f->writes - before;
    bfs_fs_abandon(&fs);
    bfs_cache_destroy(&cache);
    return err;
}

static void test_deferral_saves_device_writes(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    faulty_bio_t f;
    faulty_init(&f, bio);
    uint32_t deferred = 0;
    TEST_ASSERT_EQ(cached_run(&f, &deferred), BFS_OK);
    TEST_ASSERT_EQ(verify_image(bio), 2);
    bfs_bio_close(bio);

    /* The same workload with write-through nodes. */
    make_baseline();
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    faulty_init(&f, bio);
    bfs_cache_t cache;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &f.base, CACHE_SLOTS), BFS_OK);
    bfs_cache_set_node_write_retention(&cache, true);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);
    uint32_t before = f.writes;
    TEST_ASSERT_EQ(run_workload(&fs), BFS_OK);
    uint32_t direct = f.writes - before;
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_cache_destroy(&cache);
    TEST_ASSERT_EQ(verify_image(bio), 2);
    bfs_bio_close(bio);
    TEST_ASSERT(deferred * 2 < direct);
    unlink(TEST_IMG);
}

static void crash_at_every_write(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    faulty_bio_t f;
    faulty_init(&f, bio);
    uint32_t total = 0;
    TEST_ASSERT_EQ(cached_run(&f, &total), BFS_OK);
    bfs_bio_close(bio);
    TEST_ASSERT(total > 4);

    unsigned old_states = 0, new_states = 0;
    for (uint32_t cut = 1; cut <= total; cut++) {
        make_baseline();
        bio = bio_emu_open(TEST_IMG, BLK_SIZE);
        faulty_init(&f, bio);
        bfs_cache_t probe;
        bfs_fs_t fs;
        /* Count the mount's own writes so the cut lands inside the workload. */
        TEST_ASSERT_EQ(bfs_cache_init(&probe, &f.base, CACHE_SLOTS), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &probe.bio), BFS_OK);
        uint32_t mount_writes = f.writes;
        bfs_fs_abandon(&fs);
        bfs_cache_destroy(&probe);
        faulty_init(&f, bio);
        f.crash_after = mount_writes + cut;
        (void)cached_run(&f, NULL);
        int state = verify_image(bio);
        bfs_bio_close(bio);
        if (state != 1 && state != 2)
            fprintf(stderr, "  crash cut %u of %u: state %d\n", cut, total, state);
        TEST_ASSERT(state == 1 || state == 2);
        if (state == 1) old_states++; else new_states++;
    }
    TEST_ASSERT(old_states > 0);
    TEST_ASSERT(new_states > 0);
    unlink(TEST_IMG);
}

static void write_failure_at_every_write(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    faulty_bio_t f;
    faulty_init(&f, bio);
    uint32_t total = 0;
    TEST_ASSERT_EQ(cached_run(&f, &total), BFS_OK);
    bfs_bio_close(bio);

    unsigned failures = 0;
    for (uint32_t at = 1; at <= total; at++) {
        for (int persist = 0; persist < 2; persist++) {
            make_baseline();
            bio = bio_emu_open(TEST_IMG, BLK_SIZE);
            faulty_init(&f, bio);
            bfs_cache_t probe;
            bfs_fs_t fs;
            TEST_ASSERT_EQ(bfs_cache_init(&probe, &f.base, CACHE_SLOTS), BFS_OK);
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, &probe.bio), BFS_OK);
            uint32_t mount_writes = f.writes;
            bfs_fs_abandon(&fs);
            bfs_cache_destroy(&probe);
            faulty_init(&f, bio);
            f.fail_at = mount_writes + at;
            f.persist_failure = persist != 0;
            bfs_err_t err = cached_run(&f, NULL);
            if (err != BFS_OK) failures++;
            int state = verify_image(bio);
            bfs_bio_close(bio);
            if (state != 1 && state != 2)
                fprintf(stderr, "  write failure %u (persist %d) of %u: err %d state %d\n",
                        at, persist, total, err, state);
            TEST_ASSERT(state == 1 || state == 2);
            TEST_ASSERT(err != BFS_OK || state == 2);
        }
    }
    TEST_ASSERT(failures > 0);
    unlink(TEST_IMG);
}

/* A file created and deleted in one transaction is queued for reclamation
 * after publication; its nodes may still be written, but the committed state
 * never references them. */
static void test_crash_at_every_write_keeps_committed_state(void)
{
    static const uint32_t limits[] = { 1, 4, CACHE_SLOTS / 2 };
    for (int snapshot = 0; snapshot < 2; snapshot++) {
        baseline_snapshot = snapshot != 0;
        for (uint32_t i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
            dirty_limit = limits[i];
            crash_at_every_write();
        }
    }
    baseline_snapshot = false;
    dirty_limit = CACHE_SLOTS / 2;
}

static void test_write_failure_at_every_write_keeps_committed_state(void)
{
    static const uint32_t limits[] = { 1, 4, CACHE_SLOTS / 2 };
    for (int snapshot = 0; snapshot < 2; snapshot++) {
        baseline_snapshot = snapshot != 0;
        for (uint32_t i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
            dirty_limit = limits[i];
            write_failure_at_every_write();
        }
    }
    baseline_snapshot = false;
    dirty_limit = CACHE_SLOTS / 2;
}

static void test_file_deleted_in_transaction(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    faulty_bio_t f;
    faulty_init(&f, bio);
    bfs_cache_t cache;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &f.base, CACHE_SLOTS), BFS_OK);
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, CACHE_SLOTS / 2);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "gone", 4, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&file, payload, sizeof(payload)), (int32_t)sizeof(payload));
    bfs_blk_t extent_root = file.extents.tree.root;
    TEST_ASSERT(extent_root != BFS_BLK_NULL);
    TEST_ASSERT(cache.dirty_count > 0);
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "gone", 4), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(cache.dirty_count, 0);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_cache_destroy(&cache);
    TEST_ASSERT_EQ(verify_image(bio), 1);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── B-tree nodes freed within the transaction ─────────────── */

static int u32_compare(const void *a, const void *b)
{
    uint32_t va = bfs_load_be32(a), vb = bfs_load_be32(b);
    return va < vb ? -1 : va > vb ? 1 : 0;
}

static const bfs_btree_ops_t u32_ops = {
    .key_compare = u32_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
};

static const bfs_btree_ops_t u32_cached_ops = {
    .key_compare = u32_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = true,
};

typedef struct {
    bfs_allocator_t base;
    bfs_blk_t next;
    bfs_blk_t freed[MEM_BLOCKS];
    uint32_t freed_count;
} bump_alloc_t;

static bfs_blk_t bump_alloc(bfs_allocator_t *a)
{
    bump_alloc_t *b = (bump_alloc_t *)a;
    return b->next < MEM_BLOCKS ? b->next++ : BFS_BLK_NULL;
}

static bfs_err_t bump_dealloc(bfs_allocator_t *a, bfs_blk_t blk)
{
    bump_alloc_t *b = (bump_alloc_t *)a;
    if (b->freed_count == MEM_BLOCKS) return BFS_ERR_NOSPC;
    b->freed[b->freed_count++] = blk;
    return BFS_OK;
}

static bfs_err_t tree_put(bfs_btree_t *tree, uint32_t k, bool insert)
{
    uint32_t key = bfs_be32(k), val = bfs_be32(k * 3u);
    return insert ? bfs_btree_insert(tree, &key, &val) : bfs_btree_delete(tree, &key);
}

static void test_nodes_freed_in_transaction_are_not_written(void)
{
    bfs_cache_t cache;
    TEST_ASSERT(memory_cache(&cache, 16, 8));
    bump_alloc_t alloc = { .base = { .alloc = bump_alloc, .dealloc = bump_dealloc },
                           .next = 2 };
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &cache.bio, &alloc.base, &u32_ops,
                                  BFS_BLK_NULL, 1), BFS_OK);
    bfs_btree_owned_t owned = {0};
    tree.free_sink.owned = &owned;
    for (uint32_t k = 1; k <= 1500; k++) TEST_ASSERT_EQ(tree_put(&tree, k, true), BFS_OK);
    TEST_ASSERT(tree.height >= 2);
    TEST_ASSERT_EQ(memory.total_writes, 0);
    for (uint32_t k = 1; k <= 1490; k++) TEST_ASSERT_EQ(tree_put(&tree, k, false), BFS_OK);
    TEST_ASSERT(alloc.freed_count > 0);
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    TEST_ASSERT(memory.total_writes > 0);
    for (uint32_t i = 0; i < alloc.freed_count; i++)
        TEST_ASSERT_EQ(memory.writes[alloc.freed[i]], 0);

    /* The flushed nodes carry valid CRCs: read them back without the cache. */
    bfs_btree_t check;
    TEST_ASSERT_EQ(bfs_btree_init(&check, &memory.bio, &alloc.base, &u32_ops,
                                  tree.root, 1), BFS_OK);
    for (uint32_t k = 1; k <= 1500; k++) {
        uint32_t key = bfs_be32(k), val;
        TEST_ASSERT_EQ(bfs_btree_search(&check, &key, &val),
                       k > 1490 ? BFS_OK : BFS_ERR_NOTFOUND);
    }
    bfs_btree_owned_destroy(&owned);
    bfs_cache_destroy(&cache);
}

/* With a deferring cache, run allocations rewrite the owned free-space root
 * in place; the accounting must follow the published replacement. */
static void test_owned_free_root_rewritten_in_place(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_cache_t cache;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, bio, CACHE_SLOTS), BFS_OK);
    bfs_cache_set_deferred_node_limit(&cache, CACHE_SLOTS / 2);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);
    TEST_ASSERT_EQ(fs.freespace.tree.height, 1);
    bfs_blk_t first = bfs_freespace_alloc(&fs.freespace, 4);
    TEST_ASSERT(first != BFS_BLK_NULL);
    bfs_blk_t owned_root = fs.freespace.tree.root;
    uint32_t free_before = fs.freespace.total_free;
    bfs_blk_t second = bfs_freespace_alloc(&fs.freespace, 4);
    TEST_ASSERT(second != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fs.freespace.tree.root, owned_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, free_before - 4);
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, first, 4), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, second, 4), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_cache_destroy(&cache);
    TEST_ASSERT_EQ(verify_image(bio), 1);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* A deferred image is validated when it is written, so searches can use it in
 * place; trees without the validation opt-in are not marked. */
static void test_deferred_images_are_validated_on_write(void)
{
    const bfs_btree_ops_t *variants[] = { &u32_cached_ops, &u32_ops };
    for (uint32_t v = 0; v < 2; v++) {
        bfs_cache_t cache;
        TEST_ASSERT(memory_cache(&cache, 16, 8));
        bump_alloc_t alloc = { .base = { .alloc = bump_alloc, .dealloc = bump_dealloc },
                               .next = 2 };
        bfs_btree_t tree;
        TEST_ASSERT_EQ(bfs_btree_init(&tree, &cache.bio, &alloc.base, variants[v],
                                      BFS_BLK_NULL, 1), BFS_OK);
        bfs_btree_owned_t owned = {0};
        tree.free_sink.owned = &owned;
        for (uint32_t k = 1; k <= 20; k++) TEST_ASSERT_EQ(tree_put(&tree, k, true), BFS_OK);
        bfs_node_validation_t context = {
            .key_compare = u32_compare, .key_size = sizeof(uint32_t),
            .val_size = sizeof(uint32_t), .block_size = BLK_SIZE,
            .block_count = MEM_BLOCKS,
        };
        TEST_ASSERT_EQ(bfs_bio_node_structure_valid(&cache.bio, tree.root, &context),
                       v == 0);
        uint32_t key = bfs_be32(7), val;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_OK);
        TEST_ASSERT_EQ(bfs_load_be32(&val), 21u);
        bfs_btree_owned_destroy(&owned);
        bfs_cache_destroy(&cache);
    }
}

/* Readers that do not share the transaction's registry, such as a separately
 * opened directory tree, still see the deferred nodes through the cache. */
static void test_independent_reader_sees_deferred_nodes(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_cache_t cache;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, bio, CACHE_SLOTS), BFS_OK);
    bfs_cache_set_deferred_node_limit(&cache, CACHE_SLOTS / 2);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "first", 5, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "second", 6, &ino), BFS_OK);
    TEST_ASSERT(cache.dirty_count > 0);
    bfs_dir_tree_t reader;
    bfs_dir_init(&reader, &cache.bio, bfs_freespace_allocator(&fs.freespace),
                 fs.dir_tree.tree.root, 1);
    uint32_t found, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&reader, BFS_ROOT_INO, "second", 6, &found, &type),
                   BFS_OK);
    TEST_ASSERT_EQ(found, ino);
    bfs_fs_abandon(&fs);
    TEST_ASSERT_EQ(cache.dirty_count, 0);
    bfs_cache_destroy(&cache);
    TEST_ASSERT_EQ(verify_image(bio), 1);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* A leaf replaced in the live transaction stays resident and valid until
 * the commit reclaims it. A later search must not answer from it. */
static void test_search_skips_replaced_leaf(void)
{
    make_baseline();
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_cache_t cache;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, bio, CACHE_SLOTS), BFS_OK);
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, CACHE_SLOTS / 2);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "hint", 4, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    uint32_t protection = bfs_be32(inode.protection) ^ 0x0Fu;
    inode.protection = bfs_be32(protection);
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &inode), BFS_OK);
    bfs_inode_t again;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &again), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(again.protection), protection);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_cache_destroy(&cache);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Deferred node writes")
    TEST_RUN(test_cache_defers_until_flush);
    TEST_RUN(test_cache_never_evicts_dirty_slots);
    TEST_RUN(test_cache_limit_writes_oldest_first);
    TEST_RUN(test_cache_discard_and_write_through);
    TEST_RUN(test_cache_failed_write_back_keeps_image);
    TEST_RUN(test_deferral_saves_device_writes);
    TEST_RUN(test_crash_at_every_write_keeps_committed_state);
    TEST_RUN(test_write_failure_at_every_write_keeps_committed_state);
    TEST_RUN(test_file_deleted_in_transaction);
    TEST_RUN(test_search_skips_replaced_leaf);
    TEST_RUN(test_nodes_freed_in_transaction_are_not_written);
    TEST_RUN(test_independent_reader_sees_deferred_nodes);
    TEST_RUN(test_owned_free_root_rewritten_in_place);
    TEST_RUN(test_deferred_images_are_validated_on_write);
TEST_SUITE_END()
