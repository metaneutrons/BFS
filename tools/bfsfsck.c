/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Filesystem check and repair tool (read-only check, mount for repair)
 *
 * Usage: bfsfsck <disk_image> [--fix]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bfs_fs.h"
#include "bfs_superblock.h"
#include "bfs_btree.h"
#include "bfs_dir.h"
#include "bfs_inode.h"
#include "block_device_emu.h"

static uint8_t *block_map;
static uint32_t block_count;
static uint32_t errors, warnings;

#define ERR(fmt, ...) do { errors++; fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__); } while(0)
#define WARN(fmt, ...) do { warnings++; fprintf(stderr, "WARN:  " fmt "\n", ##__VA_ARGS__); } while(0)

static void mark(uint32_t blk, uint8_t type, const char *owner)
{
    if (blk >= block_count) { ERR("block %u out of range (%s)", blk, owner); return; }
    if (block_map[blk] && block_map[blk] != type)
        ERR("block %u double-referenced (%s)", blk, owner);
    block_map[blk] = type;
}

static void node_cb(bfs_blk_t blk, void *ctx) { mark(blk, 2, (const char *)ctx); }

static bool free_cb(const void *key, const void *val, void *ctx)
{
    (void)ctx;
    uint32_t blk = bfs_be32(*(const uint32_t *)key);
    uint32_t len = bfs_be32(*(const uint32_t *)val);
    for (uint32_t i = 0; i < len; i++) mark(blk + i, 1, "free");
    return true;
}

static bool dir_cb(const char *name, uint8_t nlen, uint32_t ino, uint32_t type, void *ctx)
{
    (void)name; (void)nlen; (void)type;
    bfs_fs_t *fs = (bfs_fs_t *)ctx;
    if (nlen == 2 && name[0] == '.' && name[1] == '.') return true;
    bfs_inode_t inode;
    if (bfs_inode_read(&fs->inode_tree, ino, &inode) != BFS_OK)
        ERR("orphan dir entry '%.*s' ino=%u", nlen, name, ino);
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "Usage: bfsfsck <image> [--fix]\n"); return 1; }
    int fix = (argc > 2 && strcmp(argv[2], "--fix") == 0);

    bfs_bio_t *bio = bio_emu_open(argv[1], 4096);
    if (!bio) { fprintf(stderr, "Cannot open %s\n", argv[1]); return 1; }

    printf("=== BFS Filesystem Check ===\n");

    /* Mount (needed for tree access) */
    bfs_fs_t fs;
    if (bfs_fs_mount(&fs, bio) != BFS_OK) { fprintf(stderr, "Mount failed\n"); return 1; }

    block_count = bio->block_count;
    block_map = calloc(block_count, 1);
    mark(0, 2, "superblock");

    /* Mark emergency pool */
    bfs_superblock_t sb;
    bfs_sb_read(bio, &sb);
    printf("  Volume: %s  Blocks: %u  Free: %u\n", sb.volname, block_count, fs.freespace.total_free);
    uint32_t ec = bfs_be32(sb.emergency_count);
    for (uint32_t i = 0; i < ec; i++) mark(bfs_be32(sb.emergency_pool[i]), 2, "emergency");

    /* Mark reserve pool */
    for (uint32_t i = 0; i < fs.freespace.reserve_count; i++)
        mark(fs.freespace.reserve[i], 2, "reserve");

    /* Walk all tree nodes */
    bfs_btree_walk_nodes(&fs.freespace.tree, node_cb, "free-tree");
    bfs_btree_walk_nodes(&fs.dir_tree.tree, node_cb, "dir-tree");
    bfs_btree_walk_nodes(&fs.inode_tree, node_cb, "inode-tree");

    /* Mark free extents */
    bfs_btree_scan(&fs.freespace.tree, NULL, free_cb, NULL);

    /* Cross-check directory entries */
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, dir_cb, &fs);

    /* Count leaked blocks */
    uint32_t leaked = 0;
    for (uint32_t i = 1; i < block_count; i++)
        if (!block_map[i]) leaked++;

    if (leaked) {
        WARN("%u leaked blocks", leaked);
        if (fix) {
            printf("Recovering...\n");
            for (uint32_t i = 1; i < block_count; i++)
                if (!block_map[i]) bfs_freespace_free(&fs.freespace, i, 1);
            bfs_fs_sync(&fs);
            printf("Recovered %u blocks.\n", leaked);
        }
    }

    printf("\n  Errors: %u  Warnings: %u\n", errors, warnings);
    printf("  %s\n", errors ? "ERRORS FOUND" : (warnings ? "Minor issues" : "CLEAN"));

    free(block_map);
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    return errors ? 2 : (warnings ? 1 : 0);
}
