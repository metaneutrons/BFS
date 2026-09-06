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
#include "bfs_extent.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"

static uint8_t *block_map;
static uint8_t *reference_map;
static uint32_t block_count;
static uint32_t errors, warnings;
static bool reference_saturated;

static const bfs_btree_ops_t snapshot_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_snapshot_record_t),
};

#define ERR(fmt, ...) do { errors++; fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__); } while(0)
#define WARN(fmt, ...) do { warnings++; fprintf(stderr, "WARN:  " fmt "\n", ##__VA_ARGS__); } while(0)

static void mark(uint32_t blk, uint8_t type, const char *owner)
{
    if (blk >= block_count) { ERR("block %u out of range (%s)", blk, owner); return; }
    uint8_t old_type = block_map[blk] & 0x7fu;
    if (old_type && old_type != type)
        ERR("block %u double-referenced (%s)", blk, owner);
    block_map[blk] = (block_map[blk] & 0x80u) | type;
}

static void node_cb(bfs_blk_t blk, void *ctx) { mark(blk, 2, (const char *)ctx); }

static void mark_reference(uint32_t blk, uint8_t type, const char *owner)
{
    mark(blk, type, owner);
    if (blk >= block_count) return;
    if (reference_map[blk] == UINT8_MAX) {
        reference_saturated = true;
        return;
    }
    reference_map[blk]++;
}

static void reference_node_cb(bfs_blk_t blk, void *ctx)
{
    mark_reference(blk, 2, (const char *)ctx);
}

static void mark_range(uint32_t start, uint32_t count, uint8_t type, const char *owner)
{
    uint64_t end = (uint64_t)start + count;
    if (start >= block_count || end > block_count) {
        ERR("block range %u+%u out of range (%s)", start, count, owner);
        if (start >= block_count) return;
        count = block_count - start;
    }
    for (uint32_t i = 0; i < count; i++)
        mark(start + i, type, owner);
}

static void mark_reference_range(uint32_t start, uint32_t count, uint8_t type,
                                 const char *owner)
{
    uint64_t end = (uint64_t)start + count;
    if (start >= block_count || end > block_count) {
        ERR("block range %u+%u out of range (%s)", start, count, owner);
        if (start >= block_count) return;
        count = block_count - start;
    }
    for (uint32_t i = 0; i < count; i++)
        mark_reference(start + i, type, owner);
}

static bool free_cb(const void *key, const void *val, void *ctx)
{
    (void)ctx;
    uint32_t blk = bfs_load_be32(key);
    uint32_t len = bfs_load_be32(val);
    mark_range(blk, len, 1, "free");
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

typedef struct {
    bfs_fs_t *fs;
    const char *owner;
} mark_ctx_t;

static bool extent_data_cb(const void *key, const void *val, void *ctx)
{
    (void)key;
    mark_ctx_t *mc = (mark_ctx_t *)ctx;
    const bfs_extent_val_t *ev = (const bfs_extent_val_t *)val;
    uint32_t disk = bfs_be32(ev->disk_block);
    uint32_t len = bfs_be32(ev->length);
    mark_reference_range(disk, len, 3, mc->owner);
    return true;
}

static void mark_extent_tree(bfs_fs_t *fs, bfs_blk_t root, const char *owner)
{
    if (root == BFS_BLK_NULL) return;

    bfs_extent_tree_t et;
    if (bfs_extent_init(&et, fs->bio, &fs->freespace, root, bfs_txn_id(&fs->txn)) != BFS_OK) {
        ERR("cannot read extent tree root %u (%s)", root, owner);
        return;
    }
    if (bfs_btree_walk_nodes(&et.tree, reference_node_cb,
                             "extent-tree") != BFS_OK) {
        ERR("cannot walk extent tree root %u (%s)", root, owner);
        return;
    }
    mark_ctx_t mc = { .fs = fs, .owner = owner };
    if (bfs_btree_scan(&et.tree, NULL, extent_data_cb, &mc) != BFS_OK)
        ERR("cannot scan extent tree root %u (%s)", root, owner);
}

static bool inode_extent_cb(const void *key, const void *val, void *ctx)
{
    (void)key;
    mark_ctx_t *mc = (mark_ctx_t *)ctx;
    const bfs_inode_t *inode = (const bfs_inode_t *)val;
    mark_extent_tree(mc->fs, bfs_be32(inode->extent_root), mc->owner);
    return true;
}

static void mark_inode_payloads(bfs_fs_t *fs, bfs_btree_t *inode_tree, const char *owner)
{
    mark_ctx_t mc = { .fs = fs, .owner = owner };
    if (bfs_btree_scan(inode_tree, NULL, inode_extent_cb, &mc) != BFS_OK)
        ERR("cannot scan inode tree payloads (%s)", owner);
}

static bool snapshot_mark_cb(uint32_t id, const bfs_snapshot_record_t *rec, void *ctx)
{
    bfs_fs_t *fs = (bfs_fs_t *)ctx;
    uint64_t snap_txn = ((uint64_t)bfs_be32(rec->txn_id_hi) << 32) |
                        bfs_be32(rec->txn_id_lo);

    bfs_dir_tree_t dir_tree;
    if (bfs_dir_init(&dir_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                     bfs_be32(rec->dir_tree_root), snap_txn) == BFS_OK) {
        if (bfs_btree_walk_nodes(&dir_tree.tree, reference_node_cb,
                                 "snapshot-dir-tree") != BFS_OK)
            ERR("cannot walk snapshot %u directory tree", id);
    } else {
        ERR("cannot read snapshot %u directory tree", id);
    }

    bfs_btree_t inode_tree;
    if (bfs_inode_init(&inode_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                       bfs_be32(rec->inode_tree_root), snap_txn) == BFS_OK) {
        if (bfs_btree_walk_nodes(&inode_tree, reference_node_cb,
                                 "snapshot-inode-tree") != BFS_OK)
            ERR("cannot walk snapshot %u inode tree", id);
        else
            mark_inode_payloads(fs, &inode_tree, "snapshot-data");
    } else {
        ERR("cannot read snapshot %u inode tree", id);
    }

    return true;
}

static bfs_bio_t *open_detected_bio(const char *path)
{
    for (uint32_t size = BFS_MIN_BLOCK_SIZE; size <= BFS_MAX_BLOCK_SIZE; size *= 2) {
        bfs_bio_t *probe = bio_emu_open(path, size);
        if (!probe) continue;
        bfs_superblock_t sb;
        if (bfs_sb_read(probe, &sb) == BFS_OK) return probe;
        bfs_bio_close(probe);
    }
    return NULL;
}

typedef struct {
    bfs_bio_t base;
    bfs_bio_t *inner;
} inspect_bio_t;

static bfs_err_t inspect_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    return bfs_bio_read(((inspect_bio_t *)bio)->inner, blk, buf);
}

static bfs_err_t inspect_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    (void)bio; (void)blk; (void)buf;
    return BFS_ERR_IO;
}

static bfs_err_t inspect_sync(bfs_bio_t *bio)
{
    (void)bio;
    return BFS_OK;
}

static const bfs_bio_ops_t inspect_ops = {
    .read_block = inspect_read, .write_block = inspect_write, .sync = inspect_sync,
};

static bool refcount_check_cb(const void *key, const void *val, void *ctx)
{
    (void)ctx;
    bfs_blk_t blk = bfs_load_be32(key);
    uint32_t stored = bfs_load_be32(val);
    if (blk == BFS_BLK_NULL || blk >= block_count) {
        ERR("refcount entry block %u is out of range", blk);
        return true;
    }
    if (stored < 2) {
        ERR("refcount entry block %u stores invalid count %u", blk, stored);
    } else if (reference_map[blk] <= 1) {
        ERR("refcount entry block %u has no matching shared references", blk);
    } else if (reference_map[blk] != UINT8_MAX &&
               stored != reference_map[blk]) {
        ERR("refcount mismatch for block %u: stored=%u observed=%u",
            blk, stored, reference_map[blk]);
    } else if (reference_map[blk] == UINT8_MAX && stored < UINT8_MAX) {
        ERR("refcount mismatch for block %u: stored=%u observed>=255",
            blk, stored);
    }
    block_map[blk] |= 0x80u;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--fix") != 0)) {
        fprintf(stderr, "Usage: bfsfsck <image> [--fix]\n");
        return 2;
    }
    int fix = argc == 3;

    bfs_bio_t *bio = open_detected_bio(argv[1]);
    if (!bio) { fprintf(stderr, "Cannot open %s\n", argv[1]); return 1; }

    printf("=== BFS Filesystem Check ===\n");

    inspect_bio_t inspect = { .base = *bio, .inner = bio };
    inspect.base.ops = &inspect_ops;
    /* A read-only check must not commit or resume interrupted deletions. The
     * write guard makes such a mount fail, leaving explicit repair to --fix. */
    bfs_fs_t fs;
    if (bfs_fs_mount(&fs, fix ? bio : &inspect.base) != BFS_OK) {
        fprintf(stderr, "Mount failed\n");
        bfs_bio_close(bio);
        return 1;
    }

    block_count = bio->block_count;
    block_map = calloc(block_count, 1);
    reference_map = calloc(block_count, 1);
    if (!block_map || !reference_map) {
        fprintf(stderr, "Cannot allocate block maps for %u blocks\n", block_count);
        free(reference_map);
        free(block_map);
        bfs_fs_abandon(&fs);
        bfs_bio_close(bio);
        return 1;
    }
    /* Mark emergency pool */
    bfs_superblock_t sb;
    if (bfs_sb_read(bio, &sb) != BFS_OK) {
        ERR("cannot reread the superblock");
        sb = fs.txn.sb;
    }
    printf("  Volume: %s  Blocks: %u  Free: %u\n", sb.volname, block_count, fs.freespace.total_free);

    uint32_t data_start = bfs_data_start_block(bio->block_size);
    mark_range(0, data_start, 2, "reserved/superblock");
    uint64_t backup_off = ((uint64_t)bfs_be32(sb.sb_backup_offset_hi) << 32) |
                          bfs_be32(sb.sb_backup_offset_lo);
    uint64_t backup_block = backup_off / bio->block_size;
    if (backup_block >= block_count)
        ERR("backup superblock offset is out of range");
    else
        mark((uint32_t)backup_block, 2, "backup-superblock");

    uint32_t ec = bfs_be32(sb.emergency_count);
    for (uint32_t i = 0; i < ec; i++) mark(bfs_be32(sb.emergency_pool[i]), 2, "emergency");

    /* Mark reserve pool */
    for (uint32_t i = 0; i < fs.freespace.reserve_count; i++)
        mark(fs.freespace.reserve[i], 2, "reserve");

    /* Walk all tree nodes */
    if (bfs_btree_walk_nodes(&fs.freespace.tree, node_cb, "free-tree") != BFS_OK)
        ERR("cannot walk free-space tree");
    if (bfs_btree_walk_nodes(&fs.dir_tree.tree, reference_node_cb,
                             "dir-tree") != BFS_OK)
        ERR("cannot walk directory tree");
    if (bfs_btree_walk_nodes(&fs.inode_tree, reference_node_cb,
                             "inode-tree") != BFS_OK)
        ERR("cannot walk inode tree");
    if (fs.has_snapshots &&
        bfs_btree_walk_nodes(&fs.refcount.tree, node_cb, "refcount-tree") != BFS_OK)
        ERR("cannot walk refcount tree");

    bfs_blk_t snap_root = bfs_be32(fs.txn.sb_new.snapshot_tree_root);
    if (snap_root != BFS_BLK_NULL) {
        bfs_btree_t snap_tree;
        if (bfs_btree_init(&snap_tree, fs.bio, bfs_freespace_allocator(&fs.freespace),
                           &snapshot_ops, snap_root, bfs_txn_id(&fs.txn)) == BFS_OK) {
            if (bfs_btree_walk_nodes(&snap_tree, node_cb,
                                     "snapshot-tree") != BFS_OK)
                ERR("cannot walk snapshot tree");
        } else {
            ERR("cannot read snapshot tree");
        }
        if (bfs_snapshot_list(&fs, snapshot_mark_cb, &fs) != BFS_OK)
            ERR("cannot scan snapshot records");
    }

    /* Mark free extents */
    if (bfs_btree_scan(&fs.freespace.tree, NULL, free_cb, NULL) != BFS_OK)
        ERR("cannot scan free-space extents");

    /* Mark file extent trees and data blocks */
    mark_inode_payloads(&fs, &fs.inode_tree, "file-data");

    /* Cross-check directory entries */
    if (bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, dir_cb, &fs) != BFS_OK)
        ERR("cannot scan the root directory");

    if (fs.has_snapshots) {
        if (bfs_btree_scan(&fs.refcount.tree, NULL, refcount_check_cb, NULL) != BFS_OK)
            ERR("cannot scan refcount entries");
        for (uint32_t i = 0; i < block_count; i++) {
            if (reference_map[i] > 1 && !(block_map[i] & 0x80u))
                ERR("shared block %u has no refcount entry (observed=%u)",
                    i, reference_map[i]);
        }
        if (reference_saturated)
            WARN("one or more observed reference counts saturated at 255");
    }

    /* Count leaked blocks */
    uint32_t leaked = 0;
    for (uint32_t i = 0; i < block_count; i++)
        if ((block_map[i] & 0x7fu) == 0) leaked++;

    if (leaked) {
        WARN("%u leaked blocks", leaked);
        if (fix) {
            if (errors) {
                ERR("refusing leak repair after structural scan errors");
            } else {
                uint32_t recovered = 0;
                printf("Recovering...\n");
                for (uint32_t i = 0; i < block_count; i++) {
                    if ((block_map[i] & 0x7fu) == 0) {
                        bfs_err_t err = bfs_freespace_free(&fs.freespace, i, 1);
                        if (err != BFS_OK) {
                            ERR("cannot recover block %u (error %d)", i, err);
                            break;
                        }
                        recovered++;
                    }
                }
                if (errors == 0 && bfs_fs_sync(&fs) != BFS_OK)
                    ERR("cannot commit repaired free-space metadata");
                if (errors == 0)
                    printf("Recovered %u blocks.\n", recovered);
            }
        }
    }

    free(reference_map);
    free(block_map);
    if (fix && errors == 0) {
        if (bfs_fs_unmount(&fs) != BFS_OK)
            ERR("cannot unmount the filesystem cleanly");
    } else {
        bfs_fs_abandon(&fs);
    }
    bfs_bio_close(bio);

    printf("\n  Errors: %u  Warnings: %u\n", errors, warnings);
    printf("  %s\n", errors ? "ERRORS FOUND" : (warnings ? "Minor issues" : "CLEAN"));
    return errors ? 2 : (warnings ? 1 : 0);
}
