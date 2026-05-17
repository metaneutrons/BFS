/*
 * BFS — Property-based model checking
 *
 * Runs random operations and verifies invariants AFTER EVERY operation.
 * This catches transient inconsistencies that end-of-test checks miss.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_dir.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_model.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096
#define MAX_FILES 60
#define NAME_FMT "f%03d"

/* ── PRNG ──────────────────────────────────────────────────── */

static uint32_t rng_state;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* ── Model: ground truth of what SHOULD be on disk ─────────── */

typedef struct {
    bool exists;
    uint32_t ino;
    uint32_t size; /* bytes written */
} file_model_t;

static file_model_t model[MAX_FILES];

/* ── Invariant checks ──────────────────────────────────────── */

typedef struct { uint32_t count; } inv_ctx_t;
static bool inv_count_cb(const char *n, uint8_t l, uint32_t i, uint32_t t, void *c) {
    (void)n;(void)l;(void)i;(void)t;
    ((inv_ctx_t*)c)->count++; return true;
}

/*
 * Invariant 1: Every file in our model that exists is findable by lookup.
 * Invariant 2: Every file found by scan has a valid inode.
 * Invariant 3: Scan count matches model count.
 * Returns 0 if all invariants hold, >0 = number of violations.
 */
static int check_invariants(bfs_fs_t *fs, const char *context)
{
    int violations = 0;

    /* Count expected files */
    uint32_t expected = 0;
    for (int i = 0; i < MAX_FILES; i++)
        if (model[i].exists) expected++;

    /* Invariant 1: Every model-existing file is findable by lookup */
    for (int i = 0; i < MAX_FILES; i++) {
        if (!model[i].exists) continue;
        char name[16];
        snprintf(name, sizeof(name), NAME_FMT, i);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO,
                                         name, (uint8_t)strlen(name), &ino, &type);
        if (err != BFS_OK) {
            fprintf(stderr, "  INVARIANT 1 VIOLATED [%s]: %s exists in model but lookup failed (err=%d)\n",
                    context, name, err);
            violations++;
        }
    }

    /* Invariant 2: Every file found by lookup has a readable inode */
    for (int i = 0; i < MAX_FILES; i++) {
        if (!model[i].exists) continue;
        bfs_inode_t inode;
        bfs_err_t err = bfs_inode_read(&fs->inode_tree, model[i].ino, &inode);
        if (err != BFS_OK) {
            char name[16];
            snprintf(name, sizeof(name), NAME_FMT, i);
            fprintf(stderr, "  INVARIANT 2 VIOLATED [%s]: %s (ino=%u) has no readable inode (err=%d)\n",
                    context, name, model[i].ino, err);
            violations++;
        }
    }

    /* Invariant 3: Scan count matches model count */
    inv_ctx_t ctx = {0};
    bfs_dir_scan(&fs->dir_tree, BFS_ROOT_INO, inv_count_cb, &ctx);
    if (ctx.count != expected) {
        fprintf(stderr, "  INVARIANT 3 VIOLATED [%s]: scan returned %u entries, model has %u\n",
                context, ctx.count, expected);
        violations++;
    }

    /* Invariant 4: No model-deleted file is findable */
    for (int i = 0; i < MAX_FILES; i++) {
        if (model[i].exists) continue;
        char name[16];
        snprintf(name, sizeof(name), NAME_FMT, i);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO,
                                         name, (uint8_t)strlen(name), &ino, &type);
        if (err == BFS_OK) {
            fprintf(stderr, "  INVARIANT 4 VIOLATED [%s]: %s deleted in model but still found (ino=%u)\n",
                    context, name, ino);
            violations++;
        }
    }

    return violations;
}

/* ── Property test: random ops with invariant checking ─────── */

static void test_model_check_seed(uint32_t seed, int num_ops)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Model", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    memset(model, 0, sizeof(model));
    rng_state = seed;

    char desc[64];
    uint8_t write_buf[BLK_SIZE];

    for (int op = 0; op < num_ops; op++) {
        uint32_t action = rng() % 5;
        int idx = (int)(rng() % MAX_FILES);
        char name[16];
        snprintf(name, sizeof(name), NAME_FMT, idx);
        uint8_t nlen = (uint8_t)strlen(name);

        switch (action) {
        case 0: /* CREATE */
            if (!model[idx].exists) {
                uint32_t ino;
                bfs_err_t err = bfs_fs_create_file(&fs, BFS_ROOT_INO, name, nlen, &ino);
                if (err == BFS_OK) {
                    model[idx].exists = true;
                    model[idx].ino = ino;
                    model[idx].size = 0;
                }
            }
            break;

        case 1: /* DELETE */
            if (model[idx].exists) {
                bfs_err_t err = bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, nlen);
                if (err == BFS_OK) {
                    model[idx].exists = false;
                    model[idx].ino = 0;
                    model[idx].size = 0;
                }
            }
            break;

        case 2: /* WRITE */
            if (model[idx].exists) {
                bfs_file_t f;
                if (bfs_file_open(&f, &fs, model[idx].ino) == BFS_OK) {
                    uint32_t write_size = (rng() % 4 + 1) * 1024;
                    memset(write_buf, (uint8_t)(idx + op), write_size > BLK_SIZE ? BLK_SIZE : write_size);
                    int32_t written = bfs_file_write(&f, write_buf, write_size > BLK_SIZE ? BLK_SIZE : write_size);
                    if (written > 0)
                        model[idx].size = (uint32_t)f.size;
                }
            }
            break;

        case 3: /* READ (verify no crash) */
            if (model[idx].exists && model[idx].size > 0) {
                bfs_file_t f;
                if (bfs_file_open(&f, &fs, model[idx].ino) == BFS_OK) {
                    uint8_t tmp[1024];
                    bfs_file_read(&f, tmp, sizeof(tmp));
                }
            }
            break;

        case 4: /* SYNC */
            bfs_fs_sync(&fs);
            break;
        }

        /* CHECK INVARIANTS AFTER EVERY OPERATION */
        snprintf(desc, sizeof(desc), "seed=%u op=%d action=%u idx=%d", seed, op, action, idx);
        int v = check_invariants(&fs, desc);
        if (v > 0) {
            fprintf(stderr, "PROPERTY VIOLATION at %s\n", desc);
            TEST_ASSERT_EQ(v, 0);
            break;
        }
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Invariant 5: Persistence — state survives remount ─────── */

static void test_model_persistence(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Persist", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    memset(model, 0, sizeof(model));
    rng_state = 77777;

    /* Do 200 random ops */
    uint8_t write_buf[BLK_SIZE];
    for (int op = 0; op < 200; op++) {
        int idx = (int)(rng() % MAX_FILES);
        char name[16];
        snprintf(name, sizeof(name), NAME_FMT, idx);
        uint8_t nlen = (uint8_t)strlen(name);

        if (!model[idx].exists) {
            uint32_t ino;
            if (bfs_fs_create_file(&fs, BFS_ROOT_INO, name, nlen, &ino) == BFS_OK) {
                model[idx].exists = true;
                model[idx].ino = ino;
                bfs_file_t f;
                if (bfs_file_open(&f, &fs, ino) == BFS_OK) {
                    memset(write_buf, (uint8_t)idx, 100);
                    bfs_file_write(&f, write_buf, 100);
                    model[idx].size = 100;
                }
            }
        } else {
            bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, nlen);
            model[idx].exists = false;
        }
    }

    /* Sync + unmount + remount */
    bfs_fs_sync(&fs);
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    /* Invariant 5: All model state matches after remount */
    int v = check_invariants(&fs, "after remount");
    TEST_ASSERT_EQ(v, 0);

    /* Also verify file sizes */
    for (int i = 0; i < MAX_FILES; i++) {
        if (!model[i].exists) continue;
        bfs_inode_t inode;
        if (bfs_inode_read(&fs.inode_tree, model[i].ino, &inode) == BFS_OK) {
            uint64_t disk_size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
            TEST_ASSERT_EQ(disk_size, model[i].size);
        }
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test entries ──────────────────────────────────────────── */

static void test_model_seed_1(void) { test_model_check_seed(12345, 500); }
static void test_model_seed_2(void) { test_model_check_seed(99999, 500); }
static void test_model_seed_3(void) { test_model_check_seed(31415, 500); }
static void test_model_seed_4(void) { test_model_check_seed(27182, 500); }
static void test_model_seed_5(void) { test_model_check_seed(65537, 500); }

TEST_SUITE_BEGIN("Property-Based Model Check")
    TEST_RUN(test_model_seed_1);
    TEST_RUN(test_model_seed_2);
    TEST_RUN(test_model_seed_3);
    TEST_RUN(test_model_seed_4);
    TEST_RUN(test_model_seed_5);
    TEST_RUN(test_model_persistence);
TEST_SUITE_END()
