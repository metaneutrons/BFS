/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — B+tree node-layout accessors (core-internal, single source of truth).
 *
 * These define the on-block layout: where keys, values and child pointers sit,
 * and how the per-node CRC is computed. btree.c uses them, and the invariant
 * test includes the SAME definitions instead of mirroring them — so a layout
 * change can never leave the checker validating a stale layout.
 */
#ifndef BFS_BTREE_INTERNAL_H
#define BFS_BTREE_INTERNAL_H

#include "bfs_btree.h"
#include "bfs_ondisk.h"
#include "bfs_crc32.h"

/* ── Node capacity calculations ────────────────────────────── */

static inline uint32_t node_data_size(const bfs_btree_t *tree)
{
    return tree->bio->block_size - sizeof(bfs_btnode_hdr_t);
}

static inline uint32_t leaf_max_keys(const bfs_btree_t *tree)
{
    return node_data_size(tree) / (tree->ops->key_size + tree->ops->val_size);
}

/* Max keys in an internal node: N keys + (N+1) child pointers (4 bytes each) */
static inline uint32_t internal_max_keys(const bfs_btree_t *tree)
{
    return (node_data_size(tree) - 4) / (tree->ops->key_size + 4);
}

/* ── Node accessors ────────────────────────────────────────── */

static inline void *node_key(const bfs_btree_t *tree, uint8_t *buf, uint32_t i)
{
    return buf + sizeof(bfs_btnode_hdr_t) + i * tree->ops->key_size;
}

static inline void *leaf_val(const bfs_btree_t *tree, uint8_t *buf, uint32_t i)
{
    uint32_t keys_end = sizeof(bfs_btnode_hdr_t) + leaf_max_keys(tree) * tree->ops->key_size;
    return buf + keys_end + i * tree->ops->val_size;
}

static inline uint8_t *internal_child_ptr(const bfs_btree_t *tree, uint8_t *buf, uint32_t i)
{
    uint32_t keys_end = sizeof(bfs_btnode_hdr_t) + internal_max_keys(tree) * tree->ops->key_size;
    return buf + keys_end + i * sizeof(uint32_t);
}

static inline bfs_blk_t get_child(const bfs_btree_t *tree, uint8_t *buf, uint32_t i)
{
    return bfs_load_be32(internal_child_ptr(tree, buf, i));
}

static inline void set_child(const bfs_btree_t *tree, uint8_t *buf, uint32_t i, bfs_blk_t blk)
{
    bfs_store_be32(internal_child_ptr(tree, buf, i), blk);
}

/* ── Per-node CRC ──────────────────────────────────────────── */

static inline uint32_t node_compute_crc(const bfs_btree_t *tree, const uint8_t *buf)
{
    /* Cast away const temporarily — we restore the value */
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    uint32_t saved_crc = hdr->crc32;
    hdr->crc32 = 0;
    uint32_t crc = bfs_crc32(0, buf, tree->bio->block_size);
    hdr->crc32 = saved_crc;
    return crc;
}

/* ── Header field accessors ────────────────────────────────── */

static inline bfs_btnode_hdr_t *hdr_of(uint8_t *buf) { return (bfs_btnode_hdr_t *)buf; }
static inline uint32_t num_keys(uint8_t *buf) { return bfs_be32(hdr_of(buf)->num_keys); }
static inline uint16_t node_level(uint8_t *buf) { return bfs_be16(hdr_of(buf)->level); }
static inline bool is_leaf(uint8_t *buf) { return node_level(buf) == BFS_BTNODE_LEAF; }

#endif /* BFS_BTREE_INTERNAL_H */
