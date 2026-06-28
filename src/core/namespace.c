/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Namespace operations (directory / inode CRUD)
 *
 * create / mkdir / delete / rmdir / rename / hard+soft links / comments, plus
 * their fs-lock wrappers. Split out of fs.c, which keeps format, mount, the
 * sync/reclaim engine and space reservation. These ops sit on top of the dir,
 * inode and extent trees and call fs.c's exported helpers (bfs_fs_alloc_ino,
 * bfs_fs_queue_pending_free) and the transaction boundary bfs_txn_commit().
 */
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_internal.h"
#include "bfs_inode.h"
#include "bfs_extent.h"
#include "bfs_snapshot.h"
#include <string.h>
#include <stdlib.h>

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

typedef struct {
    bfs_fs_t *fs;
    bfs_err_t err;
} fs_queue_ctx_t;

static void fs_queue_block_cb(bfs_blk_t blk, void *ctx)
{
    fs_queue_ctx_t *qc = (fs_queue_ctx_t *)ctx;
    if (qc->err == BFS_OK)
        qc->err = bfs_fs_queue_pending_free(qc->fs, blk);
}

static bfs_err_t fs_queue_extent_tree_for_delete(bfs_fs_t *fs, bfs_blk_t root)
{
    /* Free both the extent-tree node blocks and the data blocks they map. */
    fs_queue_ctx_t qc = { .fs = fs, .err = BFS_OK };
    bfs_err_t err = bfs_extent_walk(fs->bio, &fs->freespace, fs->live_txn_id, root,
                                    fs_queue_block_cb, fs_queue_block_cb, &qc);
    if (err != BFS_OK) return err;
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
bfs_err_t bfs_fs_create_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_create_file_unlocked(fs, parent_ino, name, name_len, ino_out);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_mkdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_mkdir_unlocked(fs, parent_ino, name, name_len, ino_out);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_delete_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_delete_file_unlocked(fs, parent_ino, name, name_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_rmdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_rmdir_unlocked(fs, parent_ino, name, name_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_rename(bfs_fs_t *fs, uint32_t old_parent, const char *old_name, uint8_t old_len, uint32_t new_parent, const char *new_name, uint8_t new_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_rename_unlocked(fs, old_parent, old_name, old_len, new_parent, new_name, new_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_make_hardlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t target_ino)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_make_hardlink_unlocked(fs, parent_ino, name, name_len, target_ino);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_make_softlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, const char *target_path, uint16_t path_len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_make_softlink_unlocked(fs, parent_ino, name, name_len, target_path, path_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_set_comment(bfs_fs_t *fs, uint32_t ino, const char *comment, uint8_t len)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_set_comment_unlocked(fs, ino, comment, len);
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
