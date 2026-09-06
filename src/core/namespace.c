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

typedef struct {
    char name[80];
    uint8_t len;
    bool found;
    bool corrupt;
} comment_key_ctx_t;

static bfs_err_t fs_find_comment_unlocked(bfs_fs_t *fs, uint32_t ino,
                                          comment_key_ctx_t *key);
static bfs_err_t fs_remove_comments_unlocked(bfs_fs_t *fs, uint32_t ino);

static bfs_err_t fs_restore_comment(bfs_fs_t *fs, uint32_t ino,
                                     const comment_key_ctx_t *comment)
{
    if (!comment->found) return BFS_OK;
    return bfs_dir_insert(&fs->dir_tree, ino | 0x80000000u, comment->name,
                           comment->len, ino, 0);
}

static bfs_err_t fs_cleanup_result(bfs_fs_t *fs, bfs_err_t primary, bfs_err_t cleanup)
{
    if (cleanup != BFS_OK) fs->recovery_error = cleanup;
    return cleanup == BFS_OK ? primary : cleanup;
}

static bfs_err_t fs_namespace_result(bfs_fs_t *fs, bfs_err_t result)
{
    bfs_err_t error = fs->recovery_error;
    if (error == BFS_OK) error = fs->dir_tree.tree.free_sink_err;
    if (error == BFS_OK) error = fs->inode_tree.free_sink_err;
    if (error == BFS_OK) error = fs->freespace.tree.free_sink_err;
    if (error == BFS_OK) error = fs->refcount.tree.free_sink_err;
    if (error == BFS_OK) return result;
    /* An incomplete rollback must never become the next committed namespace. */
    fs->recovery_error = error;
    return error;
}

static bool fs_name_valid(const char *name, uint8_t len)
{
    if (!name || len == 0) return false;
    if ((len == 1 && name[0] == '.') ||
        (len == 2 && name[0] == '.' && name[1] == '.'))
        return false;
    for (uint8_t i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == ':') return false;
    }
    return true;
}

static bool fs_handle_valid(const bfs_fs_t *fs)
{
    return fs && fs->mounted;
}

static bfs_err_t fs_read_inode_type(bfs_fs_t *fs, uint32_t ino,
                                    bfs_inode_t *inode_out, uint32_t *type_out)
{
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) return err;
    if (ino >= 0x80000000u) return BFS_ERR_CORRUPT;
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
    bfs_blk_t *items;
    size_t count;
    size_t capacity;
    size_t limit;
    bfs_err_t err;
} fs_delete_blocks_t;

static void fs_delete_blocks_destroy(fs_delete_blocks_t *blocks)
{
    free(blocks->items);
    blocks->items = NULL;
    blocks->count = blocks->capacity = 0;
}

static void fs_collect_delete_block(bfs_blk_t blk, void *ctx)
{
    fs_delete_blocks_t *blocks = (fs_delete_blocks_t *)ctx;
    if (blocks->err != BFS_OK) return;
    if (blk == BFS_BLK_NULL) {
        blocks->err = BFS_ERR_CORRUPT;
        return;
    }
    if (blocks->count >= blocks->limit) {
        blocks->err = BFS_ERR_NOSPC;
        return;
    }
    if (blocks->count == blocks->capacity) {
        size_t capacity = blocks->capacity ? blocks->capacity * 2u : 256u;
        if (capacity < blocks->capacity ||
            capacity > SIZE_MAX / sizeof(*blocks->items)) {
            blocks->err = BFS_ERR_NOMEM;
            return;
        }
        bfs_blk_t *items = malloc(capacity * sizeof(*items));
        if (!items) {
            blocks->err = BFS_ERR_NOMEM;
            return;
        }
        if (blocks->items) {
            /* count equals the old capacity; the checked new allocation doubles it. */
            memcpy(items, blocks->items, blocks->count * sizeof(*items)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        }
        free(blocks->items);
        blocks->items = items;
        blocks->capacity = capacity;
    }
    blocks->items[blocks->count++] = blk;
}

static void fs_sort_blocks(bfs_blk_t *items, size_t count)
{
    for (size_t gap = count / 2u; gap > 0; gap /= 2u) {
        for (size_t i = gap; i < count; i++) {
            bfs_blk_t value = items[i];
            size_t j = i;
            while (j >= gap && items[j - gap] > value) {
                items[j] = items[j - gap];
                j -= gap;
            }
            items[j] = value;
        }
    }
}

static bfs_err_t fs_collect_extent_tree_for_delete(bfs_fs_t *fs,
                                                   bfs_blk_t root,
                                                   fs_delete_blocks_t *blocks)
{
    uint32_t cap = bfs_fs_pending_cap(fs);
    blocks->limit = cap >= BFS_PENDING_FREES_MAX ? fs->bio->block_count :
                    cap > BFS_FS_OP_FREE_RESERVE
                        ? cap - BFS_FS_OP_FREE_RESERVE : 0;
    bfs_err_t err = bfs_extent_walk(fs->bio, &fs->freespace, fs->live_txn_id,
                                    root, fs_collect_delete_block,
                                    fs_collect_delete_block, blocks);
    if (err != BFS_OK) return err;
    if (blocks->err != BFS_OK) return blocks->err;

    if (blocks->count > 1)
        fs_sort_blocks(blocks->items, blocks->count);
    for (size_t i = 1; i < blocks->count; i++) {
        if (blocks->items[i] == blocks->items[i - 1])
            return BFS_ERR_CORRUPT;
    }
    if (blocks->count > UINT32_MAX - BFS_FS_OP_FREE_RESERVE)
        return BFS_ERR_NOSPC;
    return bfs_fs_ensure_free_headroom(
        fs, (uint32_t)blocks->count + BFS_FS_OP_FREE_RESERVE);
}

static bfs_err_t fs_queue_extent_tree_for_delete(bfs_fs_t *fs,
                                                 const fs_delete_blocks_t *blocks)
{
    for (size_t i = 0; i < blocks->count; i++) {
        bfs_err_t err = bfs_fs_queue_pending_free(fs, blocks->items[i]);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

static bfs_err_t fs_create_file_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t ino = bfs_fs_alloc_ino(fs);
    if (ino == 0) return BFS_ERR_NOSPC;
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
        bfs_err_t cleanup_err = bfs_inode_delete(&fs->inode_tree, ino);
        if (cleanup_err == BFS_OK) fs_release_ino_if_last(fs, ino);
        return fs_cleanup_result(fs, err, cleanup_err);
    }
    if (ino_out) *ino_out = ino;
    return BFS_OK;
}

static bfs_err_t fs_mkdir_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t ino = bfs_fs_alloc_ino(fs);
    if (ino == 0) return BFS_ERR_NOSPC;
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
        bfs_err_t cleanup_err = bfs_inode_delete(&fs->inode_tree, ino);
        if (cleanup_err == BFS_OK) fs_release_ino_if_last(fs, ino);
        return fs_cleanup_result(fs, err, cleanup_err);
    }
    err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, ino, BFS_INODE_DIR);
    if (err != BFS_OK) {
        bfs_err_t cleanup_err = bfs_dir_remove(&fs->dir_tree, ino, "..", 2);
        if (cleanup_err == BFS_OK)
            cleanup_err = bfs_inode_delete(&fs->inode_tree, ino);
        if (cleanup_err == BFS_OK) fs_release_ino_if_last(fs, ino);
        return fs_cleanup_result(fs, err, cleanup_err);
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
    err = bfs_inode_read(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK)
        return err == BFS_ERR_NOTFOUND ? BFS_ERR_CORRUPT : err;

    uint32_t lc = bfs_be32(inode.link_count);
    if (lc == 0) return BFS_ERR_CORRUPT;
    if (lc > 1) {
        inode.link_count = bfs_be32(lc - 1);
        err = bfs_inode_write(&fs->inode_tree, ino, &inode);
        if (err != BFS_OK) return err;
        err = bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
        if (err != BFS_OK) {
            inode.link_count = bfs_be32(lc);
            bfs_err_t rollback_err = bfs_inode_write(&fs->inode_tree, ino, &inode);
            return fs_cleanup_result(fs, err, rollback_err);
        }
        return BFS_OK;
    }

    fs_delete_blocks_t blocks = {0};
    err = fs_collect_extent_tree_for_delete(fs, bfs_be32(inode.extent_root),
                                            &blocks);
    if (err != BFS_OK) {
        fs_delete_blocks_destroy(&blocks);
        return err;
    }

    comment_key_ctx_t comment;
    err = fs_find_comment_unlocked(fs, ino, &comment);
    if (err != BFS_OK) goto delete_out;
    if (comment.found) {
        err = bfs_dir_remove(&fs->dir_tree, ino | 0x80000000u,
                              comment.name, comment.len);
        if (err != BFS_OK) goto delete_out;
    }
    err = bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
    if (err != BFS_OK) {
        err = fs_cleanup_result(fs, err, fs_restore_comment(fs, ino, &comment));
        goto delete_out;
    }
    err = bfs_inode_delete(&fs->inode_tree, ino);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = bfs_dir_insert(&fs->dir_tree, parent_ino, name,
                                                name_len, ino, type);
        bfs_err_t comment_err = fs_restore_comment(fs, ino, &comment);
        if (rollback_err == BFS_OK) rollback_err = comment_err;
        err = fs_cleanup_result(fs, err, rollback_err);
        goto delete_out;
    }
    err = fs_queue_extent_tree_for_delete(fs, &blocks);

delete_out:
    fs_delete_blocks_destroy(&blocks);
    return err;
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

    comment_key_ctx_t comment;
    err = fs_find_comment_unlocked(fs, dir_ino, &comment);
    if (err != BFS_OK) return err;
    if (comment.found) {
        err = bfs_dir_remove(&fs->dir_tree, dir_ino | 0x80000000u,
                              comment.name, comment.len);
        if (err != BFS_OK) return err;
    }
    err = bfs_dir_remove(&fs->dir_tree, dir_ino, "..", 2);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND)
        return fs_cleanup_result(fs, err, fs_restore_comment(fs, dir_ino, &comment));
    bool removed_dotdot = err == BFS_OK;

    err = bfs_dir_remove(&fs->dir_tree, parent_ino, name, name_len);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = BFS_OK;
        if (removed_dotdot)
            rollback_err = bfs_dir_insert(&fs->dir_tree, dir_ino, "..", 2,
                                          parent_ino, BFS_INODE_DIR);
        bfs_err_t comment_err = fs_restore_comment(fs, dir_ino, &comment);
        if (rollback_err == BFS_OK) rollback_err = comment_err;
        return fs_cleanup_result(fs, err, rollback_err);
    }

    err = bfs_inode_delete(&fs->inode_tree, dir_ino);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = bfs_dir_insert(&fs->dir_tree, parent_ino, name,
                                                name_len, dir_ino, BFS_INODE_DIR);
        if (removed_dotdot) {
            bfs_err_t dotdot_err = bfs_dir_insert(&fs->dir_tree, dir_ino, "..", 2,
                                                  parent_ino, BFS_INODE_DIR);
            if (rollback_err == BFS_OK) rollback_err = dotdot_err;
        }
        bfs_err_t comment_err = fs_restore_comment(fs, dir_ino, &comment);
        if (rollback_err == BFS_OK) rollback_err = comment_err;
        return fs_cleanup_result(fs, err, rollback_err);
    }
    return BFS_OK;
}

static bfs_err_t fs_dir_is_descendant(bfs_fs_t *fs, uint32_t dir_ino, uint32_t ancestor_ino,
                                      bool *is_descendant)
{
    *is_descendant = false;
    uint32_t cur = dir_ino;
    uint32_t fast = dir_ino;
    while (cur != BFS_ROOT_INO) {
        if (cur == ancestor_ino) {
            *is_descendant = true;
            return BFS_OK;
        }
        uint32_t parent = 0, type = 0;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, cur, "..", 2, &parent, &type);
        if (err != BFS_OK) return err;
        if (type != BFS_INODE_DIR || parent == 0 || parent == cur)
            return BFS_ERR_CORRUPT;
        cur = parent;

        for (unsigned int step = 0; step < 2 && fast != BFS_ROOT_INO; step++) {
            uint32_t fast_parent = 0;
            err = bfs_dir_lookup(&fs->dir_tree, fast, "..", 2, &fast_parent, &type);
            if (err != BFS_OK) return err;
            if (type != BFS_INODE_DIR || fast_parent == 0 || fast_parent == fast)
                return BFS_ERR_CORRUPT;
            fast = fast_parent;
        }
        if (cur != BFS_ROOT_INO && cur == fast) return BFS_ERR_CORRUPT;
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
        if (old_dotdot != old_parent) return BFS_ERR_CORRUPT;
    }

    err = bfs_dir_insert(&fs->dir_tree, new_parent, new_name, new_len, ino, type);
    if (err != BFS_OK) return err;

    if (type == BFS_INODE_DIR && old_parent != new_parent) {
        bool removed_dotdot = false;
        err = bfs_dir_remove(&fs->dir_tree, ino, "..", 2);
        if (err == BFS_OK) {
            removed_dotdot = true;
            err = bfs_dir_insert(&fs->dir_tree, ino, "..", 2, new_parent, BFS_INODE_DIR);
        }
        if (err != BFS_OK) {
            bfs_err_t rollback_err = BFS_OK;
            if (removed_dotdot)
                rollback_err = bfs_dir_insert(&fs->dir_tree, ino, "..", 2,
                                              old_dotdot, BFS_INODE_DIR);
            bfs_err_t remove_err = bfs_dir_remove(&fs->dir_tree, new_parent,
                                                  new_name, new_len);
            if (rollback_err == BFS_OK) rollback_err = remove_err;
            return fs_cleanup_result(fs, err, rollback_err);
        }
    }

    err = bfs_dir_remove(&fs->dir_tree, old_parent, old_name, old_len);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = BFS_OK;
        if (type == BFS_INODE_DIR && old_parent != new_parent) {
            rollback_err = bfs_dir_remove(&fs->dir_tree, ino, "..", 2);
            if (rollback_err == BFS_OK)
                rollback_err = bfs_dir_insert(&fs->dir_tree, ino, "..", 2,
                                              old_dotdot, BFS_INODE_DIR);
        }
        bfs_err_t remove_err = bfs_dir_remove(&fs->dir_tree, new_parent,
                                              new_name, new_len);
        if (rollback_err == BFS_OK) rollback_err = remove_err;
        return fs_cleanup_result(fs, err, rollback_err);
    }
    return BFS_OK;
}

static bfs_err_t fs_make_hardlink_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t target_ino)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    bfs_inode_t inode;
    err = bfs_inode_read(&fs->inode_tree, target_ino, &inode);
    if (err != BFS_OK) return err;
    if (bfs_be32(inode.type) != BFS_INODE_FILE) return BFS_ERR_INVAL;

    uint32_t old_lc = bfs_be32(inode.link_count);
    if (old_lc == 0) return BFS_ERR_CORRUPT;
    if (old_lc == UINT32_MAX) return BFS_ERR_NOSPC;

    err = bfs_dir_insert(&fs->dir_tree, parent_ino, name, name_len, target_ino, BFS_INODE_FILE);
    if (err != BFS_OK) return err;

    inode.link_count = bfs_be32(old_lc + 1);
    err = bfs_inode_write(&fs->inode_tree, target_ino, &inode);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = bfs_dir_remove(&fs->dir_tree, parent_ino, name,
                                                name_len);
        return fs_cleanup_result(fs, err, rollback_err);
    }
    return err;
}

static bfs_err_t fs_make_softlink_unlocked(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, const char *target_path, uint16_t path_len)
{
    bfs_err_t err = fs_require_dir(fs, parent_ino);
    if (err != BFS_OK) return err;

    uint32_t ino = bfs_fs_alloc_ino(fs);
    if (ino == 0) return BFS_ERR_NOSPC;
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
        bfs_err_t cleanup_err = bfs_file_open_unlocked(&cleanup, fs, ino);
        if (cleanup_err == BFS_OK)
            cleanup_err = bfs_file_truncate_unlocked(&cleanup, 0);
        if (cleanup_err == BFS_OK)
            cleanup_err = bfs_inode_delete(&fs->inode_tree, ino);
        if (cleanup_err == BFS_OK) fs_release_ino_if_last(fs, ino);
        err = fs_cleanup_result(fs, err, cleanup_err);
    }
    return err;
}

static bool comment_key_cb(const char *name, uint8_t name_len,
                           uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    (void)inode_nr;
    (void)entry_type;
    comment_key_ctx_t *key = (comment_key_ctx_t *)ctx;
    if (key->found || name_len > 79) {
        key->corrupt = true;
        return false;
    }
    key->len = name_len;
    /* The check above leaves a terminator slot in this 80-byte array. */
    memcpy(key->name, name, key->len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    key->name[key->len] = 0;
    key->found = true;
    return true;
}

static bfs_err_t fs_find_comment_unlocked(bfs_fs_t *fs, uint32_t ino,
                                          comment_key_ctx_t *key)
{
    memset(key, 0, sizeof(*key));
    uint32_t comment_parent = ino | 0x80000000u;
    bfs_err_t err = bfs_dir_scan(&fs->dir_tree, comment_parent, comment_key_cb, key);
    if (err != BFS_OK) return err;
    return key->corrupt ? BFS_ERR_CORRUPT : BFS_OK;
}

static bfs_err_t fs_remove_comments_unlocked(bfs_fs_t *fs, uint32_t ino)
{
    uint32_t comment_parent = ino | 0x80000000u;
    for (;;) {
        comment_key_ctx_t key;
        bfs_err_t err = fs_find_comment_unlocked(fs, ino, &key);
        if (err != BFS_OK) return err;
        if (!key.found) return BFS_OK;
        err = bfs_dir_remove(&fs->dir_tree, comment_parent, key.name, key.len);
        if (err != BFS_OK) return err;
    }
}

static bfs_err_t fs_set_comment_unlocked(bfs_fs_t *fs, uint32_t ino,
                                         const char *comment, uint8_t len)
{
    if (len > 79 || (len != 0 && !comment)) return BFS_ERR_INVAL;

    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) return err;
    if (ino >= 0x80000000u) return BFS_ERR_CORRUPT;

    comment_key_ctx_t old_comment;
    err = fs_find_comment_unlocked(fs, ino, &old_comment);
    if (err != BFS_OK) return err;

    err = fs_remove_comments_unlocked(fs, ino);
    if (err != BFS_OK) return err;
    if (len == 0) return BFS_OK;

    uint32_t comment_parent = ino | 0x80000000u;
    err = bfs_dir_insert(&fs->dir_tree, comment_parent, comment, len, ino, 0);
    if (err != BFS_OK && old_comment.found) {
        bfs_err_t rollback_err = bfs_dir_insert(&fs->dir_tree, comment_parent,
                                                old_comment.name,
                                                old_comment.len, ino, 0);
        return fs_cleanup_result(fs, err, rollback_err);
    }
    return err;
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
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&fs->inode_tree, ino, &inode);
    if (err != BFS_OK) return err;
    if (ino >= 0x80000000u) return BFS_ERR_CORRUPT;
    uint32_t comment_parent = ino | 0x80000000u;
    comment_ctx_t cc = { .buf = buf, .max_len = max_len, .found = false };
    err = bfs_dir_scan(&fs->dir_tree, comment_parent, comment_scan_cb, &cc);
    if (err != BFS_OK) return err;
    return cc.found ? BFS_OK : BFS_ERR_NOTFOUND;
}
bfs_err_t bfs_fs_create_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    if (!fs_handle_valid(fs) || parent_ino == 0 || !fs_name_valid(name, name_len))
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_create_file_unlocked(fs, parent_ino, name, name_len, ino_out);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_mkdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t *ino_out)
{
    if (!fs_handle_valid(fs) || parent_ino == 0 || !fs_name_valid(name, name_len))
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_mkdir_unlocked(fs, parent_ino, name, name_len, ino_out);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_delete_file(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    if (!fs_handle_valid(fs) || parent_ino == 0 || !fs_name_valid(name, name_len))
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_delete_file_unlocked(fs, parent_ino, name, name_len);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_rmdir(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len)
{
    if (!fs_handle_valid(fs) || parent_ino == 0 || !fs_name_valid(name, name_len))
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_rmdir_unlocked(fs, parent_ino, name, name_len);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_rename(bfs_fs_t *fs, uint32_t old_parent, const char *old_name, uint8_t old_len, uint32_t new_parent, const char *new_name, uint8_t new_len)
{
    if (!fs_handle_valid(fs) || old_parent == 0 || new_parent == 0 ||
        !fs_name_valid(old_name, old_len) || !fs_name_valid(new_name, new_len))
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_rename_unlocked(fs, old_parent, old_name, old_len, new_parent, new_name, new_len);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_make_hardlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, uint32_t target_ino)
{
    if (!fs_handle_valid(fs) || parent_ino == 0 || target_ino == 0 ||
        !fs_name_valid(name, name_len))
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_make_hardlink_unlocked(fs, parent_ino, name, name_len, target_ino);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_make_softlink(bfs_fs_t *fs, uint32_t parent_ino, const char *name, uint8_t name_len, const char *target_path, uint16_t path_len)
{
    if (!fs_handle_valid(fs) || parent_ino == 0 ||
        !fs_name_valid(name, name_len) || !target_path || path_len == 0)
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_make_softlink_unlocked(fs, parent_ino, name, name_len, target_path, path_len);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_set_comment(bfs_fs_t *fs, uint32_t ino, const char *comment, uint8_t len)
{
    if (!fs_handle_valid(fs) || ino == 0 ||
        (len != 0 && !comment) || len > 79)
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_ensure_free_headroom(fs, BFS_FS_OP_FREE_RESERVE);
    if (err == BFS_OK)
        err = fs_set_comment_unlocked(fs, ino, comment, len);
    err = fs_namespace_result(fs, err);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_get_comment(bfs_fs_t *fs, uint32_t ino, char *buf, uint8_t max_len)
{
    if (!fs_handle_valid(fs) || ino == 0 ||
        !buf || max_len == 0)
        return BFS_ERR_INVAL;
    bfs_lock_read(&fs->lock);
    bfs_err_t err = fs->recovery_error != BFS_OK ? fs->recovery_error :
                       fs_get_comment_unlocked(fs, ino, buf, max_len);
    bfs_lock_unlock(&fs->lock);
    return err;
}
