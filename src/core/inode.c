/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode B+tree operations
 */

#include "bfs_inode.h"
#include <string.h>
#ifdef BFS_PERF_PROBE
#include "perf_probe.h"
#endif
#ifdef BFS_PERF_WRITE_DETAIL
#include "../amiga/write_probe.h"
#endif

void bfs_inode_apply_stamp(bfs_inode_t *inode, const bfs_inode_stamp_t *stamp,
                           bool creation)
{
    inode->modify_days = bfs_be16(stamp->days);
    inode->modify_mins = bfs_be16(stamp->mins);
    inode->modify_ticks = bfs_be16(stamp->ticks);
    if (creation) {
        inode->create_days = inode->modify_days;
        inode->create_mins = inode->modify_mins;
        inode->create_ticks = inode->modify_ticks;
    }
}

static const bfs_btree_ops_t bfs_inode_ops = {
    .key_compare = bfs_btree_key_compare_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_inode_t),
    .cache_key_order = true,
};

bool bfs_inode_valid(const bfs_btree_t *tree, uint32_t ino,
                     const bfs_inode_t *inode)
{
    if (!tree || !tree->bio || !inode || ino == 0 || ino >= 0x80000000u ||
        bfs_be32(inode->inode_nr) != ino ||
        bfs_be32(inode->type) > BFS_INODE_HARDLINK)
        return false;
    bfs_blk_t extent_root = bfs_be32(inode->extent_root);
    if (extent_root != BFS_BLK_NULL && extent_root >= tree->bio->block_count)
        return false;
    uint32_t flags = bfs_be32(inode->flags);
    uint32_t inline_length = bfs_be32(inode->inline_length);
    if (flags & ~BFS_INODE_FLAGS_KNOWN)
        return false;
    if (bfs_be32(inode->type) == BFS_INODE_DIR &&
        (extent_root != BFS_BLK_NULL || (flags & BFS_INODE_FLAG_INLINE_EXTENT)))
        return false;
    if (flags & BFS_INODE_FLAG_INLINE_EXTENT)
        /* The extent layer checks the reserved regions; the range must at
         * least lie on the device. */
        return extent_root != BFS_BLK_NULL && inline_length != 0 &&
               inline_length <= tree->bio->block_count - extent_root;
    return inline_length == 0 && inode->inline_crc32 == 0;
}

#ifdef BFS_PERF_PROBE
static bfs_err_t validate_inode_body(const bfs_btree_t *tree, uint32_t ino,
                                     const bfs_inode_t *inode, bool unlinked);
static bfs_err_t validate_inode(const bfs_btree_t *tree, uint32_t ino,
                                const bfs_inode_t *inode, bool unlinked)
{
    bfs_perf_detail_sample_t sample = bfs_perf_probe_detail_begin(
        BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_VALIDATE);
    bfs_err_t result = validate_inode_body(tree, ino, inode, unlinked);
    bfs_perf_probe_detail_end(&sample);
    return result;
}
static bfs_err_t validate_inode_body(const bfs_btree_t *tree, uint32_t ino,
#else
static bfs_err_t validate_inode(const bfs_btree_t *tree, uint32_t ino,
#endif
                                const bfs_inode_t *inode, bool unlinked)
{
    if (!bfs_inode_valid(tree, ino, inode) ||
        (bfs_be32(inode->link_count) == 0) != unlinked)
        return BFS_ERR_INVAL;
    return BFS_OK;
}

bfs_err_t bfs_inode_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id)
{
    return bfs_btree_init(tree, bio, alloc, &bfs_inode_ops, root, txn_id);
}

/* ── Pending inodes ────────────────────────────────────────── */

static bfs_inode_pending_slot_t *pending_find(const bfs_btree_t *tree, uint32_t ino)
{
    if (!tree) return NULL;
    bfs_inode_pending_t *pending = tree->inode_pending;
    if (!pending || pending->used == 0) return NULL;
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++)
        if (pending->slot[i].ino == ino) return &pending->slot[i];
    return NULL;
}

/* Forget the pending copy of ino. */
static void pending_drop(const bfs_btree_t *tree, uint32_t ino)
{
    bfs_inode_pending_slot_t *slot = pending_find(tree, ino);
    if (slot && tree) {
        memset(slot, 0, sizeof(*slot));
        tree->inode_pending->used--;
    }
}

/* A free slot, else one whose copy equals the tree, else NULL. */
static bfs_inode_pending_slot_t *pending_take(const bfs_btree_t *tree)
{
    bfs_inode_pending_t *pending = tree->inode_pending;
    bfs_inode_pending_slot_t *clean = NULL;
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++) {
        if (pending->slot[i].ino == 0) return &pending->slot[i];
        if (!clean && !pending->slot[i].dirty) clean = &pending->slot[i];
    }
    return clean;
}

/* A write or delete through any other path supersedes the pending copy once
 * the tree holds its result: after success, or when the change was applied
 * but reclaiming its old nodes failed (free_sink_err). A failure that left
 * the tree as it was keeps the copy, which is still the newest state. The
 * second case needs nodes of an older transaction on the path, which a
 * pending inode's path does not have; it is kept for the B+tree's contract. */
static void pending_settle(const bfs_btree_t *tree, uint32_t ino, bfs_err_t err)
{
    if (tree && (err == BFS_OK || tree->free_sink_err != BFS_OK)) pending_drop(tree, ino);
}

const bfs_inode_t *bfs_inode_pending_peek(const bfs_btree_t *tree, uint32_t ino)
{
    if (!tree || ino == 0) return NULL;
    const bfs_inode_pending_slot_t *slot = pending_find(tree, ino);
    return slot ? &slot->inode : NULL;
}

void bfs_inode_pending_attach(bfs_btree_t *tree, bfs_inode_pending_t *pending)
{
    if (!tree) return;
    if (pending) memset(pending, 0, sizeof(*pending));
    tree->inode_pending = pending;
}

void bfs_inode_pending_discard(bfs_btree_t *tree)
{
    if (tree && tree->inode_pending)
        memset(tree->inode_pending, 0, sizeof(*tree->inode_pending));
}

#ifdef BFS_PERF_PROBE
static bfs_err_t inode_read_body(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out);
bfs_err_t bfs_inode_read(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
{
#ifdef BFS_PERF_WRITE_DETAIL
    bfs_write_probe_sample_t write_sample = bfs_write_probe_begin(
        BFS_WRITE_PROBE_INODE_READ);
#endif
    bfs_perf_detail_sample_t sample = bfs_perf_probe_detail_begin(
        BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_READ);
    bfs_err_t result = inode_read_body(tree, ino, out);
    bfs_perf_probe_detail_end(&sample);
#ifdef BFS_PERF_WRITE_DETAIL
    bfs_write_probe_end(&write_sample);
#endif
    return result;
}
static bfs_err_t inode_read_body(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
#else
bfs_err_t bfs_inode_read(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
#endif
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.inode_read_calls++;
#endif
    if (!tree || !out || ino == 0 || ino >= 0x80000000u)
        return BFS_ERR_INVAL;
    const bfs_inode_pending_slot_t *slot = pending_find(tree, ino);
    if (slot) {
        *out = slot->inode;
    } else {
        uint32_t key = bfs_be32(ino);
        bfs_err_t err = bfs_btree_search(tree, &key, out);
        if (err != BFS_OK) return err;
    }
    if (validate_inode(tree, ino, out, false) != BFS_OK)
        return BFS_ERR_CORRUPT;
    return BFS_OK;
}

/* One element of bfs_inode_read_sorted: the same checks and result as
 * bfs_inode_read, searched through the shared leaf view. */
#ifdef BFS_PERF_PROBE
static bfs_err_t sorted_read_body(bfs_btree_t *tree, bfs_btree_sorted_t *state,
                                  uint32_t ino, bfs_inode_t *out);
static bfs_err_t sorted_read(bfs_btree_t *tree, bfs_btree_sorted_t *state,
                             uint32_t ino, bfs_inode_t *out)
{
    bfs_perf_detail_sample_t sample = bfs_perf_probe_detail_begin(
        BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_READ);
    bfs_err_t result = sorted_read_body(tree, state, ino, out);
    bfs_perf_probe_detail_end(&sample);
    return result;
}
static bfs_err_t sorted_read_body(bfs_btree_t *tree, bfs_btree_sorted_t *state,
                                  uint32_t ino, bfs_inode_t *out)
#else
static bfs_err_t sorted_read(bfs_btree_t *tree, bfs_btree_sorted_t *state,
                             uint32_t ino, bfs_inode_t *out)
#endif
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.inode_read_calls++;
#endif
    if (ino == 0 || ino >= 0x80000000u) return BFS_ERR_INVAL;
    const bfs_inode_pending_slot_t *slot = pending_find(tree, ino);
    if (slot) {
        *out = slot->inode;
    } else {
        uint32_t key = bfs_be32(ino);
        bfs_err_t err = bfs_btree_sorted_search(tree, state, &key, out);
        if (err != BFS_OK) return err;
    }
    if (validate_inode(tree, ino, out, false) != BFS_OK) return BFS_ERR_CORRUPT;
    return BFS_OK;
}

bfs_err_t bfs_inode_read_sorted(bfs_btree_t *tree, const uint32_t *inos, uint32_t count,
                                bfs_inode_t *out, bfs_err_t *results)
{
    if (!tree || (count && (!inos || !out || !results))) return BFS_ERR_INVAL;
    for (uint32_t i = 1; i < count; i++)
        if (inos[i] < inos[i - 1]) return BFS_ERR_INVAL;
    bfs_btree_sorted_t state;
    bfs_err_t err = bfs_btree_sorted_begin(tree, &state);
    if (err != BFS_OK) return err;
    for (uint32_t i = 0; i < count; i++)
        results[i] = sorted_read(tree, &state, inos[i], &out[i]);
    bfs_btree_sorted_end(tree, &state);
    return BFS_OK;
}

#ifdef BFS_PERF_PROBE
static bfs_err_t inode_read_unlinked_body(bfs_btree_t *tree, uint32_t ino,
                                         bfs_inode_t *out);
bfs_err_t bfs_inode_read_unlinked(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
{
#ifdef BFS_PERF_WRITE_DETAIL
    bfs_write_probe_sample_t write_sample = bfs_write_probe_begin(
        BFS_WRITE_PROBE_INODE_READ);
#endif
    bfs_perf_detail_sample_t sample = bfs_perf_probe_detail_begin(
        BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_READ);
    bfs_err_t result = inode_read_unlinked_body(tree, ino, out);
    bfs_perf_probe_detail_end(&sample);
#ifdef BFS_PERF_WRITE_DETAIL
    bfs_write_probe_end(&write_sample);
#endif
    return result;
}
static bfs_err_t inode_read_unlinked_body(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
#else
bfs_err_t bfs_inode_read_unlinked(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
#endif
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.inode_read_calls++;
#endif
    if (!tree || !out || ino == 0 || ino >= 0x80000000u)
        return BFS_ERR_INVAL;
    const bfs_inode_pending_slot_t *slot = pending_find(tree, ino);
    if (slot) {
        *out = slot->inode;
    } else {
        uint32_t key = bfs_be32(ino);
        bfs_err_t err = bfs_btree_search(tree, &key, out);
        if (err != BFS_OK) return err;
    }
    return validate_inode(tree, ino, out, true) == BFS_OK ? BFS_OK : BFS_ERR_CORRUPT;
}

#ifdef BFS_PERF_WRITE_DETAIL
static bfs_err_t inode_write_body(bfs_btree_t *tree, uint32_t ino,
                                  const bfs_inode_t *inode);
bfs_err_t bfs_inode_write(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode)
{
    bfs_write_probe_sample_t sample = bfs_write_probe_begin(
        BFS_WRITE_PROBE_INODE_WRITE);
    bfs_err_t result = inode_write_body(tree, ino, inode);
    bfs_write_probe_end(&sample);
    return result;
}
static bfs_err_t inode_write_body(bfs_btree_t *tree, uint32_t ino,
                                  const bfs_inode_t *inode)
#else
bfs_err_t bfs_inode_write(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode)
#endif
{
    if (validate_inode(tree, ino, inode, false) != BFS_OK) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);

    /* Try update (single traversal for existing inodes) */
    bfs_err_t err = bfs_btree_update(tree, &key, inode);
    if (err == BFS_ERR_NOTFOUND)
        err = bfs_btree_insert(tree, &key, inode);
    pending_settle(tree, ino, err);
    return err;
}

#ifdef BFS_PERF_WRITE_DETAIL
static bfs_err_t inode_write_unlinked_body(bfs_btree_t *tree, uint32_t ino,
                                           const bfs_inode_t *inode);
bfs_err_t bfs_inode_write_unlinked(bfs_btree_t *tree, uint32_t ino,
                                   const bfs_inode_t *inode)
{
    bfs_write_probe_sample_t sample = bfs_write_probe_begin(
        BFS_WRITE_PROBE_INODE_WRITE);
    bfs_err_t result = inode_write_unlinked_body(tree, ino, inode);
    bfs_write_probe_end(&sample);
    return result;
}
static bfs_err_t inode_write_unlinked_body(bfs_btree_t *tree, uint32_t ino,
                                           const bfs_inode_t *inode)
#else
bfs_err_t bfs_inode_write_unlinked(bfs_btree_t *tree, uint32_t ino,
                                   const bfs_inode_t *inode)
#endif
{
    if (validate_inode(tree, ino, inode, true) != BFS_OK) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);
    bfs_err_t err = bfs_btree_update(tree, &key, inode);
    pending_settle(tree, ino, err);
    return err;
}

bfs_err_t bfs_inode_delete(bfs_btree_t *tree, uint32_t ino)
{
    if (!tree || ino == 0 || ino >= 0x80000000u)
        return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);
    bfs_err_t err = bfs_btree_delete(tree, &key);
    pending_settle(tree, ino, err);
    return err;
}

bfs_err_t bfs_inode_publish(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode)
{
    if (!tree || !tree->inode_pending) return bfs_inode_write(tree, ino, inode);
    if (!inode || validate_inode(tree, ino, inode, false) != BFS_OK) return BFS_ERR_INVAL;
    uint64_t txn_id = bfs_btree_txn_id(tree);
    bfs_inode_pending_slot_t *slot = pending_find(tree, ino);
    if (slot && slot->txn_id == txn_id) {
        slot->inode = *inode;
        slot->dirty = true;
        return BFS_OK;
    }
    /* First publication in this transaction: write the tree, so that the leaf
     * belongs to the transaction and later copies can be written in place. */
    bfs_err_t err = bfs_inode_write(tree, ino, inode);
    if (err != BFS_OK) return err;
    slot = pending_take(tree);
    if (slot) {
        if (slot->ino == 0) tree->inode_pending->used++;
        slot->ino = ino;
        slot->dirty = false;
        slot->txn_id = txn_id;
        slot->inode = *inode;
    }
    return BFS_OK;
}

bfs_err_t bfs_inode_flush_pending(bfs_btree_t *tree)
{
    if (!tree || !tree->inode_pending) return BFS_OK;
    bfs_err_t result = BFS_OK;
    if (tree->inode_pending->used == 0) return BFS_OK;
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++) {
        bfs_inode_pending_slot_t slot = tree->inode_pending->slot[i];
        memset(&tree->inode_pending->slot[i], 0, sizeof(slot));
        if (slot.ino != 0) tree->inode_pending->used--;
        if (slot.ino == 0 || !slot.dirty || result != BFS_OK) continue;
        /* The copy's path was written in this transaction, so the update
         * needs no deferred-free headroom (bfs_btree_update_path_headroom). */
        uint32_t key = bfs_be32(slot.ino);
#ifdef BFS_PERF_WRITE_DETAIL
        bfs_write_probe_sample_t sample = bfs_write_probe_begin(
            BFS_WRITE_PROBE_INODE_WRITE);
#endif
        result = validate_inode(tree, slot.ino, &slot.inode, false) == BFS_OK
                     ? bfs_btree_update_path_headroom(tree, &key, &slot.inode)
                     : BFS_ERR_INVAL;
#ifdef BFS_PERF_WRITE_DETAIL
        bfs_write_probe_end(&sample);
#endif
    }
    return result;
}
