/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Filesystem format, mount, sync, unmount
 */

#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_extent.h"
#include "bfs_snapshot.h"
#include <string.h>
#include <stdlib.h>

static bool valid_block_size(uint32_t bs)
{
    return bs >= BFS_MIN_BLOCK_SIZE && bs <= BFS_MAX_BLOCK_SIZE && (bs & (bs - 1)) == 0;
}

static bfs_err_t return_reserve_to_free_tree(bfs_fs_t *fs);

/* ── Format ────────────────────────────────────────────────── */

bfs_err_t bfs_fs_format(bfs_bio_t *bio, const char *volname, uint32_t options)
{
    if (!bio) return BFS_ERR_INVAL;
    uint32_t bs = bio->block_size;
    bfs_blk_t bc = bio->block_count;

    if (!valid_block_size(bs) || bc < BFS_MIN_VOLUME_BLOCKS)
        return BFS_ERR_INVAL;

    bfs_blk_t data_start = (BFS_DATA_OFFSET + bs - 1) / bs;
    if (data_start == BFS_BLK_NULL || bc <= data_start)
        return BFS_ERR_INVAL;
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
    bfs_lock_init(&fs.lock);
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
    fs.freespace.sb = &fs.txn.sb_new;

    uint32_t epool_count = BFS_EMERGENCY_POOL_SIZE;
    if (epool_count > data_blocks / 4) epool_count = data_blocks / 4;
    for (uint32_t i = 0; i < epool_count; i++)
        sb.emergency_pool[i] = bfs_be32(data_start + i);
    sb.emergency_count = bfs_be32(epool_count);
    fs.txn.sb_new.emergency_count = sb.emergency_count;
    memcpy(fs.txn.sb_new.emergency_pool, sb.emergency_pool, sizeof(sb.emergency_pool));

    uint32_t greserve = bc / 20;
    if (greserve < 64) greserve = (data_blocks < 1024) ? (data_blocks / 8) : 64;
    if (greserve < 8) greserve = 8;
    if (greserve > 512) greserve = 512;
    if (greserve >= data_blocks) greserve = data_blocks / 4;
    sb.global_reserve = bfs_be32(greserve);
    fs.txn.sb_new.global_reserve = sb.global_reserve;
    fs.freespace.global_reserve = greserve;

    bfs_blk_t backup_blk = (bfs_blk_t)(backup_off / bs);
    if (backup_blk >= data_start + epool_count && backup_blk < bc) {
        err = bfs_freespace_add(&fs.freespace, data_start + epool_count, backup_blk - (data_start + epool_count));
        if (err != BFS_OK) return err;
        err = bfs_freespace_add(&fs.freespace, backup_blk + 1, bc - (backup_blk + 1));
        if (err != BFS_OK) return err;
        fs.freespace.total_free = data_blocks - epool_count - 1;
    } else {
        err = bfs_freespace_add(&fs.freespace, data_start + epool_count, data_blocks - epool_count);
        if (err != BFS_OK) return err;
        fs.freespace.total_free = data_blocks - epool_count;
    }
    bfs_freespace_refill_reserve(&fs.freespace);

    err = bfs_dir_init(&fs.dir_tree, bio, bfs_freespace_allocator(&fs.freespace),
                  BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) return err;
    fs.dir_tree.tree.txn_id_ptr = &fs.live_txn_id;
    fs.dir_tree.tree.fs_ctx = &fs;
    err = bfs_dir_insert(&fs.dir_tree, 0, "/", 1, BFS_ROOT_INO, BFS_INODE_DIR);
    if (err != BFS_OK) return err;

    err = bfs_inode_init(&fs.inode_tree, bio, bfs_freespace_allocator(&fs.freespace),
                    BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) return err;
    fs.inode_tree.txn_id_ptr = &fs.live_txn_id;
    fs.inode_tree.fs_ctx = &fs;
    bfs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.inode_nr = bfs_be32(BFS_ROOT_INO);
    root_inode.type = bfs_be32(BFS_INODE_DIR);
    root_inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs.inode_tree, BFS_ROOT_INO, &root_inode);
    if (err != BFS_OK) return err;

    err = return_reserve_to_free_tree(&fs);
    if (err != BFS_OK) return err;

    bfs_txn_set_dir_root(&fs.txn, fs.dir_tree.tree.root);
    bfs_txn_set_free_root(&fs.txn, fs.freespace.tree.root);
    bfs_txn_set_free_blocks(&fs.txn, fs.freespace.total_free);
    bfs_txn_set_inode_root(&fs.txn, fs.inode_tree.root);

    err = bfs_txn_commit(&fs.txn);
    bfs_lock_destroy(&fs.lock);
    if (err != BFS_OK) return err;
    return bfs_bio_sync(bio);
}

/* ── Mount ─────────────────────────────────────────────────── */

bfs_err_t bfs_fs_mount(bfs_fs_t *fs, bfs_bio_t *bio)
{
    memset(fs, 0, sizeof(*fs));
    bfs_lock_init(&fs->lock);
    fs->bio = bio;
    bfs_err_t err = bfs_txn_begin(&fs->txn, bio);
    if (err != BFS_OK) goto fail;

    bfs_superblock_t *sb = &fs->txn.sb;
    fs->live_txn_id = bfs_txn_id(&fs->txn);

    bfs_blk_t free_root = bfs_be32(sb->free_tree_root);
    err = bfs_freespace_init(&fs->freespace, bio, free_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->freespace.tree.txn_id_ptr = &fs->live_txn_id;
    fs->freespace.tree.fs_ctx = fs;
    fs->freespace.total_free = bfs_be32(sb->free_blocks);
    fs->freespace.global_reserve = bfs_be32(sb->global_reserve);
    fs->freespace.sb = &fs->txn.sb_new;

    bfs_blk_t dir_root = bfs_be32(sb->dir_tree_root);
    err = bfs_dir_init(&fs->dir_tree, bio, bfs_freespace_allocator(&fs->freespace),
                  dir_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->dir_tree.tree.txn_id_ptr = &fs->live_txn_id;
    fs->dir_tree.tree.fs_ctx = fs;

    bfs_blk_t inode_root = bfs_be32(sb->inode_tree_root);
    err = bfs_inode_init(&fs->inode_tree, bio, bfs_freespace_allocator(&fs->freespace),
                    inode_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->inode_tree.txn_id_ptr = &fs->live_txn_id;
    fs->inode_tree.fs_ctx = fs;

    bfs_blk_t rc_root = bfs_be32(sb->refcount_tree_root);
    fs->has_snapshots = (rc_root != BFS_BLK_NULL && rc_root != 0);
    if (fs->has_snapshots) {
        err = bfs_refcount_init(&fs->refcount, bio, bfs_freespace_allocator(&fs->freespace),
                                 rc_root, fs->live_txn_id);
        if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
        fs->refcount.tree.txn_id_ptr = &fs->live_txn_id;
        fs->refcount.tree.fs_ctx = fs;
    }

    fs->mounted = true;
    fs->next_ino = bfs_be32(sb->next_ino);
    fs->options = bfs_be32(sb->options);
    fs->data_checksums = (fs->options & BFS_OPT_DATA_CHECKSUMS) != 0;
    fs->scratch = malloc(bio->block_size);
    if (!fs->scratch) { err = BFS_ERR_NOMEM; goto fail; }

    /* Resume any interrupted snapshot deletions */
    err = bfs_snapshot_resume_deletions(fs);
    if (err != BFS_OK) {
        free(fs->scratch);
        fs->scratch = NULL;
        goto fail;
    }

    return BFS_OK;

fail:
    bfs_lock_destroy(&fs->lock);
    return err;
}

/* ── Inode allocation ──────────────────────────────────────── */

uint32_t bfs_fs_alloc_ino(bfs_fs_t *fs)
{
    return fs->next_ino++;
}

/* ── Directory operations ──────────────────────────────────── */

static bfs_err_t fs_read_inode_type(bfs_fs_t *fs, uint32_t ino,
                                    bfs_inode_t *inode_out, uint32_t *type_out)
{
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) return err;
    uint32_t type = bfs_be32(inode.type);
    if (inode_out) *inode_out = inode;
    if (type_out) *type_out = type;
    return BFS_OK;
}

static bfs_err_t fs_require_dir(bfs_fs_t *fs, uint32_t ino)
{
    uint32_t type;
    bfs_err_t err = fs_read_inode_type(fs, ino, NULL, &type);
    if (err != BFS_OK) return err;
    return type == BFS_INODE_DIR ? BFS_OK : BFS_ERR_INVAL;
}

static void fs_release_ino_if_last(bfs_fs_t *fs, uint32_t ino)
{
    if (fs->next_ino == ino + 1)
        fs->next_ino = ino;
}

static bfs_err_t fs_queue_pending_block(bfs_fs_t *fs, bfs_blk_t blk)
{
    if (blk == BFS_BLK_NULL) return BFS_OK;
    if (!fs->has_snapshots)
        return bfs_freespace_free(&fs->freespace, blk, 1);
    if (fs->pending_count >= BFS_PENDING_FREES_MAX) {
        bfs_err_t err = bfs_fs_sync_unlocked(fs);
        if (err != BFS_OK) return err;
    }
    if (fs->pending_count >= BFS_PENDING_FREES_MAX)
        return BFS_ERR_NOSPC;
    fs->pending_frees[fs->pending_count++] = blk;
    return BFS_OK;
}

typedef struct {
    bfs_fs_t *fs;
    bfs_err_t err;
} fs_queue_ctx_t;

static bool fs_queue_extent_data_cb(const void *key, const void *val, void *ctx)
{
    (void)key;
    fs_queue_ctx_t *qc = (fs_queue_ctx_t *)ctx;
    const bfs_extent_val_t *ev = (const bfs_extent_val_t *)val;
    bfs_blk_t disk = bfs_be32(ev->disk_block);
    uint32_t len = bfs_be32(ev->length);
    for (uint32_t i = 0; i < len; i++) {
        qc->err = fs_queue_pending_block(qc->fs, disk + i);
        if (qc->err != BFS_OK) return false;
    }
    return true;
}

static void fs_queue_extent_node_cb(bfs_blk_t blk, void *ctx)
{
    fs_queue_ctx_t *qc = (fs_queue_ctx_t *)ctx;
    if (qc->err == BFS_OK)
        qc->err = fs_queue_pending_block(qc->fs, blk);
}

static bfs_err_t fs_queue_extent_tree_for_delete(bfs_fs_t *fs, bfs_blk_t root)
{
    if (root == BFS_BLK_NULL) return BFS_OK;
    bfs_extent_tree_t et;
    bfs_err_t err = bfs_extent_init(&et, fs->bio, &fs->freespace, root, fs->live_txn_id);
    if (err != BFS_OK) return err;
    fs_queue_ctx_t qc = { .fs = fs, .err = BFS_OK };
    err = bfs_btree_scan(&et.tree, NULL, fs_queue_extent_data_cb, &qc);
    if (err != BFS_OK) return err;
    if (qc.err != BFS_OK) return qc.err;
    {
        bfs_err_t werr = bfs_btree_walk_nodes(&et.tree, fs_queue_extent_node_cb, &qc);
        if (qc.err == BFS_OK) qc.err = werr;
    }
    return qc.err;
}

static bfs_err_t fs_create_file_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t ino = bfs_fs_alloc_ino(fs);
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_FILE);
    inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) {
        fs_release_ino_if_last(fs, ino);
        return err;
    }
    err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_FILE);
    if (err != BFS_OK) {
        bfs_inode_delete(&fs->inode_tree, ino);
        fs_release_ino_if_last(fs, ino);
        return err;
    }
    if (ino_out) *ino_out = ino;
    return BFS_OK;
}

static bfs_err_t fs_mkdir_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t ino = bfs_fs_alloc_ino(fs);
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_DIR);
    inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) {
        fs_release_ino_if_last(fs, ino);
        return err;
    }
    err = bfs_dir_insert(&fs->dir_tree, ino, "..", 2, parent_ino, BFS_INODE_DIR);
    if (err != BFS_OK) {
        bfs_inode_delete(&fs->inode_tree, ino);
        fs_release_ino_if_last(fs, ino);
        return err;
    }
    err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_DIR);
    if (err != BFS_OK) {
        bfs_dir_remove(&fs->dir_tree, ino, "..", 2);
        bfs_inode_delete(&fs->inode_tree, ino);
        fs_release_ino_if_last(fs, ino);
        return err;
    }
    if (ino_out) *ino_out = ino;
    return BFS_OK;
}

static bfs_err_t fs_delete_file_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    uint32_t ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, parent_ino, name, name_len, &ino, &type);
    if (err != BFS_OK) return err;
    if (type == BFS_INODE_DIR) return BFS_ERR_INVAL;
    bfs_inode_t inode;
    if (bfs_inode_read(&fs->inode_tree, ino, &inode) == BFS_OK) {
        uint32_t lc = bfs_be32(inode.link_count);
        if (lc > 1) {
            inode.link_count = bfs_be32(lc - 1);
            err = bfs_inode_write(&fs->inode_tree, ino, &inode);
            if (err != BFS_OK) return err;
            err = bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
            if (err != BFS_OK) {
                inode.link_count = bfs_be32(lc);
                bfs_inode_write(&fs->inode_tree, ino, &inode);
            }
            return err;
        } else {
            err = bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
            if (err != BFS_OK) return err;
            bfs_blk_t ext_root = bfs_be32(inode.extent_root);
            err = bfs_inode_delete(&fs->inode_tree, ino);
            if (err != BFS_OK) {
                bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, type);
                return err;
            }
            return fs_queue_extent_tree_for_delete(fs, ext_root);
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

static bfs_err_t fs_rmdir_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t dir_ino, type;
    err = bfs_dir_lookup(&fs->dir_tree, parent_ino, name, name_len, &dir_ino, &type);
    if (err != BFS_OK) return err;
    if (type != BFS_INODE_DIR) return BFS_ERR_INVAL;
    empty_check_t ec = {0};
    err = bfs_dir_scan(&fs->dir_tree, dir_ino, empty_check_cb, &ec);
    if (err != BFS_OK) return err;
    if (ec.count > 0) return BFS_ERR_NOTEMPTY;

    err = bfs_dir_remove(&fs->dir_tree, dir_ino, "..", 2);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND) return err;

    err = bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
    if (err != BFS_OK) {
        bfs_dir_insert(&fs->dir_tree, dir_ino, "..", 2, parent_ino, BFS_INODE_DIR);
        return err;
    }

    err = bfs_inode_delete(&fs->inode_tree, dir_ino);
    if (err != BFS_OK) {
        bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, dir_ino, BFS_INODE_DIR);
        bfs_dir_insert(&fs->dir_tree, dir_ino, "..", 2, parent_ino, BFS_INODE_DIR);
    }
    return err;
}

static bfs_err_t fs_dir_is_descendant(bfs_fs_t *fs, uint32_t dir_ino, uint32_t ancestor_ino,
                                      bool *is_descendant)
{
    *is_descendant = false;
    uint32_t cur = dir_ino;
    while (cur != BFS_ROOT_INO) {
        if (cur == ancestor_ino) {
            *is_descendant = true;
            return BFS_OK;
        }
        uint32_t parent = 0, type = 0;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, cur, "..", 2, &parent, &type);
        if (err != BFS_OK) return err;
        if (type != BFS_INODE_DIR || parent == cur) return BFS_ERR_CORRUPT;
        cur = parent;
    }
    *is_descendant = (ancestor_ino == BFS_ROOT_INO);
    return BFS_OK;
}

static bool same_name_folded(const char *a, uint8_t alen, const char *b, uint8_t blen)
{
    if (alen != blen) return false;
    for (uint8_t i = 0; i < alen; i++) {
        if (bfs_intl_toupper((uint8_t)a[i]) != bfs_intl_toupper((uint8_t)b[i]))
            return false;
    }
    return true;
}

static bfs_err_t fs_rename_unlocked(bfs_fs_t *fs, uint32_t old_parent, const char *old_name, uint8_t old_len, uint32_t new_parent, const char *new_name, uint8_t new_len)
{
    bfs_err_t err = fs_require_dir(fs, old_parent);
    if (err != BFS_OK) return err;
    err = fs_require_dir(fs, new_parent);
    if (err != BFS_OK) return err;
    if (old_parent == new_parent && same_name_folded(old_name, old_len, new_name, new_len))
        return BFS_OK;

    uint32_t ino, type;
    err = bfs_dir_lookup(&fs->dir_tree, old_parent, old_name, old_len, &ino, &type);
    if (err != BFS_OK) return err;

    uint32_t old_dotdot = 0;
    if (type == BFS_INODE_DIR) {
        bool descendant = false;
        err = fs_dir_is_descendant(fs, new_parent, ino, &descendant);
        if (err != BFS_OK) return err;
        if (descendant) return BFS_ERR_INVAL;
        err = bfs_dir_lookup(&fs->dir_tree, ino, "..", 2, &old_dotdot, NULL);
        if (err != BFS_OK) return err;
    }

    err = bfs_dir_insert(&fs->dir_tree, new_parent, new_name, new_len, ino, type);
    if (err != BFS_OK) return err;

    if (type == BFS_INODE_DIR && old_parent != new_parent) {
        err = bfs_dir_remove(&fs->dir_tree, ino, "..", 2);
        if (err == BFS_OK)
            err = bfs_dir_insert(&fs->dir_tree, ino, "..", 2, new_parent, BFS_INODE_DIR);
        if (err != BFS_OK) {
            bfs_dir_insert(&fs->dir_tree, ino, "..", 2, old_dotdot, BFS_INODE_DIR);
            bfs_dir_remove(&fs->dir_tree, new_parent, new_name, new_len);
            return err;
        }
    }

    err = bfs_dir_remove(&fs->dir_tree, old_parent, old_name, old_len);
    if (err != BFS_OK) {
        if (type == BFS_INODE_DIR && old_parent != new_parent) {
            bfs_dir_remove(&fs->dir_tree, ino, "..", 2);
            bfs_dir_insert(&fs->dir_tree, ino, "..", 2, old_dotdot, BFS_INODE_DIR);
        }
        bfs_dir_remove(&fs->dir_tree, new_parent, new_name, new_len);
    }
    return err;
}

static bfs_err_t fs_make_hardlink_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t target_ino)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    bfs_inode_t inode;
    err = bfs_inode_read(&fs->inode_tree, target_ino, &inode);
    if (err != BFS_OK) return err;
    if (bfs_be32(inode.type) != BFS_INODE_FILE) return BFS_ERR_INVAL;

    err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, target_ino, BFS_INODE_FILE);
    if (err != BFS_OK) return err;

    uint32_t old_lc = bfs_be32(inode.link_count);
    if (old_lc == UINT32_MAX) {
        bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
        return BFS_ERR_INVAL;
    }
    inode.link_count = bfs_be32(bfs_be32(inode.link_count) + 1);
    err = bfs_inode_write(&fs->inode_tree, target_ino, &inode);
    if (err != BFS_OK)
        bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
    return err;
}

static bfs_err_t fs_make_softlink_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, const char *target_path, uint16_t path_len)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t ino = bfs_fs_alloc_ino(fs);
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_SOFTLINK);
    inode.size_lo = bfs_be32(path_len);
    inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) {
        fs_release_ino_if_last(fs, ino);
        return err;
    }

    bfs_file_t f;
    err = bfs_file_open_unlocked(&f, fs, ino);
    if (err == BFS_OK) {
        int32_t written = bfs_file_write_unlocked(&f, target_path, path_len);
        err = (written == path_len) ? BFS_OK : (written < 0 ? (bfs_err_t)written : BFS_ERR_IO);
    }
    if (err == BFS_OK)
        err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_SOFTLINK);
    if (err != BFS_OK) {
        bfs_file_t cleanup;
        if (bfs_file_open_unlocked(&cleanup, fs, ino) == BFS_OK)
            bfs_file_truncate_unlocked(&cleanup, 0);
        bfs_inode_delete(&fs->inode_tree, ino);
        fs_release_ino_if_last(fs, ino);
    }
    return err;
}

static bfs_err_t fs_set_comment_unlocked(bfs_fs_t *fs, uint32_t ino, const char *comment, uint8_t len)
{
    uint32_t comment_parent = ino | 0x80000000u;
    bfs_dir_remove(&fs->dir_tree, comment_parent, "\x01", 1);
    if (len == 0) return BFS_OK;
    if (len > 79) len = 79;
    return bfs_dir_insert(&fs->dir_tree, comment_parent, comment, len, ino, 0);
}

typedef struct {
    char *buf;
    uint8_t max_len;
    bool found;
} comment_ctx_t;

static bool comment_scan_cb(const char *name, uint8_t name_len, uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    (void)inode_nr; (void)entry_type;
    comment_ctx_t *cc = (comment_ctx_t *)ctx;
    if (cc->max_len == 0) {
        cc->found = true;
        return false;
    }
    uint8_t copy_len = name_len;
    if (copy_len >= cc->max_len)
        copy_len = cc->max_len - 1;
    memcpy(cc->buf, name, copy_len);
    cc->buf[copy_len] = 0;
    cc->found = true;
    return false;
}

static bfs_err_t fs_get_comment_unlocked(bfs_fs_t *fs, uint32_t ino, char *buf, uint8_t max_len)
{
    if (!buf || max_len == 0) return BFS_ERR_INVAL;
    buf[0] = 0;
    uint32_t comment_parent = ino | 0x80000000u;
    comment_ctx_t cc = { .buf = buf, .max_len = max_len, .found = false };
    bfs_err_t err = bfs_dir_scan(&fs->dir_tree, comment_parent, comment_scan_cb, &cc);
    if (err != BFS_OK) return err;
    return cc.found ? BFS_OK : BFS_ERR_NOTFOUND;
}

/* ── Sync ──────────────────────────────────────────────────── */

static void update_tree_txns(bfs_fs_t *fs)
{
    fs->live_txn_id = bfs_txn_id(&fs->txn);
}

static bfs_err_t return_reserve_to_free_tree(bfs_fs_t *fs)
{
    uint32_t saved_global_reserve = fs->freespace.global_reserve;
    fs->freespace.global_reserve = UINT32_MAX;
    while (fs->freespace.reserve_count > 0) {
        bfs_blk_t blk = fs->freespace.reserve[--fs->freespace.reserve_count];
        bfs_err_t err = bfs_freespace_free(&fs->freespace, blk, 1);
        if (err != BFS_OK) {
            fs->freespace.reserve[fs->freespace.reserve_count++] = blk;
            fs->freespace.global_reserve = saved_global_reserve;
            return BFS_OK;
        }
    }
    fs->freespace.global_reserve = saved_global_reserve;
    return BFS_OK;
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

bfs_err_t bfs_fs_sync_unlocked(bfs_fs_t *fs)
{
    if (!fs->mounted) return BFS_ERR_INVAL;

    /* Phase 0: If data=ordered is enabled, flush all data writes to physical media
     * before we commit the metadata that points to them. This ensures that a
     * crash never leaves an inode pointing to uninitialized data blocks. */
    if (fs->options & BFS_OPT_DATA_ORDERED) {
        bfs_bio_sync(fs->bio);
    }

    bfs_err_t err = return_reserve_to_free_tree(fs);
    if (err != BFS_OK) return err;

    /* Update superblock with current tree roots */
    bfs_txn_set_dir_root(&fs->txn, fs->dir_tree.tree.root);
    bfs_txn_set_free_root(&fs->txn, fs->freespace.tree.root);
    bfs_txn_set_free_blocks(&fs->txn, fs->freespace.total_free);
    bfs_txn_set_inode_root(&fs->txn, fs->inode_tree.root);
    if (fs->has_snapshots)
        fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);
    fs->txn.sb_new.next_ino = bfs_be32(fs->next_ino);

    err = bfs_txn_commit(&fs->txn);
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

        err = return_reserve_to_free_tree(fs);
        if (err != BFS_OK) return err;

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

bfs_err_t bfs_fs_sync(bfs_fs_t *fs)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_sync_unlocked(fs);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_unmount(bfs_fs_t *fs)
{
    bfs_lock_write(&fs->lock);
    if (!fs->mounted) {
        bfs_lock_unlock(&fs->lock);
        return BFS_ERR_INVAL;
    }
    bfs_err_t err = bfs_fs_sync_unlocked(fs);
    free(fs->scratch);
    fs->mounted = false;
    bfs_lock_unlock(&fs->lock);
    bfs_lock_destroy(&fs->lock);
    return err;
}

/* Advisory ENOSPC pre-check. Each item needs at most ~12 blocks for COW +
 * potential splits across the trees.
 *
 * Locking model: this reads shared free-space accounting, so it takes the read
 * lock to get a torn-free snapshot on the multi-threaded host build. Note the
 * reserve->operation sequence is NOT atomic for concurrent callers — space can
 * be consumed between this check and the (separately locked) mutation. Only the
 * AmigaOS handler, which processes packets sequentially, gets an end-to-end
 * guarantee; multi-threaded callers must treat the result as a hint and still
 * handle BFS_ERR_NOSPC returned by the operation itself. */
bfs_err_t bfs_fs_reserve(bfs_fs_t *fs, uint32_t items)
{
    /* Overflow guard: items * 12 must not wrap, or a huge request would appear
     * to "fit" on a full volume and defeat the check entirely. */
    if (items > UINT32_MAX / 12)
        return BFS_ERR_NOSPC;
    uint32_t needed = items * 12;

    bfs_lock_read(&fs->lock);
    uint64_t available = (uint64_t)fs->freespace.total_free +
                         fs->pending_count + fs->freespace.reserve_count;
    if (fs->freespace.sb) available += bfs_be32(fs->freespace.sb->emergency_count);
    bfs_lock_unlock(&fs->lock);

    return (available < needed) ? BFS_ERR_NOSPC : BFS_OK;
}

void bfs_fs_unreserve(bfs_fs_t *fs, uint32_t items) { (void)fs; (void)items; }

bfs_err_t bfs_fs_create_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_create_file_unlocked(fs, parent_ino, name, name_len, ino_out);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_mkdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_mkdir_unlocked(fs, parent_ino, name, name_len, ino_out);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_delete_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_delete_file_unlocked(fs, parent_ino, name, name_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_rmdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_rmdir_unlocked(fs, parent_ino, name, name_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_rename(bfs_fs_t *fs, uint32_t old_parent, const char *old_name, uint8_t old_len, uint32_t new_parent, const char *new_name, uint8_t new_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_rename_unlocked(fs, old_parent, old_name, old_len, new_parent, new_name, new_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_make_hardlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t target_ino)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_make_hardlink_unlocked(fs, parent_ino, name, name_len, target_ino);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_make_softlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, const char *target_path, uint16_t path_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_make_softlink_unlocked(fs, parent_ino, name, name_len, target_path, path_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_set_comment(bfs_fs_t *fs, uint32_t ino, const char *comment, uint8_t len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs_set_comment_unlocked(fs, ino, comment, len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_get_comment(bfs_fs_t *fs, uint32_t ino, char *buf, uint8_t max_len)
{
    bfs_lock_read(&fs->lock);
    bfs_err_t err = fs_get_comment_unlocked(fs, ino, buf, max_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}
