/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Filesystem format, mount, sync, unmount
 */

#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_extent.h"
#include <string.h>
#include <stdlib.h>

/* ── Format ────────────────────────────────────────────────── */

bfs_err_t bfs_fs_format(bfs_bio_t *bio, const char *volname, uint32_t options)
{
    uint32_t bs = bio->block_size;
    bfs_blk_t bc = bio->block_count;

    if (bc < BFS_MIN_VOLUME_BLOCKS) return BFS_ERR_INVAL;

    bfs_blk_t data_start = BFS_DATA_OFFSET / bs;
    uint32_t data_blocks = bc - data_start;
    uint64_t backup_off = (uint64_t)bc * bs / 2;

    bfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic       = bfs_be32(BFS_SB_MAGIC);
    sb.version     = bfs_be32(BFS_SB_VERSION);
    sb.block_size  = bfs_be32(bs);
    sb.block_count = bfs_be32(bc);
    sb.txn_id      = bfs_be64(1);
    sb.free_blocks = bfs_be32(data_blocks);
    sb.options     = bfs_be32(options);
    sb.next_ino    = bfs_be32(BFS_ROOT_INO + 1);
    sb.sb_backup_offset_lo = bfs_be32((uint32_t)backup_off);
    sb.sb_backup_offset_hi = bfs_be32((uint32_t)(backup_off >> 32));

    size_t nlen = strlen(volname);
    if (nlen > BFS_VOLNAME_MAX - 1) nlen = BFS_VOLNAME_MAX - 1;
    memcpy(sb.volname, volname, nlen);
    sb.crc32 = bfs_be32(bfs_sb_compute_crc(&sb));

    bfs_err_t err = bfs_sb_write_raw(bio, BFS_SB_OFFSET_A, &sb);
    if (err != BFS_OK) return err;
    err = bfs_sb_write_raw(bio, backup_off, &sb);
    if (err != BFS_OK) return err;

    bfs_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.bio = bio;
    fs.txn.bio = bio;
    fs.txn.sb = sb;
    fs.txn.sb_new = sb;
    fs.txn.sb_new.txn_id = bfs_be64(2);
    fs.txn.active = true;
    fs.live_txn_id = 2;

    bfs_freespace_init(&fs.freespace, bio, BFS_BLK_NULL, fs.live_txn_id);
    fs.freespace.tree.txn_id_ptr = &fs.live_txn_id;
    fs.freespace.tree.fs_ctx = &fs;

    uint32_t epool_count = BFS_EMERGENCY_POOL_SIZE;
    if (epool_count > data_blocks / 4) epool_count = data_blocks / 4;
    for (uint32_t i = 0; i < epool_count; i++)
        sb.emergency_pool[i] = bfs_be32(data_start + i);
    sb.emergency_count = bfs_be32(epool_count);

    bfs_blk_t backup_blk = (bfs_blk_t)(backup_off / bs);
    if (backup_blk >= data_start + epool_count && backup_blk < bc) {
        bfs_freespace_add(&fs.freespace, data_start + epool_count, backup_blk - (data_start + epool_count));
        bfs_freespace_add(&fs.freespace, backup_blk + 1, bc - (backup_blk + 1));
        fs.freespace.total_free = data_blocks - epool_count - 1;
    } else {
        bfs_freespace_add(&fs.freespace, data_start + epool_count, data_blocks - epool_count);
        fs.freespace.total_free = data_blocks - epool_count;
    }
    bfs_freespace_refill_reserve(&fs.freespace);

    uint32_t greserve = bc / 20;
    if (greserve < 64) greserve = 64;
    if (greserve > 512) greserve = 512;
    sb.global_reserve = bfs_be32(greserve);

    bfs_dir_init(&fs.dir_tree, bio, bfs_freespace_allocator(&fs.freespace),
                  BFS_BLK_NULL, fs.live_txn_id);
    fs.dir_tree.tree.txn_id_ptr = &fs.live_txn_id;
    fs.dir_tree.tree.fs_ctx = &fs;
    bfs_dir_insert(&fs.dir_tree, 0, "/", 1, BFS_ROOT_INO, BFS_INODE_DIR);

    bfs_inode_init(&fs.inode_tree, bio, bfs_freespace_allocator(&fs.freespace),
                    BFS_BLK_NULL, fs.live_txn_id);
    fs.inode_tree.txn_id_ptr = &fs.live_txn_id;
    fs.inode_tree.fs_ctx = &fs;
    bfs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.inode_nr = bfs_be32(BFS_ROOT_INO);
    root_inode.type = bfs_be32(BFS_INODE_DIR);
    bfs_inode_write(&fs.inode_tree, BFS_ROOT_INO, &root_inode);

    fs.txn.sb_new.emergency_count = sb.emergency_count;
    memcpy(fs.txn.sb_new.emergency_pool, sb.emergency_pool, sizeof(sb.emergency_pool));
    fs.txn.sb_new.global_reserve = sb.global_reserve;
    bfs_txn_set_dir_root(&fs.txn, fs.dir_tree.tree.root);
    bfs_txn_set_free_root(&fs.txn, fs.freespace.tree.root);
    bfs_txn_set_free_blocks(&fs.txn, fs.freespace.total_free);
    bfs_txn_set_inode_root(&fs.txn, fs.inode_tree.root);

    bfs_txn_commit(&fs.txn);
    return bfs_bio_sync(bio);
}

/* ── Mount ─────────────────────────────────────────────────── */

bfs_err_t bfs_fs_mount(bfs_fs_t *fs, bfs_bio_t *bio)
{
    memset(fs, 0, sizeof(*fs));
    fs->bio = bio;
    bfs_err_t err = bfs_txn_begin(&fs->txn, bio);
    if (err != BFS_OK) return err;

    bfs_superblock_t *sb = &fs->txn.sb;
    fs->live_txn_id = bfs_txn_id(&fs->txn);

    bfs_blk_t free_root = bfs_be32(sb->free_tree_root);
    err = bfs_freespace_init(&fs->freespace, bio, free_root, fs->live_txn_id);
    if (err != BFS_OK) return BFS_ERR_CORRUPT;
    fs->freespace.tree.txn_id_ptr = &fs->live_txn_id;
    fs->freespace.tree.fs_ctx = fs;
    fs->freespace.total_free = bfs_be32(sb->free_blocks);
    fs->freespace.global_reserve = bfs_be32(sb->global_reserve);
    fs->freespace.emergency_pool = fs->txn.sb_new.emergency_pool;
    fs->freespace.emergency_count = &fs->txn.sb_new.emergency_count;
    bfs_freespace_refill_reserve(&fs->freespace);

    bfs_blk_t dir_root = bfs_be32(sb->dir_tree_root);
    err = bfs_dir_init(&fs->dir_tree, bio, bfs_freespace_allocator(&fs->freespace),
                  dir_root, fs->live_txn_id);
    if (err != BFS_OK) return BFS_ERR_CORRUPT;
    fs->dir_tree.tree.txn_id_ptr = &fs->live_txn_id;
    fs->dir_tree.tree.fs_ctx = fs;

    bfs_blk_t inode_root = bfs_be32(sb->inode_tree_root);
    err = bfs_inode_init(&fs->inode_tree, bio, bfs_freespace_allocator(&fs->freespace),
                    inode_root, fs->live_txn_id);
    if (err != BFS_OK) return BFS_ERR_CORRUPT;
    fs->inode_tree.txn_id_ptr = &fs->live_txn_id;
    fs->inode_tree.fs_ctx = fs;

    bfs_blk_t rc_root = bfs_be32(sb->refcount_tree_root);
    fs->has_snapshots = (rc_root != BFS_BLK_NULL && rc_root != 0);
    if (fs->has_snapshots) {
        err = bfs_refcount_init(&fs->refcount, bio, bfs_freespace_allocator(&fs->freespace),
                                 rc_root, fs->live_txn_id);
        if (err != BFS_OK) return BFS_ERR_CORRUPT;
        fs->refcount.tree.txn_id_ptr = &fs->live_txn_id;
        fs->refcount.tree.fs_ctx = fs;
    }

    fs->mounted = true;
    fs->next_ino = bfs_be32(sb->next_ino);
    fs->options = bfs_be32(sb->options);
    fs->data_checksums = (fs->options & BFS_OPT_DATA_CHECKSUMS) != 0;
    fs->scratch = malloc(bio->block_size);
    return fs->scratch ? BFS_OK : BFS_ERR_NOMEM;
}

/* ── Inode allocation ──────────────────────────────────────── */

uint32_t bfs_fs_alloc_ino(bfs_fs_t *fs)
{
    return fs->next_ino++;
}

/* ── Directory operations ──────────────────────────────────── */

bfs_err_t bfs_fs_create_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    uint32_t ino = bfs_fs_alloc_ino(fs);
    bfs_err_t err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_FILE);
    if (err != BFS_OK) return err;
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_FILE);
    inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs->inode_tree, ino, &inode);
    if (ino_out) *ino_out = ino;
    return err;
}

bfs_err_t bfs_fs_mkdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    uint32_t ino = bfs_fs_alloc_ino(fs);
    bfs_err_t err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_DIR);
    if (err != BFS_OK) return err;
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_DIR);
    err = bfs_inode_write(&fs->inode_tree, ino, &inode);
    bfs_dir_insert(&fs->dir_tree, ino, "..", 2, parent_ino, BFS_INODE_DIR);
    if (ino_out) *ino_out = ino;
    return err;
}

bfs_err_t bfs_fs_delete_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    uint32_t ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, parent_ino, name, name_len, &ino, &type);
    if (err != BFS_OK) return err;
    bfs_inode_t inode;
    if (bfs_inode_read(&fs->inode_tree, ino, &inode) == BFS_OK) {
        uint32_t lc = bfs_be32(inode.link_count);
        if (lc > 1) {
            inode.link_count = bfs_be32(lc - 1);
            bfs_inode_write(&fs->inode_tree, ino, &inode);
        } else {
            bfs_blk_t ext_root = bfs_be32(inode.extent_root);
            if (ext_root != 0) {
                bfs_extent_tree_t et;
                if (bfs_extent_init(&et, fs->bio, &fs->freespace, ext_root, fs->live_txn_id) == BFS_OK) {
                    et.tree.txn_id_ptr = &fs->live_txn_id;
                    et.tree.fs_ctx = fs;
                    while (bfs_extent_truncate(&et, 0) == BFS_ERR_AGAIN) bfs_fs_sync(fs);
                }
            }
            bfs_inode_delete(&fs->inode_tree, ino);
        }
    }
    return bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
}

typedef struct { uint32_t count; } empty_check_t;
static bool empty_check_cb(const char *n, uint8_t nl, uint32_t ino, uint32_t t, void *ctx)
{
    (void)ino; (void)t;
    if (nl == 2 && n[0] == '.' && n[1] == '.') return true;
    ((empty_check_t *)ctx)->count++;
    return false;
}

bfs_err_t bfs_fs_rmdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    uint32_t dir_ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, parent_ino, name, name_len, &dir_ino, &type);
    if (err != BFS_OK) return err;
    if (type != BFS_INODE_DIR) return BFS_ERR_INVAL;
    empty_check_t ec = {0};
    bfs_dir_scan(&fs->dir_tree, dir_ino, empty_check_cb, &ec);
    if (ec.count > 0) return BFS_ERR_NOTEMPTY;
    return bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
}

bfs_err_t bfs_fs_rename(bfs_fs_t *fs, uint32_t old_parent, const char *old_name, uint8_t old_len, uint32_t new_parent, const char *new_name, uint8_t new_len)
{
    uint32_t ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, old_parent, old_name, old_len, &ino, &type);
    if (err != BFS_OK) return err;
    err = bfs_dir_insert(&fs->dir_tree, new_parent, new_name, new_len, ino, type);
    if (err != BFS_OK) return err;
    err = bfs_dir_remove(&fs->dir_tree, old_parent, old_name, old_len);
    if (err != BFS_OK) bfs_dir_remove(&fs->dir_tree, new_parent, new_name, new_len);
    return err;
}

bfs_err_t bfs_fs_make_hardlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t target_ino)
{
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&fs->inode_tree, target_ino, &inode);
    if (err != BFS_OK) return err;
    inode.link_count = bfs_be32(bfs_be32(inode.link_count) + 1);
    bfs_inode_write(&fs->inode_tree, target_ino, &inode);
    return bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, target_ino, BFS_INODE_HARDLINK);
}

bfs_err_t bfs_fs_make_softlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, const char *target_path, uint16_t path_len)
{
    uint32_t ino = bfs_fs_alloc_ino(fs);
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_SOFTLINK);
    inode.size_lo = bfs_be32(path_len);
    bfs_inode_write(&fs->inode_tree, ino, &inode);
    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    bfs_file_write(&f, target_path, path_len);
    return bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_SOFTLINK);
}

bfs_err_t bfs_fs_set_comment(bfs_fs_t *fs, uint32_t ino, const char *comment, uint8_t len)
{
    uint32_t comment_parent = ino | 0x80000000u;
    bfs_dir_remove(&fs->dir_tree, comment_parent, "\x01", 1);
    if (len == 0) return BFS_OK;
    if (len > 79) len = 79;
    return bfs_dir_insert(&fs->dir_tree, comment_parent, comment, len, ino, 0);
}

static bool comment_scan_cb(const char *name, uint8_t name_len, uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    (void)inode_nr; (void)entry_type;
    memcpy(*(char **)ctx, name, name_len);
    (*(char **)ctx)[name_len] = 0;
    return false;
}

bfs_err_t bfs_fs_get_comment(bfs_fs_t *fs, uint32_t ino, char *buf, uint8_t max_len)
{
    (void)max_len;
    buf[0] = 0;
    uint32_t comment_parent = ino | 0x80000000u;
    bfs_dir_scan(&fs->dir_tree, comment_parent, comment_scan_cb, &buf);
    return buf[0] ? BFS_OK : BFS_ERR_NOTFOUND;
}

/* ── Sync ──────────────────────────────────────────────────── */

static void update_tree_txns(bfs_fs_t *fs)
{
    fs->live_txn_id = bfs_txn_id(&fs->txn);
}

static void shellsort_blocks(bfs_blk_t *arr, uint32_t count)
{
    static const uint32_t gaps[] = {1750, 701, 301, 132, 57, 23, 10, 4, 1, 0};
    for (const uint32_t *g = gaps; *g; g++) {
        uint32_t gap = *g;
        for (uint32_t i = gap; i < count; i++) {
            bfs_blk_t tmp = arr[i];
            uint32_t j = i;
            while (j >= gap && arr[j - gap] > tmp) {
                arr[j] = arr[j - gap];
                j -= gap;
            }
            arr[j] = tmp;
        }
    }
}

bfs_err_t bfs_fs_sync(bfs_fs_t *fs)
{
    if (!fs->mounted) return BFS_ERR_INVAL;

    /* Phase 0: If data=ordered is enabled, flush all data writes to physical media
     * before we commit the metadata that points to them. This ensures that a
     * crash never leaves an inode pointing to uninitialized data blocks. */
    if (fs->options & BFS_OPT_DATA_ORDERED) {
        bfs_bio_sync(fs->bio);
    }

    /* Update superblock with current tree roots */
    bfs_txn_set_dir_root(&fs->txn, fs->dir_tree.tree.root);
    bfs_txn_set_free_root(&fs->txn, fs->freespace.tree.root);
    bfs_txn_set_free_blocks(&fs->txn, fs->freespace.total_free);
    bfs_txn_set_inode_root(&fs->txn, fs->inode_tree.root);
    if (fs->has_snapshots)
        fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);
    fs->txn.sb_new.next_ino = bfs_be32(fs->next_ino);

    bfs_err_t err = bfs_txn_commit(&fs->txn);
    if (err != BFS_OK) return err;
    update_tree_txns(fs);

    /* Process pending frees: Use a local buffer to avoid overwriting while processing */
    int sync_iterations = 0;
    while (fs->pending_count > 0 && sync_iterations < 256) {
        sync_iterations++;
        uint32_t count = fs->pending_count;
        bfs_blk_t *process_buf = malloc(count * sizeof(bfs_blk_t));
        if (!process_buf) return BFS_ERR_NOMEM;
        
        memcpy(process_buf, fs->pending_frees, count * sizeof(bfs_blk_t));
        fs->pending_count = 0;

        shellsort_blocks(process_buf, count);

        uint32_t i = 0;
        while (i < count) {
            bfs_blk_t start = process_buf[i];
            uint32_t len = 1;
            while (i + len < count && process_buf[i + len] == start + len) len++;

            if (fs->has_snapshots && fs->refcount.tree.root != BFS_BLK_NULL) {
                for (uint32_t b = 0; b < len; b++) {
                    uint32_t rc = bfs_refcount_get(&fs->refcount, start + b);
                    if (rc > 0) {
                        bool freed;
                        bfs_refcount_dec(&fs->refcount, start + b, &freed);
                        if (freed) bfs_freespace_free(&fs->freespace, start + b, 1);
                    } else {
                        bfs_freespace_free(&fs->freespace, start + b, 1);
                    }
                }
            } else {
                bfs_freespace_free(&fs->freespace, start, len);
            }
            i += len;
        }
        free(process_buf);

        /* Final commit of free tree changes */
        bfs_txn_set_free_root(&fs->txn, fs->freespace.tree.root);
        bfs_txn_set_free_blocks(&fs->txn, fs->freespace.total_free);
        if (fs->has_snapshots)
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);
        err = bfs_txn_commit(&fs->txn);
        if (err != BFS_OK) return err;
    }

    update_tree_txns(fs);
    return bfs_bio_sync(fs->bio);
}

bfs_err_t bfs_fs_unmount(bfs_fs_t *fs)
{
    if (!fs->mounted) return BFS_ERR_INVAL;
    bfs_err_t err = bfs_fs_sync(fs);
    free(fs->scratch);
    fs->mounted = false;
    return err;
}

/* Each item needs at most (tree_depth * 2) blocks for COW + potential split.
 * On AmigaOS, the handler processes packets sequentially; reserve/unreserve 
 * is inherently safe without locking. Multi-threaded ports require atomics. */
bfs_err_t bfs_fs_reserve(bfs_fs_t *fs, uint32_t items)
{
    uint32_t needed = items * 12;
    uint32_t available = fs->freespace.total_free + fs->pending_count + fs->freespace.reserve_count;
    if (fs->freespace.emergency_count) available += bfs_be32(*fs->freespace.emergency_count);
    return (available < needed) ? BFS_ERR_NOSPC : BFS_OK;
}

void bfs_fs_unreserve(bfs_fs_t *fs, uint32_t items) { (void)fs; (void)items; }
