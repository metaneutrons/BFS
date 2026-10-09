/*
 * BFS — Real-world usage integration test
 *
 * Simulates a complete Amiga user session:
 * format → install software → use it → delete old files → defrag-like patterns
 *
 * This is the closest we can get to real hardware testing without an emulator.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_dir.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_realworld.img"
#define BLK_SIZE 4096
#define BLK_COUNT 16384 /* 64MB — realistic small Amiga partition */

/* ── Helper: write a file with known content ───────────────── */

static bfs_err_t write_file(bfs_fs_t *fs, uint32_t parent, const char *name,
                             const void *data, uint32_t size, uint32_t *ino_out)
{
    uint32_t ino;
    bfs_err_t err = bfs_fs_create_file(fs, parent, name, (uint8_t)strlen(name), &ino);
    if (err != BFS_OK) return err;
    bfs_file_t f;
    err = bfs_file_open(&f, fs, ino);
    if (err != BFS_OK) return err;
    bfs_file_write(&f, data, size);
    if (ino_out) *ino_out = ino;
    return BFS_OK;
}

/* ── Helper: read and verify file content ──────────────────── */

static bfs_err_t verify_file(bfs_fs_t *fs, uint32_t parent, const char *name,
                              const void *expected, uint32_t size)
{
    uint32_t ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, parent, name,
                                     (uint8_t)strlen(name), &ino, &type);
    if (err != BFS_OK) return err;
    bfs_file_t f;
    err = bfs_file_open(&f, fs, ino);
    if (err != BFS_OK) return err;
    uint8_t *buf = malloc(size);
    if (!buf) return BFS_ERR_NOMEM;
    int32_t n = bfs_file_read(&f, buf, size);
    if (n != (int32_t)size) { free(buf); return BFS_ERR_IO; }
    int cmp = memcmp(buf, expected, size);
    free(buf);
    return cmp == 0 ? BFS_OK : BFS_ERR_CORRUPT;
}

/* ── Scan callback ─────────────────────────────────────────── */

typedef struct { uint32_t count; } rw_scan_ctx_t;
static bool rw_count_cb(const char *n, uint8_t l, uint32_t i, uint32_t t, void *c) {
    (void)n;(void)l;(void)i;(void)t;
    ((rw_scan_ctx_t*)c)->count++; return true;
}

/* ── Test: Simulate installing a program (Workbench-style) ─── */

static void test_install_program(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Work", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Create program directory structure */
    uint32_t app_ino, libs_ino, data_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "MyApp", 5, &app_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, app_ino, "Libs", 4, &libs_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, app_ino, "Data", 4, &data_ino), BFS_OK);

    /* Install executable (simulate 50KB binary) */
    uint8_t exe_data[BLK_SIZE * 12]; /* 48KB */
    for (uint32_t i = 0; i < sizeof(exe_data); i++)
        exe_data[i] = (uint8_t)(i * 7 + 13);
    TEST_ASSERT_EQ(write_file(&fs, app_ino, "MyApp", exe_data, sizeof(exe_data), NULL), BFS_OK);

    /* Install libraries */
    uint8_t lib_data[BLK_SIZE * 3]; /* 12KB */
    memset(lib_data, 0xAA, sizeof(lib_data));
    TEST_ASSERT_EQ(write_file(&fs, libs_ino, "myapp.library", lib_data, sizeof(lib_data), NULL), BFS_OK);
    TEST_ASSERT_EQ(write_file(&fs, libs_ino, "helper.library", lib_data, sizeof(lib_data), NULL), BFS_OK);

    /* Install data files */
    const char *readme = "MyApp v1.0\nCopyright 2026\n";
    TEST_ASSERT_EQ(write_file(&fs, data_ino, "README", readme, (uint32_t)strlen(readme), NULL), BFS_OK);

    /* Set file comment */
    uint32_t exe_ino, type;
    bfs_dir_lookup(&fs.dir_tree, app_ino, "MyApp", 5, &exe_ino, &type);
    bfs_fs_set_comment(&fs, exe_ino, "Main executable", 15);

    /* Sync and unmount (like ejecting the disk) */
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);

    /* Remount and verify everything */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    TEST_ASSERT_EQ(verify_file(&fs, app_ino, "MyApp", exe_data, sizeof(exe_data)), BFS_OK);
    TEST_ASSERT_EQ(verify_file(&fs, libs_ino, "myapp.library", lib_data, sizeof(lib_data)), BFS_OK);
    TEST_ASSERT_EQ(verify_file(&fs, data_ino, "README", readme, (uint32_t)strlen(readme)), BFS_OK);

    /* Verify directory structure */
    uint32_t found_ino;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "MyApp", 5, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, app_ino, "Libs", 4, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, app_ino, "Data", 4, &found_ino, &type), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: Daily use — create, modify, delete cycle ────────── */

static void test_daily_use(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Work", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Day 1: Create project */
    uint32_t proj_ino;
    bfs_fs_mkdir(&fs, BFS_ROOT_INO, "Project", 7, &proj_ino);

    char source[2048];
    memset(source, 'A', sizeof(source));
    write_file(&fs, proj_ino, "main.c", source, sizeof(source), NULL);
    write_file(&fs, proj_ino, "utils.c", source, sizeof(source), NULL);
    write_file(&fs, proj_ino, "header.h", source, 512, NULL);
    bfs_fs_sync(&fs);

    /* Day 2: Modify a file (overwrite with new content) */
    uint32_t main_ino, type;
    bfs_dir_lookup(&fs.dir_tree, proj_ino, "main.c", 6, &main_ino, &type);
    bfs_file_t f;
    bfs_file_open(&f, &fs, main_ino);
    memset(source, 'B', sizeof(source));
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    bfs_file_write(&f, source, sizeof(source));
    bfs_fs_sync(&fs);

    /* Day 3: Delete old file, create new one */
    bfs_fs_delete_file(&fs, proj_ino, "utils.c", 7);
    write_file(&fs, proj_ino, "newutils.c", source, sizeof(source), NULL);
    bfs_fs_sync(&fs);

    /* Day 4: Rename */
    bfs_fs_rename(&fs, proj_ino, "newutils.c", 10, proj_ino, "utils.c", 7);
    bfs_fs_sync(&fs);

    /* Verify final state */
    TEST_ASSERT_EQ(verify_file(&fs, proj_ino, "main.c", source, sizeof(source)), BFS_OK);
    TEST_ASSERT_EQ(verify_file(&fs, proj_ino, "utils.c", source, sizeof(source)), BFS_OK);
    TEST_ASSERT_EQ(verify_file(&fs, proj_ino, "header.h", source, 512), BFS_ERR_CORRUPT);
    /* header.h still has 'A' content, not 'B' */

    /* Unmount + remount */
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    /* Everything should survive */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, proj_ino, "main.c", 6, &main_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, proj_ino, "utils.c", 7, &main_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, proj_ino, "header.h", 8, &main_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, proj_ino, "newutils.c", 10, &main_ino, &type), BFS_ERR_NOTFOUND);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: Heavy disk usage — fill, delete, refill ─────────── */

static void test_heavy_usage(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Work", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);


    /* Phase 1: Create 100 files of varying sizes */
    uint8_t data[BLK_SIZE * 4];
    memset(data, 0x42, sizeof(data));
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file_%03d.dat", i);
        uint32_t size = (uint32_t)((i % 4 + 1) * BLK_SIZE); /* 4K-16K */
        write_file(&fs, BFS_ROOT_INO, name, data, size, NULL);
    }
    bfs_fs_sync(&fs);

    /* Phase 2: Delete every other file */
    for (int i = 0; i < 100; i += 2) {
        char name[32];
        snprintf(name, sizeof(name), "file_%03d.dat", i);
        bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)strlen(name));
    }
    bfs_fs_sync(&fs);

    /* Phase 3: Create 50 new files in the gaps */
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "new_%03d.dat", i);
        write_file(&fs, BFS_ROOT_INO, name, data, BLK_SIZE * 2, NULL);
    }
    bfs_fs_sync(&fs);

    /* Verify: 50 old files + 50 new files = 100 total */
    
    rw_scan_ctx_t ctx = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, rw_count_cb, &ctx);
    TEST_ASSERT_EQ(ctx.count, 100);

    /* Verify odd-numbered old files still readable */
    for (int i = 1; i < 100; i += 2) {
        char name[32];
        snprintf(name, sizeof(name), "file_%03d.dat", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name,
                       (uint8_t)strlen(name), &ino, &type), BFS_OK);
    }

    /* Unmount + remount + verify */
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    ctx.count = 0;
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, rw_count_cb, &ctx);
    TEST_ASSERT_EQ(ctx.count, 100);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: Simulate Workbench copy (large recursive copy) ──── */

static void test_recursive_copy(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Dest", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Simulate copying a directory tree: SYS/Libs/ with 30 libraries */
    uint32_t sys_ino, libs_ino;
    bfs_fs_mkdir(&fs, BFS_ROOT_INO, "SYS", 3, &sys_ino);
    bfs_fs_mkdir(&fs, sys_ino, "Libs", 4, &libs_ino);

    uint8_t lib_content[BLK_SIZE * 5]; /* 20KB per library */
    for (int i = 0; i < 30; i++) {
        char name[64];
        snprintf(name, sizeof(name), "library_%02d.library", i);
        memset(lib_content, (uint8_t)(i + 1), sizeof(lib_content));
        write_file(&fs, libs_ino, name, lib_content, sizeof(lib_content), NULL);
    }

    /* Simulate copying SYS/Devs/ with device drivers */
    uint32_t devs_ino;
    bfs_fs_mkdir(&fs, sys_ino, "Devs", 4, &devs_ino);
    for (int i = 0; i < 10; i++) {
        char name[64];
        snprintf(name, sizeof(name), "device_%02d.device", i);
        memset(lib_content, (uint8_t)(i + 100), BLK_SIZE * 2);
        write_file(&fs, devs_ino, name, lib_content, BLK_SIZE * 2, NULL);
    }

    bfs_fs_sync(&fs);
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount and verify all 40 files */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    for (int i = 0; i < 30; i++) {
        char name[64];
        snprintf(name, sizeof(name), "library_%02d.library", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, libs_ino, name,
                       (uint8_t)strlen(name), &ino, &type), BFS_OK);
    }
    for (int i = 0; i < 10; i++) {
        char name[64];
        snprintf(name, sizeof(name), "device_%02d.device", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, devs_ino, name,
                       (uint8_t)strlen(name), &ino, &type), BFS_OK);
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Real-World Integration")
    TEST_RUN(test_install_program);
    TEST_RUN(test_daily_use);
    TEST_RUN(test_heavy_usage);
    TEST_RUN(test_recursive_copy);
TEST_SUITE_END()
