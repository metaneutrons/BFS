/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS B+tree benchmark — measures insert, lookup, and scan performance.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bfs_fs.h"
#include "block_device_emu.h"

#define NUM_ENTRIES 10000
#define BLOCK_SIZE  4096
#define BLK_COUNT   64000
#define BENCH_IMG   "bench_btree.img"

/* ── Timing helpers ──────────────────────────────────────────── */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ── Simulated O(n) linear scan for comparison ───────────────── */

typedef struct { char name[32]; uint32_t val; } linear_entry_t;
static linear_entry_t g_linear[NUM_ENTRIES];

static volatile uint32_t g_sink;

static uint32_t linear_lookup(const char *name) {
    for (int i = 0; i < NUM_ENTRIES; i++) {
        if (strcmp(g_linear[i].name, name) == 0)
            return g_linear[i].val;
    }
    return 0;
}

/* ── Scan callback ───────────────────────────────────────────── */

static int g_scan_count;

static bool scan_cb(const char *name, uint8_t name_len,
                    uint32_t inode_nr, uint32_t entry_type, void *ctx) {
    (void)name; (void)name_len; (void)inode_nr; (void)entry_type; (void)ctx;
    g_scan_count++;
    return true;
}

/* ── Main ────────────────────────────────────────────────────── */

int main(void) {
    unlink(BENCH_IMG);
    bfs_bio_t *bio = bio_emu_create(BENCH_IMG, BLOCK_SIZE, BLK_COUNT);
    if (!bio) { fprintf(stderr, "Failed to create block device\n"); return 1; }

    if (bfs_fs_format(bio, "Bench", 0) != BFS_OK) { return 1; }

    bfs_fs_t fs;
    if (bfs_fs_mount(&fs, bio) != BFS_OK) { return 1; }

    /* Generate filenames */
    char names[NUM_ENTRIES][32];
    for (int i = 0; i < NUM_ENTRIES; i++)
        snprintf(names[i], sizeof(names[i]), "file%05d", i);

    printf("=== BFS B+tree Benchmark (4K blocks, 264-byte keys) ===\n");

    /* ── Insert ──────────────────────────────────────────────── */
    double t0 = now_ms();
    for (int i = 0; i < NUM_ENTRIES; i++) {
        uint8_t len = (uint8_t)strlen(names[i]);
        bfs_err_t err = bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO,
                                         names[i], len, (uint32_t)(i + 100), 1);
        if (err != BFS_OK) {
            fprintf(stderr, "Insert failed at %d: %d\n", i, err);
            return 1;
        }
    }
    double t_insert = now_ms() - t0;
    printf("Insert %d entries:  %.0f ms (%d ops/sec)\n",
           NUM_ENTRIES, t_insert, (int)(NUM_ENTRIES / (t_insert / 1000.0)));

    /* ── Random lookup ───────────────────────────────────────── */
    int *order = malloc(NUM_ENTRIES * sizeof(int));
    for (int i = 0; i < NUM_ENTRIES; i++) order[i] = i;
    srand(42);
    for (int i = NUM_ENTRIES - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }

    t0 = now_ms();
    for (int i = 0; i < NUM_ENTRIES; i++) {
        uint32_t ino, type;
        uint8_t len = (uint8_t)strlen(names[order[i]]);
        bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO,
                        names[order[i]], len, &ino, &type);
    }
    double t_lookup = now_ms() - t0;
    printf("Lookup %d random:   %.0f ms (%d ops/sec)\n",
           NUM_ENTRIES, t_lookup, (int)(NUM_ENTRIES / (t_lookup / 1000.0)));

    /* ── Full scan ───────────────────────────────────────────── */
    g_scan_count = 0;
    t0 = now_ms();
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, scan_cb, NULL);
    double t_scan = now_ms() - t0;
    printf("Scan %d entries:    %.0f ms\n", g_scan_count, t_scan);

    /* ── Simulated O(n) linear lookup ────────────────────────── */
    for (int i = 0; i < NUM_ENTRIES; i++) {
        strcpy(g_linear[i].name, names[i]);
        g_linear[i].val = (uint32_t)(i + 100);
    }

    t0 = now_ms();
    for (int i = 0; i < NUM_ENTRIES; i++) {
        g_sink = linear_lookup(names[order[i]]);
    }
    double t_linear = now_ms() - t0;
    printf("Simulated O(n) scan:   %.0f ms (%.1fx slower)\n",
           t_linear, t_linear > 0 && t_lookup > 0 ? t_linear / t_lookup : 0.0);

    bfs_fs_unmount(&fs);
    free(order);
    unlink(BENCH_IMG);
    return 0;
}
