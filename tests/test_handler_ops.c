#include <assert.h>
/* SPDX-License-Identifier: MPL-2.0 */
/*
 * test_handler_ops.c — Host tests simulating AmigaOS handler behavior
 *
 * These tests reproduce the exact sequence of operations the handler
 * performs, including sync-after-metadata and sync-on-close patterns.
 */

#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_bio.h"
#include "block_device_emu.h"
#include "test_harness.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static bfs_bio_t *bio;
static bfs_fs_t fs;

static void setup(void)
{
    bio = bio_emu_create("/tmp/test_handler_ops.img", 4096, 8192);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT(bfs_fs_format(bio, "Test", 0) == BFS_OK);
    TEST_ASSERT(bfs_fs_mount(&fs, bio) == BFS_OK);
}

static void teardown(void)
{
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
}

/* Simulate handler: create file, write, close+sync */
static uint32_t handler_create_write(const char *name, uint8_t nlen,
                                      const void *data, uint32_t size)
{
    uint32_t ino;
    assert(bfs_fs_create_file(&fs, 1, name, nlen, &ino) == BFS_OK);
    bfs_fs_sync(&fs); /* sync after metadata op */
    bfs_file_t f;
    assert(bfs_file_open(&f, &fs, ino) == BFS_OK);
    assert(bfs_file_write(&f, data, size) == (int32_t)size);
    bfs_fs_sync(&fs); /* sync on close */
    return ino;
}

/* Simulate handler: open existing, truncate, write, close+sync */
static void handler_overwrite(uint32_t ino, const void *data, uint32_t size)
{
    bfs_file_t f;
    assert(bfs_file_open(&f, &fs, ino) == BFS_OK);
    bfs_file_truncate(&f, 0);
    /* Handler does NOT sync after FINDOUTPUT (excluded from auto-sync) */
    assert(bfs_file_write(&f, data, size) == (int32_t)size);
    bfs_fs_sync(&fs); /* sync on close */
}

/* Simulate handler: delete file + sync */
static void handler_delete(const char *name, uint8_t nlen)
{
    assert(bfs_fs_delete_file(&fs, 1, name, nlen) == BFS_OK);
    bfs_fs_sync(&fs); /* sync after metadata op */
}

static uint32_t xorshift(uint32_t s) { s^=s<<13; s^=s>>17; s^=s<<5; return s; }

static void fill_buf(uint8_t *buf, uint32_t size, uint32_t seed)
{
    for (uint32_t i = 0; i < size; i++) { seed = xorshift(seed); buf[i] = (uint8_t)seed; }
}

/* ── Test: repeated overwrite (reopen_22 scenario) ─────────── */
static void test_handler_repeated_overwrite(void)
{
    setup();
    uint8_t buf[100];
    fill_buf(buf, 100, 0x1111);
    uint32_t ino = handler_create_write("test.dat", 8, buf, 100);

    for (int i = 0; i < 50; i++) {
        fill_buf(buf, 100, 0x2200 + i);
        handler_overwrite(ino, buf, 100);
    }

    /* Verify last write */
    bfs_file_t f;
    TEST_ASSERT(bfs_file_open(&f, &fs, ino) == BFS_OK);
    uint8_t rbuf[100];
    TEST_ASSERT(bfs_file_read(&f, rbuf, 100) == 100);
    fill_buf(buf, 100, 0x2200 + 49);
    TEST_ASSERT(memcmp(rbuf, buf, 100) == 0);

    /* Verify free space is stable (no leak) */
    uint32_t free_after = fs.freespace.total_free;
    TEST_ASSERT(free_after > 7500); /* should be nearly full capacity */
    teardown();
}

/* ── Test: many create+delete cycles (cycles_09 scenario) ──── */
static void test_handler_create_delete_cycles(void)
{
    setup();
    uint8_t buf[4096];
    uint32_t initial_free = fs.freespace.total_free;

    for (int cycle = 0; cycle < 10; cycle++) {
        for (int i = 0; i < 10; i++) {
            char name[16];
            int n = cycle * 10 + i;
            name[0] = 'f'; name[1] = '0' + (n / 10); name[2] = '0' + (n % 10); name[3] = 0;
            fill_buf(buf, 4096, 0xC000 + n);
            handler_create_write(name, 3, buf, 4096);
        }
        for (int i = 0; i < 10; i++) {
            char name[16];
            int n = cycle * 10 + i;
            name[0] = 'f'; name[1] = '0' + (n / 10); name[2] = '0' + (n % 10); name[3] = 0;
            handler_delete(name, 3);
        }
    }

    /* After all cycles, free space should be close to initial */
    uint32_t free_after = fs.freespace.total_free;
    TEST_ASSERT(free_after >= initial_free - 20); /* allow small overhead */
    teardown();
}

/* ── Test: fill disk then delete (diskfull_23 scenario) ─────── */
static void test_handler_fill_and_recover(void)
{
    setup();
    uint8_t *buf = malloc(65536);
    memset(buf, 0xAA, 65536);
    uint32_t total = 0;

    /* Write until full */
    for (int i = 0; i < 200; i++) {
        char name[8];
        name[0] = 'F'; name[1] = '0' + (i / 10); name[2] = '0' + (i % 10); name[3] = 0;
        uint32_t ino;
        if (bfs_fs_create_file(&fs, 1, name, 3, &ino) != BFS_OK) break;
        bfs_fs_sync(&fs);
        bfs_file_t f;
        if (bfs_file_open(&f, &fs, ino) != BFS_OK) break;
        int32_t w = bfs_file_write(&f, buf, 65536);
        bfs_fs_sync(&fs);
        if (w <= 0) break;
        total++;
    }
    TEST_ASSERT(total >= 3);

    /* Delete all and verify recovery */
    for (uint32_t i = 0; i < total; i++) {
        char name[8];
        name[0] = 'F'; name[1] = '0' + (i / 10); name[2] = '0' + (i % 10); name[3] = 0;
        handler_delete(name, 3);
        if (i % 10 == 9) bfs_fs_sync(&fs); /* periodic reclaim */
    }

    bfs_fs_sync(&fs); bfs_fs_sync(&fs); /* double sync to reclaim all */
    /* Verify space was recovered */
    TEST_ASSERT(fs.freespace.total_free > fs.freespace.global_reserve);

    free(buf);
    teardown();
}

/* ── Test: large file write+verify (multiext_14 scenario) ──── */
static void test_handler_large_file(void)
{
    setup();
    uint8_t *buf = malloc(65536);
    uint32_t ino;
    TEST_ASSERT(bfs_fs_create_file(&fs, 1, "big.dat", 7, &ino) == BFS_OK);
    bfs_fs_sync(&fs);

    bfs_file_t f;
    TEST_ASSERT(bfs_file_open(&f, &fs, ino) == BFS_OK);
    uint32_t seed = 0xB161;
    for (int i = 0; i < 32; i++) { /* 2MB */
        fill_buf(buf, 65536, seed + i);
        TEST_ASSERT(bfs_file_write(&f, buf, 65536) == 65536);
    }
    bfs_fs_sync(&fs);

    /* Verify */
    bfs_file_t f2;
    TEST_ASSERT(bfs_file_open(&f2, &fs, ino) == BFS_OK);
    for (int i = 0; i < 32; i++) {
        uint8_t expected[65536];
        fill_buf(expected, 65536, seed + i);
        TEST_ASSERT(bfs_file_read(&f2, buf, 65536) == 65536);
        TEST_ASSERT(memcmp(buf, expected, 65536) == 0);
    }

    free(buf);
    teardown();
}

/* ── Test: many files in one directory ─────────────────────── */
static void test_handler_many_files(void)
{
    setup();
    uint8_t buf[64];

    for (int i = 0; i < 200; i++) {
        char name[8];
        name[0] = 'f'; name[1] = '0' + (i / 100);
        name[2] = '0' + ((i / 10) % 10); name[3] = '0' + (i % 10); name[4] = 0;
        fill_buf(buf, 64, 0xA000 + i);
        handler_create_write(name, 4, buf, 64);
    }

    /* Verify last file */
    uint32_t ino, type;
    TEST_ASSERT(bfs_dir_lookup(&fs.dir_tree, 1, "f199", 4, &ino, &type) == BFS_OK);
    bfs_file_t f;
    TEST_ASSERT(bfs_file_open(&f, &fs, ino) == BFS_OK);
    uint8_t rbuf[64];
    TEST_ASSERT(bfs_file_read(&f, rbuf, 64) == 64);
    fill_buf(buf, 64, 0xA000 + 199);
    TEST_ASSERT(memcmp(rbuf, buf, 64) == 0);

    teardown();
}

TEST_SUITE_BEGIN("Handler Ops")
    TEST_RUN(test_handler_repeated_overwrite);
    TEST_RUN(test_handler_create_delete_cycles);
    TEST_RUN(test_handler_fill_and_recover);
    TEST_RUN(test_handler_large_file);
    TEST_RUN(test_handler_many_files);
TEST_SUITE_END()
