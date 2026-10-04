/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — B+tree engine
 *
 * Node layout (within a single disk block):
 *
 *   [bfs_btnode_hdr_t]  (28 bytes)
 *   [key0][key1]...[keyN-1]
 *   -- leaf:     [val0][val1]...[valN-1]
 *   -- internal: [child0][child1]...[childN]  (N+1 child pointers, uint32_t each)
 *
 * Internal nodes: key[i] is the separator. child[i] contains keys < key[i],
 *                 child[i+1] contains keys >= key[i].
 *                 So N keys → N+1 children.
 *
 * Leaf nodes: key[i]/val[i] are the actual data. right_sibling is a legacy
 * hint; traversal uses parent/child links because COW can leave it stale.
 *
 * COW: on modification, allocate a new block, copy+modify, update parent.
 *      Old blocks are freed after transaction commit.
 */

#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_crc32.h"
#include <string.h>
#include <stdlib.h>
#ifdef BFS_PERF_PROBE
#include "perf_probe.h"
#include "bfs_fs.h"
/* Instrument only this translation unit's direct heap calls. Production
 * preprocessing and the shared allocation/ownership logic are unchanged. */
#define malloc(size) bfs_perf_probe_btree_malloc(size)
#define free(pointer) bfs_perf_probe_btree_free(pointer)
#endif

#define MAX_TREE_DEPTH BFS_BTREE_MAX_DEPTH

static uint8_t *alloc_buf(const bfs_btree_t *tree)
{
    return bfs_bio_alloc_buffer(tree->bio, tree->bio->block_size);
}

static void free_buf(const bfs_btree_t *tree, void *buffer)
{
    bfs_bio_free_buffer(tree->bio, buffer);
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) destination[index] = source[index];
}

static bool tree_shape_valid(const bfs_btree_t *tree)
{
    if (!tree || !tree->bio) return false;
    if (tree->root == BFS_BLK_NULL) return tree->height == 0;
    return tree->root < tree->bio->block_count && tree->height > 0 &&
           tree->height <= MAX_TREE_DEPTH;
}

static bfs_err_t mutation_headroom(const bfs_btree_t *tree, uint32_t blocks)
{
    if (!tree->free_sink.defer || blocks == 0) return BFS_OK;
    if (!tree->free_sink.headroom || tree->free_sink.capacity == 0)
        return BFS_ERR_INVAL;
    if (blocks > tree->free_sink.capacity)
        return BFS_ERR_NOSPC;
    return blocks <= tree->free_sink.headroom(tree->free_sink.ctx)
               ? BFS_OK : BFS_ERR_AGAIN;
}

/* Node layout/capacity/CRC accessors live in bfs_btree_internal.h — shared with
 * the invariant test (tests/test_invariants.c) so it validates the real layout,
 * not a hand-kept copy. */

/* ── Node I/O ──────────────────────────────────────────────── */

static uint32_t node_compute_read_crc(const bfs_btree_t *tree, uint8_t *buf);

static bfs_node_validation_t node_validation_context(const bfs_btree_t *tree)
{
    bfs_node_validation_t validation = {
        .key_compare = tree->ops->key_compare,
        .key_size = tree->ops->key_size,
        .val_size = tree->ops->val_size,
        .block_size = tree->bio->block_size,
        .block_count = tree->bio->block_count,
    };
    return validation;
}

/* Node-local structure: header fields, strictly ascending keys and in-range
 * children. Parent bounds and the expected level are checked by traversals. */
static bool node_structure_ok(const bfs_btree_t *tree, uint8_t *buf)
{
    const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
    uint16_t level = bfs_be16(hdr->level);
    uint32_t nkeys = bfs_be32(hdr->num_keys);
    uint32_t max_keys = (level == BFS_BTNODE_LEAF)
                        ? leaf_max_keys(tree) : internal_max_keys(tree);
    if (level >= MAX_TREE_DEPTH || nkeys == 0 || nkeys > max_keys ||
        bfs_be16(hdr->flags) != 0)
        return false;
    bfs_blk_t sibling = bfs_be32(hdr->right_sibling);
    if (sibling != BFS_BLK_NULL &&
        (level != BFS_BTNODE_LEAF || sibling >= tree->bio->block_count))
        return false;
    for (uint32_t i = 1; i < nkeys; i++) {
        if (tree->ops->key_compare(node_key(tree, buf, i - 1),
                                   node_key(tree, buf, i)) >= 0)
            return false;
    }
    if (level != BFS_BTNODE_LEAF) {
        for (uint32_t i = 0; i <= nkeys; i++) {
            bfs_blk_t child = get_child(tree, buf, i);
            if (child == BFS_BLK_NULL || child >= tree->bio->block_count)
                return false;
        }
    }
    return true;
}

static bfs_err_t node_read(const bfs_btree_t *tree, bfs_blk_t blk, uint8_t *buf)
{
    if (blk == BFS_BLK_NULL || blk >= tree->bio->block_count)
        return BFS_ERR_CORRUPT;
    bfs_err_t err = bfs_bio_read(tree->bio, blk, buf);
    if (err != BFS_OK) return err;

    bfs_node_validation_t validation = node_validation_context(tree);
    if (tree->ops->cache_key_order &&
        bfs_bio_node_structure_valid(tree->bio, blk, &validation))
        return BFS_OK;

    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    if (bfs_be32(hdr->magic) != BFS_NODE_MAGIC)
        return BFS_ERR_CORRUPT;
    if (!bfs_bio_node_crc_valid(tree->bio, blk)) {
#ifdef BFS_PERF_PROBE
        struct EClockVal crc_started = {0};
        ULONG crc_call = ++bfs_perf_probe_counters.node_crc_read_calls;
        BOOL sample_crc = (crc_call % BFS_PERF_CRC_SAMPLE_STRIDE) == 0;
        if (sample_crc) bfs_perf_probe_begin(&crc_started);
        uint32_t computed_crc = node_compute_read_crc(tree, buf);
        if (sample_crc) {
            bfs_perf_probe_counters.node_crc_read_samples++;
            bfs_perf_probe_counters.node_crc_read_sample_ticks +=
                bfs_perf_probe_elapsed(&crc_started);
        }
        if (bfs_be32(hdr->crc32) != computed_crc) return BFS_ERR_CORRUPT;
#else
        if (bfs_be32(hdr->crc32) != node_compute_read_crc(tree, buf))
            return BFS_ERR_CORRUPT;
#endif
    }

    /* Validate structural header fields read from disk before any accessor uses
     * num_keys to index into the fixed-size block buffer. The CRC only catches
     * accidental bit-rot, not a deliberately-consistent corrupt node crafted on
     * untrusted media. */
    if (!node_structure_ok(tree, buf)) return BFS_ERR_CORRUPT;
    bfs_bio_mark_node_crc_valid(tree->bio, blk);
    if (tree->ops->cache_key_order)
        bfs_bio_mark_node_structure_valid(tree->bio, blk, &validation);
    return BFS_OK;
}

/* Byte ranges of a node: used keys [0, prefix_end), unused key slots up to
 * values_start, used values or children of values_length bytes, and unused
 * value slots up to the end of the block. False if the header's level or key
 * count cannot describe a node of this tree. */
typedef struct {
    uint32_t prefix_end;
    uint32_t values_start;
    uint32_t values_length;
} node_ranges_t;

static bool node_ranges(const bfs_btree_t *tree, uint8_t *buf, node_ranges_t *out)
{
    uint32_t count = num_keys(buf);
    uint16_t level = node_level(buf);
    bool leaf = level == BFS_BTNODE_LEAF;
    uint32_t capacity = leaf ? leaf_max_keys(tree) : internal_max_keys(tree);
    if (level >= MAX_TREE_DEPTH || count > capacity) return false;
    uint32_t prefix_end = (uint8_t *)node_key(tree, buf, count) - buf;
    uint8_t *values = leaf ? leaf_val(tree, buf, 0)
                          : internal_child_ptr(tree, buf, 0);
    uint32_t values_start = values - buf;
    uint32_t values_length = leaf ? count * tree->ops->val_size
                                  : (count + 1u) * sizeof(uint32_t);
    uint32_t block_size = tree->bio->block_size;
    if (prefix_end > values_start || values_start > block_size ||
        values_length > block_size - values_start)
        return false;
    out->prefix_end = prefix_end;
    out->values_start = values_start;
    out->values_length = values_length;
    return true;
}

#if defined(__GNUC__)
/* A word type that may alias the byte buffer, so the zero test compiles to
 * plain long-word loads instead of copies through the stack. */
typedef uint32_t __attribute__((__may_alias__, __aligned__(1))) bfs_any_word_t;
#endif

/* True if all length bytes at p are zero. Whole words are tested where
 * possible; on 68k that is far cheaper than the CRC table steps it saves. */
static bool bytes_are_zero(const uint8_t *p, uint32_t length)
{
    for (; length >= 16; p += 16, length -= 16) {
#if defined(__GNUC__)
        const bfs_any_word_t *w = (const bfs_any_word_t *)(const void *)p;
        if ((w[0] | w[1] | w[2] | w[3]) != 0) return false;
#else
        uint32_t w[4];
        memcpy(w, p, sizeof(w)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        if ((w[0] | w[1] | w[2] | w[3]) != 0) return false;
#endif
    }
    while (length > 0) {
        if (*p++ != 0) return false;
        length--;
    }
    return true;
}

/* Advance crc over length bytes at p. Zero bytes only shift the CRC state, so
 * an all-zero range uses the zero-run step instead of hashing every byte; the
 * result is identical either way. */
static uint32_t crc_range(uint32_t crc, const uint8_t *p, uint32_t length)
{
    return bytes_are_zero(p, length) ? bfs_crc32_zeros(crc, length)
                                     : bfs_crc32(crc, p, length);
}

/* The on-disk full-block CRC of a node read from disk, bit-identical to
 * node_compute_crc. Unused slots of nodes written by this version are zero,
 * so only the used ranges are hashed; legacy non-zero padding and headers
 * that describe no valid layout are hashed in full. The header only selects
 * how bytes are hashed, never which bytes are covered. */
static uint32_t node_compute_read_crc(const bfs_btree_t *tree, uint8_t *buf)
{
    node_ranges_t r;
    if (!node_ranges(tree, buf, &r)) return node_compute_crc(tree, buf);
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    uint32_t saved_crc = hdr->crc32;
    hdr->crc32 = 0;
    uint32_t values_end = r.values_start + r.values_length;
    uint32_t crc = bfs_crc32(0, buf, r.prefix_end);
    crc = crc_range(crc, buf + r.prefix_end, r.values_start - r.prefix_end);
    crc = bfs_crc32(crc, buf + r.values_start, r.values_length);
    crc = crc_range(crc, buf + values_end, tree->bio->block_size - values_end);
    hdr->crc32 = saved_crc;
    return crc;
}

/* Unused slots have no semantic meaning, but remain covered by the on-disk
 * full-block CRC. Canonicalize them on writes and advance the exact CRC state
 * over their zero bytes. */
static bfs_err_t node_compute_write_crc(const bfs_btree_t *tree, uint8_t *buf,
                                         uint32_t *crc_out)
{
    node_ranges_t r;
    if (!node_ranges(tree, buf, &r)) return BFS_ERR_CORRUPT;
    uint32_t gap = r.values_start - r.prefix_end;
    uint32_t values_end = r.values_start + r.values_length;
    uint32_t tail = tree->bio->block_size - values_end;
    memset(buf + r.prefix_end, 0, gap);
    memset(buf + values_end, 0, tail);
    uint32_t crc = bfs_crc32(0, buf, r.prefix_end);
    crc = bfs_crc32_zeros(crc, gap);
    crc = bfs_crc32(crc, buf + r.values_start, r.values_length);
    *crc_out = bfs_crc32_zeros(crc, tail);
    return BFS_OK;
}

static bool owned_contains(const bfs_btree_t *tree, bfs_blk_t blk);

/* Completes a deferred node image: canonical padding and the full-block CRC,
 * exactly as a direct write would have produced them. */
static bfs_err_t node_finalize_deferred(const void *layout, uint32_t block_size,
                                        uint8_t *buf)
{
    bfs_bio_t geometry = { .block_size = block_size };
    bfs_btree_t tree = { .bio = &geometry, .ops = layout };
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->crc32 = 0;
    uint32_t crc;
    bfs_err_t err = node_compute_write_crc(&tree, buf, &crc);
    if (err != BFS_OK) return err;
    hdr->crc32 = bfs_be32(crc);
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.node_crc_write_calls++;
    bfs_perf_probe_counters.btree_node_writes++;
#endif
    return BFS_OK;
}

static bfs_err_t node_write(bfs_btree_t *tree, bfs_blk_t blk, uint8_t *buf)
{
    if (blk == BFS_BLK_NULL || blk >= tree->bio->block_count)
        return BFS_ERR_CORRUPT;
    tree->generation++;
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->magic = bfs_be32(BFS_NODE_MAGIC);
    hdr->txn_id = bfs_be64(bfs_btree_txn_id(tree));
    hdr->crc32 = 0;
    /* A node the live transaction owns is unreferenced by committed state, so
     * its bytes may stay in the cache until the commit flushes them. Later
     * changes in the same transaction then cost neither a CRC nor a write. */
    if (bfs_bio_can_defer_nodes(tree->bio) && owned_contains(tree, blk)) {
        bfs_err_t err = bfs_bio_defer_node(tree->bio, blk, buf,
                                           node_finalize_deferred, tree->ops);
        /* The resident image is exactly these bytes: validate them now, as a
         * read would, so later searches can use the node in place. */
        if (err == BFS_OK && tree->ops->cache_key_order && node_structure_ok(tree, buf)) {
            bfs_node_validation_t validation = node_validation_context(tree);
            bfs_bio_mark_node_structure_valid(tree->bio, blk, &validation);
        }
        if (err != BFS_ERR_UNSUPPORTED) return err;
    }
#ifdef BFS_PERF_PROBE
    struct EClockVal crc_started = {0};
    ULONG crc_call = ++bfs_perf_probe_counters.node_crc_write_calls;
    BOOL sample_crc = (crc_call % BFS_PERF_CRC_SAMPLE_STRIDE) == 0;
    if (sample_crc) bfs_perf_probe_begin(&crc_started);
#endif
    uint32_t crc;
    bfs_err_t crc_err = node_compute_write_crc(tree, buf, &crc);
    if (crc_err != BFS_OK) return crc_err;
    hdr->crc32 = bfs_be32(crc);
#ifdef BFS_PERF_PROBE
    if (sample_crc) {
        bfs_perf_probe_counters.node_crc_write_samples++;
        bfs_perf_probe_counters.node_crc_write_sample_ticks +=
            bfs_perf_probe_elapsed(&crc_started);
    }
    bfs_perf_probe_counters.btree_node_writes++;
    if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->freespace.tree) {
        bfs_perf_probe_counters.free_tree_node_writes++;
        switch (bfs_perf_probe_free_tree_phase) {
        case BFS_PERF_FREE_TREE_PHASE_ALLOCATION_BODY:
            bfs_perf_probe_counters.free_tree_allocation_body_node_writes++;
            break;
        case BFS_PERF_FREE_TREE_PHASE_RESERVE_REFILL:
            bfs_perf_probe_counters.free_tree_reserve_refill_node_writes++;
            break;
        case BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN:
            bfs_perf_probe_counters.free_tree_reserve_return_node_writes++;
            break;
        case BFS_PERF_FREE_TREE_PHASE_POST_PUBLISH_PENDING_RECLAIM:
            bfs_perf_probe_counters.free_tree_post_publish_pending_reclaim_node_writes++;
            break;
        case BFS_PERF_FREE_TREE_PHASE_OTHER:
        default:
            bfs_perf_probe_counters.free_tree_other_node_writes++;
            break;
        }
    } else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->dir_tree.tree)
        bfs_perf_probe_counters.dir_tree_node_writes++;
    else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->inode_tree)
        bfs_perf_probe_counters.inode_tree_node_writes++;
    else if (bfs_perf_probe_fs && tree == &bfs_perf_probe_fs->refcount.tree)
        bfs_perf_probe_counters.refcount_tree_node_writes++;
    else
        bfs_perf_probe_counters.other_tree_node_writes++;
#endif
    return bfs_bio_write_node(tree->bio, blk, buf);
}

static bfs_err_t node_read_at_level(const bfs_btree_t *tree, bfs_blk_t blk,
                                    uint8_t *buf, uint16_t expected_level)
{
    bfs_err_t err = node_read(tree, blk, buf);
    if (err != BFS_OK) return err;
    return node_level(buf) == expected_level ? BFS_OK : BFS_ERR_CORRUPT;
}

typedef struct {
    uint8_t lower[BFS_MAX_KEY_SIZE];
    uint8_t upper[BFS_MAX_KEY_SIZE];
    bool have_lower;
    bool have_upper;
} node_bounds_t;

static void child_bounds(const bfs_btree_t *tree, uint8_t *parent,
                          uint32_t child, node_bounds_t *bounds)
{
    if (child > 0) {
        /* tree_shape_valid bounds key_size by these BFS_MAX_KEY_SIZE arrays. */
        memcpy(bounds->lower, node_key(tree, parent, child - 1), tree->ops->key_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        bounds->have_lower = true;
    }
    if (child < num_keys(parent)) {
        memcpy(bounds->upper, node_key(tree, parent, child), tree->ops->key_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        bounds->have_upper = true;
    }
}

static bfs_err_t node_read_bounded(const bfs_btree_t *tree, bfs_blk_t blk,
                                   uint8_t *buf, uint16_t level,
                                   const node_bounds_t *bounds)
{
    bfs_err_t err = node_read_at_level(tree, blk, buf, level);
    if (err != BFS_OK) return err;
    if ((bounds->have_lower && tree->ops->key_compare(
             node_key(tree, buf, 0), bounds->lower) < 0) ||
        (bounds->have_upper && tree->ops->key_compare(
             node_key(tree, buf, num_keys(buf) - 1), bounds->upper) >= 0))
        return BFS_ERR_CORRUPT;
    return BFS_OK;
}

/* Read-only view of a node for searches: a resident node that the cache has
 * already validated is used in place, anything else is read into buf. The
 * level and parent bounds are checked on every visit either way. A resident
 * view is valid only until the next BIO call and must not be modified. */
static bfs_err_t node_view(const bfs_btree_t *tree, bfs_blk_t blk, uint8_t *buf,
                           uint16_t level, const node_bounds_t *bounds,
                           uint8_t **node)
{
    *node = buf;
    if (blk == BFS_BLK_NULL || blk >= tree->bio->block_count)
        return BFS_ERR_CORRUPT;
    const void *resident = NULL;
    if (tree->ops->cache_key_order) {
        bfs_node_validation_t validation = node_validation_context(tree);
        resident = bfs_bio_peek_valid_node(tree->bio, blk, &validation);
    }
    if (!resident)
        return bounds ? node_read_bounded(tree, blk, buf, level, bounds)
                      : node_read_at_level(tree, blk, buf, level);
    *node = (uint8_t *)resident;
    if (node_level(*node) != level) return BFS_ERR_CORRUPT;
    if (bounds &&
        ((bounds->have_lower && tree->ops->key_compare(
              node_key(tree, *node, 0), bounds->lower) < 0) ||
         (bounds->have_upper && tree->ops->key_compare(
              node_key(tree, *node, num_keys(*node) - 1), bounds->upper) >= 0)))
        return BFS_ERR_CORRUPT;
    return BFS_OK;
}

static void node_init(const bfs_btree_t *tree, uint8_t *buf, uint16_t level)
{
    memset(buf, 0, tree->bio->block_size);
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->magic = bfs_be32(BFS_NODE_MAGIC);
    hdr->level = bfs_be16(level);
    hdr->num_keys = 0;
    hdr->right_sibling = 0;
}

/* hdr_of / num_keys / node_level / is_leaf are in bfs_btree_internal.h. */

/* ── Binary search within a node ───────────────────────────── */

int bfs_btree_key_compare_be32(const void *a, const void *b)
{
    return bfs_cmp_be32(a, b);
}

static uint32_t node_search(const bfs_btree_t *tree, uint8_t *buf,
                            const void *search_key, bool *found)
{
    uint32_t lo = 0, hi = num_keys(buf);
    *found = false;
    if (tree->ops->key_compare == bfs_btree_key_compare_be32) {
        /* The same order as the comparator, without a call per step. */
        uint32_t wanted = bfs_load_be32(search_key);
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            uint32_t key = bfs_load_be32(node_key(tree, buf, mid));
            if (key < wanted)
                lo = mid + 1;
            else if (key > wanted)
                hi = mid;
            else {
                *found = true;
                return mid;
            }
        }
        return lo;
    }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = tree->ops->key_compare(node_key(tree, buf, mid), search_key);
        if (cmp < 0)
            lo = mid + 1;
        else if (cmp > 0)
            hi = mid;
        else {
            *found = true;
            return mid;
        }
    }
    return lo;
}

/* ── Public: init and search ───────────────────────────────── */

bfs_err_t bfs_btree_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, const bfs_btree_ops_t *ops,
                     bfs_blk_t root, uint64_t txn_id)
{
    if (!tree || !bio || !bio->ops || !bio->ops->read_block ||
        !alloc || !alloc->alloc || !alloc->dealloc ||
        !ops || !ops->key_compare || ops->key_size == 0 ||
        ops->key_size > BFS_MAX_KEY_SIZE || ops->val_size == 0 ||
        !bfs_block_size_valid(bio->block_size) || bio->block_count == 0 ||
        (root != BFS_BLK_NULL && root >= bio->block_count))
        return BFS_ERR_INVAL;
    tree->bio = bio;
    tree->alloc = alloc;
    tree->ops = ops;
    tree->root = root;
    tree->height = 0;
    tree->txn_id_ptr = NULL;
    tree->txn_id_fallback = txn_id;
    tree->free_sink = (bfs_free_sink_t){0};
    tree->free_sink_err = BFS_OK;
    tree->generation = 0;
    tree->hint_leaf = BFS_BLK_NULL;
    tree->hint_generation = 0;

    if (leaf_max_keys(tree) < 3 || internal_max_keys(tree) < 3)
        return BFS_ERR_INVAL;

    if (root != BFS_BLK_NULL) {
        if (ops->cache_key_order) {
            bfs_node_validation_t validation = node_validation_context(tree);
            const uint8_t *resident = bfs_bio_peek_valid_node(bio, root, &validation);
            if (resident) {
                tree->height = node_level((uint8_t *)resident) + 1;
                return BFS_OK;
            }
        }
        uint8_t *buf = alloc_buf(tree);
        if (!buf) return BFS_ERR_NOMEM;
        bfs_err_t err = node_read(tree, root, buf);
        if (err != BFS_OK) { free_buf(tree, buf); return err; }
        tree->height = node_level(buf) + 1;
        free_buf(tree, buf);
    }
    return BFS_OK;
}

bfs_err_t bfs_btree_lower_bound(bfs_btree_t *tree, const void *key, void *key_out)
{
    if (!tree || !tree->bio || !tree->ops || !key || !key_out)
        return BFS_ERR_INVAL;
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_blk_t blk = tree->root;
    uint32_t depth = 0;
    uint16_t expected_level = (uint16_t)(tree->height - 1);
    node_bounds_t bounds = {0};
    bfs_err_t result;

    while (1) {
        if (depth++ >= MAX_TREE_DEPTH) { result = BFS_ERR_CORRUPT; break; }
        uint8_t *node;
        bfs_err_t err = node_view(tree, blk, buf, expected_level, &bounds, &node);
        if (err != BFS_OK) { result = err; break; }
        bool found;
        uint32_t idx = node_search(tree, node, key, &found);
        if (is_leaf(node)) {
            if (idx < num_keys(node)) {
                memcpy(key_out, node_key(tree, node, idx), tree->ops->key_size);
                result = BFS_OK;
            } else if (bounds.have_upper) {
                /* Every greater key lives right of this leaf: at or above the
                 * parent separator that bounds it. */
                memcpy(key_out, bounds.upper, tree->ops->key_size);
                result = BFS_ERR_AGAIN;
            } else {
                result = BFS_ERR_NOTFOUND;
            }
            break;
        }
        if (expected_level == 0) { result = BFS_ERR_CORRUPT; break; }
        uint32_t child = found ? idx + 1 : idx;
        child_bounds(tree, node, child, &bounds);
        blk = get_child(tree, node, child);
        expected_level--;
    }
    free_buf(tree, buf);
    return result;
}

/* The leaf of the last search, if the tree has not changed since, it is
 * still resident and validated, and key lies between its first and last
 * keys: in a B+tree every key of that range is in that leaf. */
static uint8_t *hinted_leaf(const bfs_btree_t *tree, const void *key)
{
    if (tree->hint_leaf == BFS_BLK_NULL || tree->hint_generation != tree->generation ||
        !tree->ops->cache_key_order)
        return NULL;
    bfs_node_validation_t validation = node_validation_context(tree);
    /* Read-only, like the resident views of node_view. */
    uint8_t *leaf = (uint8_t *)bfs_bio_peek_valid_node(tree->bio, tree->hint_leaf, &validation);
    if (!leaf || node_level(leaf) != BFS_BTNODE_LEAF) return NULL;
    uint32_t n = num_keys(leaf);
    if (n == 0 || tree->ops->key_compare(node_key(tree, leaf, 0), key) > 0 ||
        tree->ops->key_compare(key, node_key(tree, leaf, n - 1)) > 0)
        return NULL;
    return leaf;
}

bfs_err_t bfs_btree_search(bfs_btree_t *tree, const void *key, void *val_out)
{
    if (!tree || !tree->bio || !tree->ops || !key || !val_out)
        return BFS_ERR_INVAL;
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;

    uint8_t *hinted = hinted_leaf(tree, key);
    if (hinted) {
        bool found;
        uint32_t idx = node_search(tree, hinted, key, &found);
        if (!found) return BFS_ERR_NOTFOUND;
        memcpy(val_out, leaf_val(tree, hinted, idx), tree->ops->val_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        return BFS_OK;
    }

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_blk_t blk = tree->root;
    uint32_t depth = 0;
    uint16_t expected_level = (uint16_t)(tree->height - 1);
    node_bounds_t bounds = {0};

    while (1) {
        if (depth++ >= MAX_TREE_DEPTH) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
        uint8_t *node;
        bfs_err_t err = node_view(tree, blk, buf, expected_level, &bounds, &node);
        if (err != BFS_OK) { free_buf(tree, buf); return err; }

        bool found;
        uint32_t idx = node_search(tree, node, key, &found);

        if (is_leaf(node)) {
            tree->hint_leaf = blk;
            tree->hint_generation = tree->generation;
            if (!found) { free_buf(tree, buf); return BFS_ERR_NOTFOUND; }
            memcpy(val_out, leaf_val(tree, node, idx), tree->ops->val_size);
            free_buf(tree, buf);
            return BFS_OK;
        }
        if (expected_level == 0) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
        /* Internal: child[idx] has keys < key[idx], child[idx+1] has keys >= key[idx] */
        uint32_t child = found ? idx + 1 : idx;
        child_bounds(tree, node, child, &bounds);
        blk = get_child(tree, node, child);
        expected_level--;
    }
}

/* ── Live-transaction node ownership ───────────────────────── */

#define OWNED_REMOVED UINT32_MAX
#define OWNED_MIN_CAPACITY 64u

void bfs_btree_owned_reset(bfs_btree_owned_t *owned)
{
    if (!owned) return;
    if (owned->slots)
        memset(owned->slots, 0, (size_t)owned->capacity * sizeof(*owned->slots));
    owned->used = 0;
    owned->txn_id = 0;
}

void bfs_btree_owned_destroy(bfs_btree_owned_t *owned)
{
    if (!owned) return;
    free(owned->slots);
    owned->slots = NULL;
    owned->capacity = 0;
    owned->used = 0;
    owned->txn_id = 0;
}

static uint32_t owned_hash(bfs_blk_t blk, uint32_t capacity)
{
    return (uint32_t)(blk * 2654435761u) & (capacity - 1u);
}

/* Return the tree's registry for the live transaction, clearing entries that
 * belong to an earlier one. */
static bfs_btree_owned_t *owned_for(const bfs_btree_t *tree)
{
    bfs_btree_owned_t *owned = tree->free_sink.owned;
    if (!owned || owned->disabled) return NULL;
    uint64_t txn = bfs_btree_txn_id(tree);
    if (owned->txn_id != txn) {
        bfs_btree_owned_reset(owned);
        owned->txn_id = txn;
    }
    return owned;
}

static bool owned_contains(const bfs_btree_t *tree, bfs_blk_t blk)
{
    bfs_btree_owned_t *owned = owned_for(tree);
    if (!owned || owned->capacity == 0 || blk == BFS_BLK_NULL || blk == OWNED_REMOVED)
        return false;
    for (uint32_t i = owned_hash(blk, owned->capacity), n = 0;
         n < owned->capacity; i = (i + 1u) & (owned->capacity - 1u), n++) {
        if (owned->slots[i] == blk) return true;
        if (owned->slots[i] == BFS_BLK_NULL) return false;
    }
    return false;
}

static void owned_place(bfs_btree_owned_t *owned, bfs_blk_t blk)
{
    uint32_t i = owned_hash(blk, owned->capacity);
    while (owned->slots[i] != BFS_BLK_NULL && owned->slots[i] != OWNED_REMOVED &&
           owned->slots[i] != blk)
        i = (i + 1u) & (owned->capacity - 1u);
    if (owned->slots[i] == blk) return;
    if (owned->slots[i] == BFS_BLK_NULL) owned->used++;
    owned->slots[i] = blk;
}

/* Best effort: a block that cannot be registered keeps copy-on-write. */
static void owned_add(const bfs_btree_t *tree, bfs_blk_t blk)
{
    bfs_btree_owned_t *owned = owned_for(tree);
    if (!owned || blk == BFS_BLK_NULL || blk == OWNED_REMOVED) return;
    if (owned->capacity == 0 || (owned->used + 1u) * 2u > owned->capacity) {
        uint32_t capacity = owned->capacity ? owned->capacity * 2u : OWNED_MIN_CAPACITY;
        if (capacity < owned->capacity || capacity > UINT32_MAX / 2u) return;
        bfs_blk_t *slots = calloc(capacity, sizeof(*slots));
        if (!slots) return;
        bfs_btree_owned_t grown = {
            .slots = slots, .capacity = capacity, .used = 0, .txn_id = owned->txn_id,
        };
        for (uint32_t i = 0; i < owned->capacity; i++) {
            bfs_blk_t entry = owned->slots[i];
            if (entry != BFS_BLK_NULL && entry != OWNED_REMOVED)
                owned_place(&grown, entry);
        }
        free(owned->slots);
        *owned = grown;
    }
    owned_place(owned, blk);
}

static void owned_remove(const bfs_btree_t *tree, bfs_blk_t blk)
{
    bfs_btree_owned_t *owned = owned_for(tree);
    if (!owned || owned->capacity == 0 || blk == BFS_BLK_NULL || blk == OWNED_REMOVED)
        return;
    for (uint32_t i = owned_hash(blk, owned->capacity), n = 0;
         n < owned->capacity; i = (i + 1u) & (owned->capacity - 1u), n++) {
        if (owned->slots[i] == blk) {
            owned->slots[i] = OWNED_REMOVED;
            return;
        }
        if (owned->slots[i] == BFS_BLK_NULL) return;
    }
}

/* ── Insert helpers ────────────────────────────────────────── */

#define BTREE_MUTATION_MAX_BLOCKS (4u * MAX_TREE_DEPTH + 8u)

typedef struct {
    bfs_blk_t new_blocks[BTREE_MUTATION_MAX_BLOCKS];
    uint32_t new_count;
    bfs_blk_t retired_blocks[BTREE_MUTATION_MAX_BLOCKS];
    uint64_t retired_txns[BTREE_MUTATION_MAX_BLOCKS];
    uint32_t retired_count;
    /* Owned nodes rewritten in place, published only after every fallible
     * step. The images are private copies because path and sibling buffers
     * are reused and released before the mutation commits. */
    bfs_blk_t staged_blocks[BTREE_MUTATION_MAX_BLOCKS];
    uint8_t *staged_images[BTREE_MUTATION_MAX_BLOCKS];
    uint32_t staged_count;
} btree_mutation_t;

static bfs_err_t allocator_failure(bfs_btree_t *tree)
{
    if (tree->alloc->error) {
        bfs_err_t err = tree->alloc->error(tree->alloc);
        if (err != BFS_OK) return err;
    }
    return BFS_ERR_NOSPC;
}

static void latch_reclaim_error(bfs_btree_t *tree, bfs_err_t err)
{
    if (err != BFS_OK && tree->free_sink_err == BFS_OK)
        tree->free_sink_err = err;
}

static bfs_err_t node_dealloc(bfs_btree_t *tree, bfs_blk_t blk)
{
    owned_remove(tree, blk);
    /* A freed node's deferred image must never reach the block. */
    bfs_bio_discard_deferred(tree->bio, blk);
    return tree->alloc->dealloc(tree->alloc, blk);
}

static bfs_blk_t mutation_alloc(bfs_btree_t *tree, btree_mutation_t *mutation)
{
    bfs_blk_t blk = tree->alloc->alloc(tree->alloc);
    if (blk == BFS_BLK_NULL) return BFS_BLK_NULL;
    if (mutation->new_count >= BTREE_MUTATION_MAX_BLOCKS) {
        latch_reclaim_error(tree, tree->alloc->dealloc(tree->alloc, blk));
        latch_reclaim_error(tree, BFS_ERR_NOSPC);
        return BFS_BLK_NULL;
    }
    mutation->new_blocks[mutation->new_count++] = blk;
    owned_add(tree, blk);
    return blk;
}

static bfs_err_t mutation_retire_txn(btree_mutation_t *mutation, bfs_blk_t blk,
                                     uint64_t block_txn)
{
    if (blk == BFS_BLK_NULL) return BFS_OK;
    for (uint32_t i = 0; i < mutation->retired_count; i++) {
        if (mutation->retired_blocks[i] == blk) return BFS_OK;
    }
    if (mutation->retired_count >= BTREE_MUTATION_MAX_BLOCKS)
        return BFS_ERR_NOSPC;
    mutation->retired_blocks[mutation->retired_count] = blk;
    mutation->retired_txns[mutation->retired_count] = block_txn;
    mutation->retired_count++;
    return BFS_OK;
}

static bfs_err_t mutation_retire(btree_mutation_t *mutation, bfs_blk_t blk,
                                 const uint8_t *buf)
{
    const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
    return mutation_retire_txn(mutation, blk, bfs_be64(hdr->txn_id));
}

static void mutation_release_staged(bfs_btree_t *tree, btree_mutation_t *mutation)
{
    for (uint32_t i = 0; i < mutation->staged_count; i++)
        free_buf(tree, mutation->staged_images[i]);
    mutation->staged_count = 0;
}

static void mutation_abort(bfs_btree_t *tree, btree_mutation_t *mutation)
{
    /* Staged owned images were never written; the nodes keep their old bytes. */
    mutation_release_staged(tree, mutation);
    for (uint32_t i = 0; i < mutation->new_count; i++)
        latch_reclaim_error(tree, node_dealloc(tree, mutation->new_blocks[i]));
}

static bool mutation_retires(const btree_mutation_t *mutation, bfs_blk_t blk)
{
    for (uint32_t i = 0; i < mutation->retired_count; i++)
        if (mutation->retired_blocks[i] == blk) return true;
    return false;
}

static void mutation_commit(bfs_btree_t *tree, btree_mutation_t *mutation)
{
    /* Every fallible step succeeded and the in-memory tree already refers to
     * the staged images, so publish them even if a reclamation error was
     * latched. A failed rewrite leaves the live transaction's graph uncertain:
     * latch it here and in the owner's recovery state, so no later operation
     * or commit can use the graph before the committed roots are reloaded.
     * Committed nodes are never among the staged blocks. */
    for (uint32_t i = 0; i < mutation->staged_count; i++) {
        bfs_blk_t blk = mutation->staged_blocks[i];
        if (mutation_retires(mutation, blk)) continue;
        bfs_err_t err = node_write(tree, blk, mutation->staged_images[i]);
        if (err == BFS_OK) continue;
        latch_reclaim_error(tree, err);
        bfs_btree_owned_t *owned = tree->free_sink.owned;
        if (owned && owned->recovery_state && *owned->recovery_state == BFS_OK)
            *owned->recovery_state = err;
        break;
    }
    mutation_release_staged(tree, mutation);
    for (uint32_t i = 0; i < mutation->retired_count; i++) {
        bfs_blk_t blk = mutation->retired_blocks[i];
        if (mutation->retired_txns[i] >= bfs_btree_txn_id(tree)) {
            latch_reclaim_error(tree, node_dealloc(tree, blk));
        } else if (tree->free_sink.defer) {
            latch_reclaim_error(tree,
                                tree->free_sink.defer(tree->free_sink.ctx, blk));
        }
    }
}

static bfs_err_t cow_node(bfs_btree_t *tree, btree_mutation_t *mutation,
                          bfs_blk_t old_blk, uint8_t *buf,
                          bfs_blk_t *out_blk);

/* Centralized node deallocation. Blocks from the current transaction are 
 * freed immediately; older blocks are queued for post-commit reclamation. */
static void btree_free_node(bfs_btree_t *tree, bfs_blk_t blk, const uint8_t *buf)
{
    if (blk == BFS_BLK_NULL) return;
    const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
    uint64_t block_txn = bfs_be64(hdr->txn_id);

    if (block_txn >= bfs_btree_txn_id(tree)) {
        /* Current-transaction block: free it immediately. */
        latch_reclaim_error(tree, node_dealloc(tree, blk));
    } else if (tree->free_sink.defer) {
        /* Older block: it must NOT be freed mid-COW (a crash before commit would
         * corrupt the last committed state were it reused), so defer it to the
         * post-commit reclaim queue. The queue cannot be drained here — we are
         * mid-mutation — so callers reserve headroom up front and this never
         * fails. If it ever does, latch the error so insert/delete surface
         * BFS_ERR_NOSPC instead of silently dropping (leaking) the block. */
        bfs_err_t derr = tree->free_sink.defer(tree->free_sink.ctx, blk);
        if (derr != BFS_OK && tree->free_sink_err == BFS_OK)
            tree->free_sink_err = derr;
    }
    /* else: standalone tree, older block — nothing to do. */
}

static bfs_err_t cow_node(bfs_btree_t *tree, btree_mutation_t *mutation,
                          bfs_blk_t old_blk, uint8_t *buf,
                          bfs_blk_t *out_blk)
{
    const bfs_btnode_hdr_t *old_hdr = (const bfs_btnode_hdr_t *)buf;
    uint64_t old_txn = bfs_be64(old_hdr->txn_id);
    if (old_blk != BFS_BLK_NULL && old_txn == bfs_btree_txn_id(tree) &&
        owned_contains(tree, old_blk)) {
        uint32_t slot = 0;
        while (slot < mutation->staged_count && mutation->staged_blocks[slot] != old_blk)
            slot++;
        uint8_t *image = slot < mutation->staged_count ? mutation->staged_images[slot] :
                         (mutation->staged_count < BTREE_MUTATION_MAX_BLOCKS ?
                          alloc_buf(tree) : NULL);
        if (image) {
            /* The block size is the size of every node buffer and staged image. */
            memcpy(image, buf, tree->bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
            if (slot == mutation->staged_count) {
                mutation->staged_blocks[slot] = old_blk;
                mutation->staged_images[slot] = image;
                mutation->staged_count++;
            }
            *out_blk = old_blk;
            return BFS_OK;
        }
        /* No staging buffer: copy on write as for any other node. */
    }
    bfs_blk_t new_blk = mutation_alloc(tree, mutation);
    if (new_blk == BFS_BLK_NULL) return allocator_failure(tree);

    bfs_err_t err = node_write(tree, new_blk, buf);
    if (err != BFS_OK) return err;
    err = mutation_retire_txn(mutation, old_blk, old_txn);
    if (err != BFS_OK) return err;
    *out_blk = new_blk;
    return BFS_OK;
}

uint32_t bfs_btree_leaf_capacity(const bfs_btree_t *tree)
{
    if (!tree || !tree->bio || !tree->ops || !tree->ops->key_compare ||
        tree->ops->key_size == 0 || tree->ops->key_size > BFS_MAX_KEY_SIZE ||
        tree->ops->val_size == 0 || !bfs_block_size_valid(tree->bio->block_size))
        return 0;
    return leaf_max_keys(tree);
}

static bfs_err_t validate_leaf_entries(const bfs_btree_t *tree,
                                       const void *keys, uint32_t count)
{
    if (count == 0 || count > leaf_max_keys(tree)) return BFS_ERR_INVAL;
    const uint8_t *key_bytes = keys;
    for (uint32_t i = 1; i < count; i++)
        if (tree->ops->key_compare(key_bytes + (size_t)(i - 1) * tree->ops->key_size,
                                   key_bytes + (size_t)i * tree->ops->key_size) >= 0)
            return BFS_ERR_INVAL;
    return BFS_OK;
}

static bfs_err_t validate_root_leaf_replacement(bfs_btree_t *tree,
                                                 const void *keys,
                                                 const void *vals,
                                                 uint32_t count)
{
    if (!tree || !tree->alloc || !keys || !vals ||
        bfs_btree_leaf_capacity(tree) == 0)
        return BFS_ERR_INVAL;
    if (tree->height != 1 || tree->root == BFS_BLK_NULL)
        return BFS_ERR_UNSUPPORTED;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    return validate_leaf_entries(tree, keys, count);
}

static void build_leaf(bfs_btree_t *tree, uint8_t *new_buf, const void *keys,
                       const void *vals, uint32_t count)
{
    node_init(tree, new_buf, BFS_BTNODE_LEAF);
    hdr_of(new_buf)->num_keys = bfs_be32(count);
    for (uint32_t i = 0; i < count; i++) {
        copy_bytes(node_key(tree, new_buf, i),
                   (const uint8_t *)keys + (size_t)i * tree->ops->key_size,
                   tree->ops->key_size);
        copy_bytes(leaf_val(tree, new_buf, i),
                   (const uint8_t *)vals + (size_t)i * tree->ops->val_size,
                   tree->ops->val_size);
    }
}

static void build_root_leaf_replacement(bfs_btree_t *tree, uint8_t *new_buf,
                                         const uint8_t *old_buf, const void *keys,
                                         const void *vals, uint32_t count)
{
    build_leaf(tree, new_buf, keys, vals, count);
    hdr_of(new_buf)->right_sibling =
        ((const bfs_btnode_hdr_t *)old_buf)->right_sibling;
}

bfs_err_t bfs_btree_create_root_leaf(bfs_btree_t *tree, const void *keys,
                                     const void *vals, uint32_t count)
{
    if (!tree || !tree->bio || !tree->alloc || !keys || !vals ||
        bfs_btree_leaf_capacity(tree) == 0)
        return BFS_ERR_INVAL;
    if (tree->root != BFS_BLK_NULL) return BFS_ERR_EXISTS;
    bfs_err_t err = validate_leaf_entries(tree, keys, count);
    if (err != BFS_OK) return err;
    tree->free_sink_err = BFS_OK;
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    build_leaf(tree, buf, keys, vals, count);

    btree_mutation_t mutation = {0};
    bfs_blk_t blk = mutation_alloc(tree, &mutation);
    if (blk == BFS_BLK_NULL) {
        free_buf(tree, buf);
        return allocator_failure(tree);
    }
    err = node_write(tree, blk, buf);
    free_buf(tree, buf);
    if (err != BFS_OK) {
        mutation_abort(tree, &mutation);
        return err;
    }
    tree->root = blk;
    tree->generation++;
    tree->height = 1;
    mutation_commit(tree, &mutation);
    return tree->free_sink_err;
}

static bfs_err_t replace_root_leaf(bfs_btree_t *tree, bfs_blk_t expected_root,
                                   const void *keys, const void *vals,
                                   uint32_t count, bool transfer_old_root,
                                   bool allow_in_place, bool *published)
{
    *published = false;
    bfs_err_t err = validate_root_leaf_replacement(tree, keys, vals, count);
    if (err != BFS_OK) return err;
    if (transfer_old_root &&
        (expected_root == BFS_BLK_NULL || expected_root != tree->root))
        return BFS_ERR_UNSUPPORTED;

    if (!transfer_old_root) {
        tree->free_sink_err = BFS_OK;
        err = mutation_headroom(tree, 1);
        if (err != BFS_OK) return err;
    }
    uint8_t *old_buf = alloc_buf(tree);
    uint8_t *new_buf = alloc_buf(tree);
    if (!old_buf || !new_buf) {
        free_buf(tree, old_buf);
        free_buf(tree, new_buf);
        return BFS_ERR_NOMEM;
    }
    bfs_blk_t old_root = tree->root;
    err = node_read_at_level(tree, old_root, old_buf, BFS_BTNODE_LEAF);
    if (err != BFS_OK) goto done;
    if (transfer_old_root &&
        bfs_be64(hdr_of(old_buf)->txn_id) != bfs_btree_txn_id(tree)) {
        err = BFS_ERR_UNSUPPORTED;
        goto done;
    }
    if (transfer_old_root) tree->free_sink_err = BFS_OK;

    btree_mutation_t mutation = {0};
    build_root_leaf_replacement(tree, new_buf, old_buf, keys, vals, count);
    /* A root the live transaction owns is rewritten in place, published like
     * any staged owned node. Only a deferring cache makes that publication a
     * memory update; written through, a device error at publication would
     * discard the transaction where copy-on-write fails before it. */
    if (allow_in_place && !transfer_old_root && bfs_bio_can_defer_nodes(tree->bio) &&
        bfs_be64(hdr_of(old_buf)->txn_id) == bfs_btree_txn_id(tree) &&
        owned_contains(tree, old_root)) {
        mutation.staged_blocks[0] = old_root;
        mutation.staged_images[0] = new_buf;
        mutation.staged_count = 1;
        new_buf = NULL; /* now owned by the mutation */
        *published = true;
        mutation_commit(tree, &mutation);
        err = tree->free_sink_err;
        goto done;
    }
    bfs_blk_t new_root = mutation_alloc(tree, &mutation);
    if (new_root == BFS_BLK_NULL) {
        err = allocator_failure(tree);
        goto abort_done;
    }
    if (new_root == old_root) {
        /* Do not let mutation_abort deallocate a live root returned by a bad
         * allocator. This one-block operation has no other scratch blocks. */
        mutation.new_count--;
        err = BFS_ERR_CORRUPT;
        goto abort_done;
    }
    err = node_write(tree, new_root, new_buf);
    if (err != BFS_OK) goto abort_done;
    if (!transfer_old_root) {
        err = mutation_retire(&mutation, old_root, old_buf);
        if (err != BFS_OK) goto abort_done;
    }
    tree->root = new_root;
    tree->generation++;
    *published = true;
    mutation_commit(tree, &mutation);
    err = tree->free_sink_err;
    goto done;

abort_done:
    mutation_abort(tree, &mutation);
done:
    free_buf(tree, old_buf);
    free_buf(tree, new_buf);
    return err;
}

bfs_err_t bfs_btree_replace_root_leaf(bfs_btree_t *tree, const void *keys,
                                      const void *vals, uint32_t count)
{
    bool published;
    return replace_root_leaf(tree, BFS_BLK_NULL, keys, vals, count, false, false,
                             &published);
}

bfs_err_t bfs_btree_rewrite_root_leaf(bfs_btree_t *tree, const void *keys,
                                      const void *vals, uint32_t count,
                                      bool *published)
{
    bool ignored;
    return replace_root_leaf(tree, BFS_BLK_NULL, keys, vals, count, false, true,
                             published ? published : &ignored);
}

bfs_err_t bfs_btree_root_leaf_txn_id(bfs_btree_t *tree, uint64_t *txn_id_out)
{
    if (!txn_id_out || bfs_btree_leaf_capacity(tree) == 0)
        return BFS_ERR_INVAL;
    if (tree->root == BFS_BLK_NULL || tree->height != 1)
        return BFS_ERR_UNSUPPORTED;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_err_t err = node_read_at_level(tree, tree->root, buf,
                                       BFS_BTNODE_LEAF);
    if (err == BFS_OK)
        *txn_id_out = bfs_be64(hdr_of(buf)->txn_id);
    free_buf(tree, buf);
    return err;
}

bfs_err_t bfs_btree_replace_owned_root_leaf(bfs_btree_t *tree,
                                            bfs_blk_t expected_root,
                                            const void *keys, const void *vals,
                                            uint32_t count)
{
    bool published;
    return replace_root_leaf(tree, expected_root, keys, vals, count, true, false,
                             &published);
}

/* Insert key/val into a leaf at position idx. Caller must ensure there's room. */
static void leaf_insert_at(const bfs_btree_t *tree, uint8_t *buf,
                           uint32_t idx, const void *key, const void *val)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;
    uint32_t vs = tree->ops->val_size;

    for (uint32_t i = n; i > idx; i--) {
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i - 1), ks);
        memcpy(leaf_val(tree, buf, i), leaf_val(tree, buf, i - 1), vs);
    }
    memcpy(node_key(tree, buf, idx), key, ks);
    memcpy(leaf_val(tree, buf, idx), val, vs);
    hdr_of(buf)->num_keys = bfs_be32(n + 1);
}

/* Insert key + right_child into an internal node at position idx.
 * Caller must ensure there's room. */
static void internal_insert_at(const bfs_btree_t *tree, uint8_t *buf,
                               uint32_t idx, const void *key, bfs_blk_t right_child)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;

    for (uint32_t i = n; i > idx; i--)
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i - 1), ks);
    memcpy(node_key(tree, buf, idx), key, ks);

    for (uint32_t i = n + 1; i > idx + 1; i--)
        set_child(tree, buf, i, get_child(tree, buf, i - 1));
    set_child(tree, buf, idx + 1, right_child);

    hdr_of(buf)->num_keys = bfs_be32(n + 1);
}

typedef struct {
    bool did_split;
    uint8_t median_key[BFS_MAX_KEY_SIZE];
    bfs_blk_t new_right;
} split_result_t;

/* Split a full leaf. Left keeps first half, right gets second half.
 * Median key (first key of right) is returned for parent insertion. */
static bfs_err_t leaf_split(bfs_btree_t *tree, btree_mutation_t *mutation,
                            uint8_t *buf, split_result_t *result)
{
    uint32_t n = num_keys(buf);
    uint32_t mid = n / 2;
    uint32_t ks = tree->ops->key_size;
    uint32_t vs = tree->ops->val_size;

    uint8_t *right_buf = alloc_buf(tree);
    if (!right_buf) return BFS_ERR_NOMEM;
    node_init(tree, right_buf, BFS_BTNODE_LEAF);

    uint32_t right_count = n - mid;
    for (uint32_t i = 0; i < right_count; i++) {
        memcpy(node_key(tree, right_buf, i), node_key(tree, buf, mid + i), ks);
        memcpy(leaf_val(tree, right_buf, i), leaf_val(tree, buf, mid + i), vs);
    }
    hdr_of(right_buf)->num_keys = bfs_be32(right_count);
    hdr_of(right_buf)->right_sibling = hdr_of(buf)->right_sibling;

    hdr_of(buf)->num_keys = bfs_be32(mid);

    bfs_blk_t right_blk = mutation_alloc(tree, mutation);
    if (right_blk == BFS_BLK_NULL) {
        free_buf(tree, right_buf);
        return allocator_failure(tree);
    }

    hdr_of(buf)->right_sibling = bfs_be32(right_blk);

    bfs_err_t write_err = node_write(tree, right_blk, right_buf);
    if (write_err != BFS_OK) {
        free_buf(tree, right_buf);
        return write_err;
    }

    memcpy(result->median_key, node_key(tree, right_buf, 0), ks);
    result->new_right = right_blk;
    result->did_split = true;
    free_buf(tree, right_buf);
    return BFS_OK;
}

/* Split a full internal node. Median key is promoted (not kept in either child). */
static bfs_err_t internal_split(bfs_btree_t *tree, btree_mutation_t *mutation,
                                uint8_t *buf, split_result_t *result)
{
    uint32_t n = num_keys(buf);
    uint32_t mid = n / 2;
    uint32_t ks = tree->ops->key_size;

    uint8_t *right_buf = alloc_buf(tree);
    if (!right_buf) return BFS_ERR_NOMEM;
    node_init(tree, right_buf, node_level(buf));

    memcpy(result->median_key, node_key(tree, buf, mid), ks);

    uint32_t right_count = n - mid - 1;
    for (uint32_t i = 0; i < right_count; i++)
        memcpy(node_key(tree, right_buf, i), node_key(tree, buf, mid + 1 + i), ks);
    hdr_of(right_buf)->num_keys = bfs_be32(right_count);

    for (uint32_t i = 0; i <= right_count; i++)
        set_child(tree, right_buf, i, get_child(tree, buf, mid + 1 + i));

    hdr_of(buf)->num_keys = bfs_be32(mid);

    bfs_blk_t right_blk = mutation_alloc(tree, mutation);
    if (right_blk == BFS_BLK_NULL) {
        free_buf(tree, right_buf);
        return allocator_failure(tree);
    }

    bfs_err_t write_err = node_write(tree, right_blk, right_buf);
    if (write_err != BFS_OK) {
        free_buf(tree, right_buf);
        return write_err;
    }

    result->new_right = right_blk;
    result->did_split = true;
    free_buf(tree, right_buf);
    return BFS_OK;
}

/* ── Iterative top-down insert ─────────────────────────────── */

typedef struct {
    bfs_blk_t blk;
    uint32_t child_idx;
} path_entry_t;

bfs_err_t bfs_btree_insert(bfs_btree_t *tree, const void *key, const void *val)
{
    if (!tree || !tree->bio || !tree->alloc || !key || !val)
        return BFS_ERR_INVAL;
    tree->free_sink_err = BFS_OK;
    btree_mutation_t mutation = {0};
    /* Empty tree: create a root leaf */
    if (tree->root == BFS_BLK_NULL) {
        uint8_t *buf = alloc_buf(tree);
        if (!buf) return BFS_ERR_NOMEM;
        node_init(tree, buf, BFS_BTNODE_LEAF);
        leaf_insert_at(tree, buf, 0, key, val);

        bfs_blk_t blk = mutation_alloc(tree, &mutation);
        if (blk == BFS_BLK_NULL) {
            free_buf(tree, buf);
            return allocator_failure(tree);
        }
        bfs_err_t err = node_write(tree, blk, buf);
        free_buf(tree, buf);
        if (err != BFS_OK) {
            mutation_abort(tree, &mutation);
            return err;
        }
        tree->root = blk;
        tree->generation++;
        tree->height = 1;
        mutation_commit(tree, &mutation);
        return tree->free_sink_err;
    }
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    bfs_err_t preflight = mutation_headroom(tree, tree->height);
    if (preflight != BFS_OK) return preflight;

    const uint32_t bs = tree->bio->block_size;
    bfs_err_t rc = BFS_OK;

    /* Descend to leaf, recording path */
    path_entry_t path[MAX_TREE_DEPTH];
    /* Shape is validated above; every recorded path index is below height.
     * Split siblings and a possible new root have independent buffers. */
    uint8_t *node_bufs = bfs_bio_alloc_buffer(tree->bio,
                                             (size_t)tree->height * bs);
    if (!node_bufs) return BFS_ERR_NOMEM;
    #define NBUF(d) (node_bufs + (d) * bs)
    int depth = 0;

    bfs_blk_t blk = tree->root;
    while (1) {
        if (depth >= MAX_TREE_DEPTH) { rc = BFS_ERR_CORRUPT; goto insert_cleanup; }
        if ((uint32_t)depth >= tree->height) {
            rc = BFS_ERR_CORRUPT;
            goto insert_cleanup;
        }
        uint16_t expected_level = (uint16_t)(tree->height - 1 - depth);
        bfs_err_t err = node_read_at_level(tree, blk, NBUF(depth), expected_level);
        if (err != BFS_OK) { rc = err; goto insert_cleanup; }
        path[depth].blk = blk;

        if (is_leaf(NBUF(depth)))
            break;

        bool found;
        uint32_t idx = node_search(tree, NBUF(depth), key, &found);
        path[depth].child_idx = found ? idx + 1 : idx;
        blk = get_child(tree, NBUF(depth), path[depth].child_idx);
        depth++;
    }

    /* Check for duplicate in leaf */
    uint8_t *leaf = NBUF(depth);
    bool found;
    uint32_t idx = node_search(tree, leaf, key, &found);
    if (found) { rc = BFS_ERR_EXISTS; goto insert_cleanup; }

    /* Split the leaf FIRST if it's full, then insert into the correct half */
    split_result_t split = { .did_split = false };

    if (num_keys(leaf) >= leaf_max_keys(tree)) {
        bfs_err_t err = leaf_split(tree, &mutation, leaf, &split);
        if (err != BFS_OK) { rc = err; goto insert_cleanup; }

        /* Determine which half the new key goes into */
        if (tree->ops->key_compare(key, split.median_key) >= 0) {
            /* Key goes into the right (new) node — read it, insert there */
            uint8_t *right_buf = alloc_buf(tree);
            if (!right_buf) { rc = BFS_ERR_NOMEM; goto insert_cleanup; }
            err = node_read(tree, split.new_right, right_buf);
            if (err != BFS_OK) { free_buf(tree, right_buf); rc = err; goto insert_cleanup; }

            bool f2;
            uint32_t idx2 = node_search(tree, right_buf, key, &f2);
            if (f2) { free_buf(tree, right_buf); rc = BFS_ERR_EXISTS; goto insert_cleanup; }
            leaf_insert_at(tree, right_buf, idx2, key, val);

            /* Re-write the right node */
            err = node_write(tree, split.new_right, right_buf);
            free_buf(tree, right_buf);
            if (err != BFS_OK) { rc = err; goto insert_cleanup; }
        } else {
            /* Key goes into the left (current) node */
            bool f2;
            uint32_t idx2 = node_search(tree, leaf, key, &f2);
            leaf_insert_at(tree, leaf, idx2, key, val);
        }
    } else {
        /* Room available — just insert */
        leaf_insert_at(tree, leaf, idx, key, val);
    }

    /* COW the leaf */
    bfs_blk_t new_blk;
    rc = cow_node(tree, &mutation, path[depth].blk, leaf, &new_blk);
    if (rc != BFS_OK) goto insert_cleanup;

    /* Walk back up the path */
    for (int d = depth - 1; d >= 0; d--) {
        uint8_t *node = NBUF(d);
        uint32_t ci = path[d].child_idx;

        set_child(tree, node, ci, new_blk);

        if (split.did_split) {
            if (num_keys(node) >= internal_max_keys(tree)) {
                /* Split the internal node FIRST, then insert into correct half */
                split_result_t parent_split;
                bfs_err_t err = internal_split(tree, &mutation, node, &parent_split);
                if (err != BFS_OK) { rc = err; goto insert_cleanup; }

                /* Determine which half gets the new key */
                if (tree->ops->key_compare(split.median_key, parent_split.median_key) >= 0) {
                    /* Insert into right half */
                    uint8_t *right_buf = alloc_buf(tree);
                    if (!right_buf) { rc = BFS_ERR_NOMEM; goto insert_cleanup; }
                    err = node_read(tree, parent_split.new_right, right_buf);
                    if (err != BFS_OK) { free_buf(tree, right_buf); rc = err; goto insert_cleanup; }
                    bool f;
                    uint32_t ri = node_search(tree, right_buf, split.median_key, &f);
                    internal_insert_at(tree, right_buf, ri, split.median_key, split.new_right);
                    err = node_write(tree, parent_split.new_right, right_buf);
                    free_buf(tree, right_buf);
                    if (err != BFS_OK) { rc = err; goto insert_cleanup; }
                } else {
                    /* Insert into left half (current node) */
                    bool f;
                    uint32_t li = node_search(tree, node, split.median_key, &f);
                    internal_insert_at(tree, node, li, split.median_key, split.new_right);
                }
                split = parent_split;
            } else {
                internal_insert_at(tree, node, ci, split.median_key, split.new_right);
                split.did_split = false;
            }
        }

        rc = cow_node(tree, &mutation, path[d].blk, node, &new_blk);
        if (rc != BFS_OK) goto insert_cleanup;
    }

    bfs_blk_t final_root = new_blk;

    /* If the root split, create a new root */
    if (split.did_split) {
        uint8_t *root_buf = alloc_buf(tree);
        if (!root_buf) { rc = BFS_ERR_NOMEM; goto insert_cleanup; }
        node_init(tree, root_buf, node_level(NBUF(0)) + 1);
        set_child(tree, root_buf, 0, new_blk);
        memcpy(node_key(tree, root_buf, 0), split.median_key, tree->ops->key_size);
        set_child(tree, root_buf, 1, split.new_right);
        hdr_of(root_buf)->num_keys = bfs_be32(1);

        bfs_blk_t root_blk = mutation_alloc(tree, &mutation);
        if (root_blk == BFS_BLK_NULL) {
            free_buf(tree, root_buf);
            rc = allocator_failure(tree);
            goto insert_cleanup;
        }
        bfs_err_t err = node_write(tree, root_blk, root_buf);
        free_buf(tree, root_buf);
        if (err != BFS_OK) { rc = err; goto insert_cleanup; }
        final_root = root_blk;
        tree->height++;
    }
    tree->root = final_root;
    tree->generation++;

insert_cleanup:
    free_buf(tree, node_bufs);
    #undef NBUF
    if (rc == BFS_OK)
        mutation_commit(tree, &mutation);
    else
        mutation_abort(tree, &mutation);
    if (rc == BFS_OK && tree->free_sink_err != BFS_OK)
        rc = tree->free_sink_err;
    return rc;
}

/* ── Update (single-traversal value modification with COW) ── */

bfs_err_t bfs_btree_update(bfs_btree_t *tree, const void *key, const void *new_val)
{
    if (!tree || !tree->bio || !tree->alloc || !key || !new_val)
        return BFS_ERR_INVAL;
    tree->free_sink_err = BFS_OK;
    btree_mutation_t mutation = {0};
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    bfs_err_t preflight = mutation_headroom(tree, tree->height);
    if (preflight != BFS_OK) return preflight;

    const uint32_t bs = tree->bio->block_size;
    uint32_t depth = tree->height > 0 ? tree->height : 2;
    uint8_t *node_bufs = bfs_bio_alloc_buffer(tree->bio, (size_t)depth * bs);
    if (!node_bufs) return BFS_ERR_NOMEM;
    #define UBUF(d) (node_bufs + (d) * bs)

    path_entry_t path[MAX_TREE_DEPTH];
    int d = 0;
    bfs_blk_t blk = tree->root;

    /* Descend to leaf */
    while (1) {
        if (d >= MAX_TREE_DEPTH) { free_buf(tree, node_bufs); return BFS_ERR_CORRUPT; }
        if ((uint32_t)d >= tree->height) {
            free_buf(tree, node_bufs);
            return BFS_ERR_CORRUPT;
        }
        bfs_err_t err = node_read_at_level(
            tree, blk, UBUF(d), (uint16_t)(tree->height - 1 - d));
        if (err != BFS_OK) { free_buf(tree, node_bufs); return err; }
        path[d].blk = blk;
        if (is_leaf(UBUF(d))) break;
        bool found;
        uint32_t idx = node_search(tree, UBUF(d), key, &found);
        path[d].child_idx = found ? idx + 1 : idx;
        blk = get_child(tree, UBUF(d), path[d].child_idx);
        d++;
    }

    /* Find key in leaf and update value */
    bool found;
    uint32_t idx = node_search(tree, UBUF(d), key, &found);
    if (!found) { free_buf(tree, node_bufs); return BFS_ERR_NOTFOUND; }
    /* The validated value already represents the requested state. Keep its
     * real transaction tag and ownership; an identical update needs no COW. */
    if (memcmp(leaf_val(tree, UBUF(d), idx), new_val,
               tree->ops->val_size) == 0) {
        free_buf(tree, node_bufs);
        return BFS_OK;
    }
    memcpy(leaf_val(tree, UBUF(d), idx), new_val, tree->ops->val_size);

    /* COW back up */
    bfs_blk_t new_blk;
    bfs_err_t cerr = cow_node(tree, &mutation, path[d].blk, UBUF(d), &new_blk);
    if (cerr != BFS_OK) goto update_cleanup;
    for (int i = d - 1; i >= 0; i--) {
        set_child(tree, UBUF(i), path[i].child_idx, new_blk);
        cerr = cow_node(tree, &mutation, path[i].blk, UBUF(i), &new_blk);
        if (cerr != BFS_OK) goto update_cleanup;
    }
    tree->root = new_blk;
    tree->generation++;

update_cleanup:
    free_buf(tree, node_bufs);
    #undef UBUF
    if (cerr == BFS_OK)
        mutation_commit(tree, &mutation);
    else
        mutation_abort(tree, &mutation);
    if (cerr == BFS_OK && tree->free_sink_err != BFS_OK)
        cerr = tree->free_sink_err;
    return cerr;
}

/* Descend to the leaf that holds key, recording the path and, if bounds is
 * not NULL, the separators that bound the leaf's keys. */
static bfs_err_t rekey_descend_to_leaf(const bfs_btree_t *tree, const void *key,
                                       uint8_t *node_bufs, path_entry_t *path,
                                       int *depth_out, node_bounds_t *bounds)
{
    const uint32_t block_size = tree->bio->block_size;
    bfs_blk_t block = tree->root;
    for (int depth = 0;; depth++) {
        if (depth >= MAX_TREE_DEPTH || (uint32_t)depth >= tree->height)
            return BFS_ERR_CORRUPT;
        uint8_t *buffer = node_bufs + (size_t)depth * block_size;
        bfs_err_t err = node_read_at_level(
            tree, block, buffer, (uint16_t)(tree->height - 1 - depth));
        if (err != BFS_OK) return err;
        path[depth].blk = block;
        if (is_leaf(buffer)) {
            *depth_out = depth;
            return BFS_OK;
        }
        bool found;
        uint32_t index = node_search(tree, buffer, key, &found);
        path[depth].child_idx = found ? index + 1 : index;
        if (bounds) child_bounds(tree, buffer, path[depth].child_idx, bounds);
        block = get_child(tree, buffer, path[depth].child_idx);
    }
}

static bfs_err_t rekey_commit_path(bfs_btree_t *tree, btree_mutation_t *mutation,
                                   path_entry_t *path, uint8_t *node_bufs, int depth)
{
    const uint32_t block_size = tree->bio->block_size;
    bfs_blk_t replacement;
    uint8_t *leaf = node_bufs + (size_t)depth * block_size;
    bfs_err_t err = cow_node(tree, mutation, path[depth].blk, leaf, &replacement);
    if (err != BFS_OK) return err;
    for (int level = depth - 1; level >= 0; level--) {
        uint8_t *parent = node_bufs + (size_t)level * block_size;
        set_child(tree, parent, path[level].child_idx, replacement);
        err = cow_node(tree, mutation, path[level].blk, parent, &replacement);
        if (err != BFS_OK) return err;
    }
    tree->root = replacement;
    tree->generation++;
    return BFS_OK;
}

bfs_err_t bfs_btree_rekey_equal(bfs_btree_t *tree, const void *old_key,
                                const void *new_key)
{
    if (!tree || !tree->bio || !tree->alloc || !old_key || !new_key ||
        !tree->ops || tree->ops->key_compare(old_key, new_key) != 0)
        return BFS_ERR_INVAL;
    tree->free_sink_err = BFS_OK;
    btree_mutation_t mutation = {0};
    if (tree->root == BFS_BLK_NULL) return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    bfs_err_t preflight = mutation_headroom(tree, tree->height);
    if (preflight != BFS_OK) return preflight;

    const uint32_t block_size = tree->bio->block_size;
    uint8_t *node_bufs = bfs_bio_alloc_buffer(tree->bio,
                                             (size_t)tree->height * block_size);
    if (!node_bufs) return BFS_ERR_NOMEM;

    path_entry_t path[MAX_TREE_DEPTH];
    int depth = 0;
    bfs_err_t err = rekey_descend_to_leaf(tree, old_key, node_bufs, path, &depth, NULL);
    if (err != BFS_OK) goto rekey_cleanup;

    bool found;
    uint8_t *leaf = node_bufs + (size_t)depth * block_size;
    uint32_t index = node_search(tree, leaf, old_key, &found);
    if (!found) {
        err = BFS_ERR_NOTFOUND;
        goto rekey_cleanup;
    }
    if (tree->ops->key_size > BFS_MAX_KEY_SIZE) {
        err = BFS_ERR_CORRUPT;
        goto rekey_cleanup;
    }
    copy_bytes(node_key(tree, leaf, index), new_key, tree->ops->key_size);
    err = rekey_commit_path(tree, &mutation, path, node_bufs, depth);

rekey_cleanup:
    free_buf(tree, node_bufs);
    if (err == BFS_OK) mutation_commit(tree, &mutation);
    else mutation_abort(tree, &mutation);
    if (err == BFS_OK && tree->free_sink_err != BFS_OK) err = tree->free_sink_err;
    return err;
}

/* True if new_key can replace the key at index of leaf: it keeps the leaf
 * ordered and stays inside the separators that route to the leaf, so no
 * other node has to change. */
static bool key_fits_at(const bfs_btree_t *tree, uint8_t *leaf, uint32_t index,
                        const node_bounds_t *bounds, const void *new_key)
{
    uint32_t n = num_keys(leaf);
    return !((index > 0 &&
              tree->ops->key_compare(node_key(tree, leaf, index - 1), new_key) >= 0) ||
             (index + 1 < n &&
              tree->ops->key_compare(new_key, node_key(tree, leaf, index + 1)) >= 0) ||
             (bounds->have_lower && tree->ops->key_compare(new_key, bounds->lower) < 0) ||
             (bounds->have_upper && tree->ops->key_compare(new_key, bounds->upper) >= 0));
}

bfs_err_t bfs_btree_update_key(bfs_btree_t *tree, const void *old_key,
                               const void *new_key, const void *new_val)
{
    if (!tree || !tree->bio || !tree->alloc || !old_key || !new_key || !new_val ||
        !tree->ops)
        return BFS_ERR_INVAL;
    tree->free_sink_err = BFS_OK;
    btree_mutation_t mutation = {0};
    if (tree->root == BFS_BLK_NULL) return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;

    node_bounds_t bounds = {0};
    bfs_err_t preflight = mutation_headroom(tree, tree->height);
    if (preflight != BFS_OK) return preflight;

    const uint32_t block_size = tree->bio->block_size;
    uint8_t *node_bufs = bfs_bio_alloc_buffer(tree->bio,
                                             (size_t)tree->height * block_size);
    if (!node_bufs) return BFS_ERR_NOMEM;

    path_entry_t path[MAX_TREE_DEPTH];
    int depth = 0;
    bfs_err_t err = rekey_descend_to_leaf(tree, old_key, node_bufs, path, &depth,
                                          &bounds);
    if (err != BFS_OK) goto update_key_cleanup;

    bool found;
    uint8_t *leaf = node_bufs + (size_t)depth * block_size;
    uint32_t index = node_search(tree, leaf, old_key, &found);
    if (!found) {
        err = BFS_ERR_NOTFOUND;
        goto update_key_cleanup;
    }
    if (!key_fits_at(tree, leaf, index, &bounds, new_key)) {
        err = BFS_ERR_UNSUPPORTED;
        goto update_key_cleanup;
    }
    copy_bytes(node_key(tree, leaf, index), new_key, tree->ops->key_size);
    copy_bytes(leaf_val(tree, leaf, index), new_val, tree->ops->val_size);
    err = rekey_commit_path(tree, &mutation, path, node_bufs, depth);

update_key_cleanup:
    free_buf(tree, node_bufs);
    if (err == BFS_OK) mutation_commit(tree, &mutation);
    else mutation_abort(tree, &mutation);
    if (err == BFS_OK && tree->free_sink_err != BFS_OK) err = tree->free_sink_err;
    return err;
}

/* ── Scan ──────────────────────────────────────────────────── */

/* Inner nodes from the root to the current leaf and the child taken in each. */
typedef struct {
    bfs_blk_t blk[MAX_TREE_DEPTH];
    uint32_t idx[MAX_TREE_DEPTH];
    uint32_t depth;
} scan_path_t;

/* Descend to the leaf where key belongs, or to the leftmost leaf for NULL,
 * and record the path. Inner nodes are only viewed. *leaf is a view that the
 * next BIO call may invalidate. */
static bfs_err_t scan_descend(const bfs_btree_t *tree, const void *key,
                              uint8_t *buf, scan_path_t *path, uint8_t **leaf)
{
    bfs_blk_t blk = tree->root;
    uint16_t level = (uint16_t)(tree->height - 1);
    node_bounds_t bounds = {0};
    path->depth = 0;
    for (;;) {
        uint8_t *node;
        bfs_err_t err = node_view(tree, blk, buf, level, &bounds, &node);
        if (err != BFS_OK) return err;
        if (level == 0) {
            *leaf = node;
            return BFS_OK;
        }
        if (path->depth >= MAX_TREE_DEPTH) return BFS_ERR_CORRUPT;
        uint32_t child = 0;
        if (key) {
            bool found;
            uint32_t idx = node_search(tree, node, key, &found);
            child = found ? idx + 1 : idx;
        }
        path->blk[path->depth] = blk;
        path->idx[path->depth] = child;
        path->depth++;
        child_bounds(tree, node, child, &bounds);
        blk = get_child(tree, node, child);
        level--;
    }
}

/* Move the path to the next leaf to the right; *leaf is NULL past the last. */
static bfs_err_t scan_next_leaf(const bfs_btree_t *tree, uint8_t *buf,
                                scan_path_t *path, uint8_t **leaf)
{
    *leaf = NULL;
    while (path->depth > 0) {
        uint32_t d = path->depth - 1;
        uint16_t level = (uint16_t)(tree->height - 1 - d);
        uint8_t *node;
        bfs_err_t err = node_view(tree, path->blk[d], buf, level, NULL, &node);
        if (err != BFS_OK) return err;
        uint32_t next = path->idx[d] + 1;
        if (next > num_keys(node)) {
            path->depth--;
            continue;
        }
        path->idx[d] = next;
        node_bounds_t bounds = {0};
        child_bounds(tree, node, next, &bounds);
        bfs_blk_t blk = get_child(tree, node, next);
        level--;
        for (;;) {
            err = node_view(tree, blk, buf, level, &bounds, &node);
            if (err != BFS_OK) return err;
            if (level == 0) {
                *leaf = node;
                return BFS_OK;
            }
            if (path->depth >= MAX_TREE_DEPTH) return BFS_ERR_CORRUPT;
            path->blk[path->depth] = blk;
            path->idx[path->depth] = 0;
            path->depth++;
            child_bounds(tree, node, 0, &bounds);
            blk = get_child(tree, node, 0);
            level--;
        }
    }
    return BFS_OK;
}

void bfs_btree_cursor_init(bfs_btree_cursor_t *cursor)
{
    if (cursor) memset(cursor, 0, sizeof(*cursor));
}

void bfs_btree_cursor_release(bfs_btree_cursor_t *cursor)
{
    if (!cursor) return;
    free(cursor->leaf);
    memset(cursor, 0, sizeof(*cursor));
}

/* True if the cursor's copy is the current leaf that holds key. */
static bool cursor_covers(const bfs_btree_t *tree, const bfs_btree_cursor_t *cursor,
                          const void *key)
{
    if (!cursor->valid || cursor->tree != tree || cursor->root != tree->root ||
        cursor->generation != tree->generation)
        return false;
    uint32_t n = num_keys(cursor->leaf);
    return n > 0 && tree->ops->key_compare(node_key(tree, cursor->leaf, 0), key) <= 0 &&
           tree->ops->key_compare(key, node_key(tree, cursor->leaf, n - 1)) <= 0;
}

bfs_err_t bfs_btree_scan(bfs_btree_t *tree, const void *start_key,
                           bfs_scan_cb cb, void *ctx)
{
    return bfs_btree_scan_cursor(tree, NULL, start_key, cb, ctx);
}

bfs_err_t bfs_btree_scan_cursor(bfs_btree_t *tree, bfs_btree_cursor_t *cursor,
                                const void *start_key, bfs_scan_cb cb, void *ctx)
{
    if (!tree || !tree->bio || !tree->ops || !cb) return BFS_ERR_INVAL;
    if (tree->root == BFS_BLK_NULL)
        return BFS_OK;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    if (cursor && !cursor->leaf) {
        cursor->leaf = malloc(tree->bio->block_size);
        cursor->valid = false;
        if (!cursor->leaf) cursor = NULL;
    }

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    uint8_t *last = NULL;
    scan_path_t path;
    uint8_t *leaf = NULL;
    uint32_t idx = 0;
    /* A leaf taken from the cursor has no recorded path from the root. */
    bool have_path = true;
    bfs_err_t err = BFS_OK;
    if (cursor && start_key && cursor_covers(tree, cursor, start_key)) {
        leaf = cursor->leaf;
        have_path = false;
    } else {
        err = scan_descend(tree, start_key, buf, &path, &leaf);
    }
    if (err == BFS_OK && start_key) {
        bool found;
        idx = node_search(tree, leaf, start_key, &found);
    }
    while (err == BFS_OK && leaf) {
        uint32_t n = num_keys(leaf);
        if (last && idx == 0 && n > 0 &&
            tree->ops->key_compare(node_key(tree, leaf, 0), last) <= 0) {
            err = BFS_ERR_CORRUPT;
            break;
        }
        bool changed = false;
        uint32_t reached = n;
        if (idx < n) {
            /* Callbacks may do I/O, which can invalidate a view. */
            uint8_t *copy = cursor ? cursor->leaf : buf;
            if (leaf != copy) memcpy(copy, leaf, tree->bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
            leaf = copy;
            if (cursor) {
                cursor->tree = tree;
                cursor->root = tree->root;
                cursor->generation = tree->generation;
                cursor->valid = true;
            }
            uint32_t generation = tree->generation;
            bool stopped = false;
            for (uint32_t i = idx; i < n; i++) {
                if (!cb(node_key(tree, leaf, i), leaf_val(tree, leaf, i), ctx)) {
                    stopped = true;
                    break;
                }
                /* The rest of this copy may be stale once the tree changed. */
                if (tree->generation != generation) {
                    changed = true;
                    reached = i + 1;
                    break;
                }
            }
            if (stopped) break;
        }
        if (n > 0) {
            /* The order check and a changed tree need the last key passed. */
            if (!last) last = malloc(tree->ops->key_size);
            if (!last) {
                err = BFS_ERR_NOMEM;
                break;
            }
            memcpy(last, node_key(tree, leaf, reached - 1), tree->ops->key_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        }
        if (changed && cursor) cursor->valid = false;
        if (changed || !have_path) {
            /* A callback changed the tree, or the leaf came from the cursor:
             * find the place after last from the root. */
            err = scan_descend(tree, last, buf, &path, &leaf);
            if (err != BFS_OK) break;
            have_path = true;
            bool found;
            idx = node_search(tree, leaf, last, &found);
            if (found) idx++;
            continue;
        }
        err = scan_next_leaf(tree, buf, &path, &leaf);
        idx = 0;
    }
    free(last);
    free_buf(tree, buf);
    return err;
}


/* ── Floor search ──────────────────────────────────────────── */

/* Find the rightmost key in the subtree rooted at blk. */
static bfs_err_t rightmost_in_subtree(const bfs_btree_t *tree, bfs_blk_t blk,
                                       uint16_t expected_level,
                                       void *key_out, void *val_out)
{
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    uint32_t depth = 0;
    while (1) {
        if (depth++ >= MAX_TREE_DEPTH) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
        bfs_err_t err = node_read_at_level(tree, blk, buf, expected_level);
        if (err != BFS_OK) { free_buf(tree, buf); return err; }
        uint32_t n = num_keys(buf);
        if (n == 0) { free_buf(tree, buf); return BFS_ERR_NOTFOUND; }
        if (is_leaf(buf)) {
            memcpy(key_out, node_key(tree, buf, n - 1), tree->ops->key_size);
            memcpy(val_out, leaf_val(tree, buf, n - 1), tree->ops->val_size);
            free_buf(tree, buf);
            return BFS_OK;
        }
        if (expected_level == 0) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
        blk = get_child(tree, buf, n); /* rightmost child */
        expected_level--;
    }
}

bfs_err_t bfs_btree_search_floor(bfs_btree_t *tree, const void *key,
                                    void *key_out, void *val_out)
{
    if (!tree || !tree->bio || !tree->ops || !key || !key_out || !val_out)
        return BFS_ERR_INVAL;
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_blk_t blk = tree->root;
    uint32_t depth = 0;
    uint16_t expected_level = (uint16_t)(tree->height - 1);

    /* Track the last internal node where we descended right (idx > 0).
     * If the leaf has no key <= search_key, the predecessor is the
     * rightmost key in child[turn_idx - 1] of that node. */
    bfs_blk_t turn_blk = BFS_BLK_NULL;
    uint32_t turn_child_idx = 0; /* child index we came from (the left sibling has the predecessor) */
    uint16_t turn_level = 0;

    while (1) {
        if (depth++ >= MAX_TREE_DEPTH) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
        uint8_t *node;
        bfs_err_t err = node_view(tree, blk, buf, expected_level, NULL, &node);
        if (err != BFS_OK) { free_buf(tree, buf); return err; }

        bool found;
        uint32_t idx = node_search(tree, node, key, &found);

        if (is_leaf(node)) {
            if (found) {
                memcpy(key_out, node_key(tree, node, idx), tree->ops->key_size);
                memcpy(val_out, leaf_val(tree, node, idx), tree->ops->val_size);
                free_buf(tree, buf);
                return BFS_OK;
            }
            if (idx > 0) {
                memcpy(key_out, node_key(tree, node, idx - 1), tree->ops->key_size);
                memcpy(val_out, leaf_val(tree, node, idx - 1), tree->ops->val_size);
                free_buf(tree, buf);
                return BFS_OK;
            }
            /* idx == 0: all keys in this leaf > search_key.
             * The predecessor is the rightmost key in the left subtree
             * at the last right-turn point. */
            if (turn_blk == BFS_BLK_NULL) {
                free_buf(tree, buf);
                return BFS_ERR_NOTFOUND;
            }
            /* Re-read the turn node and descend into child[turn_child_idx - 1] */
            err = node_read_at_level(tree, turn_blk, buf, turn_level);
            if (err != BFS_OK) { free_buf(tree, buf); return err; }
            bfs_blk_t left = get_child(tree, buf, turn_child_idx - 1);
            uint16_t left_level = (uint16_t)(node_level(buf) - 1);
            free_buf(tree, buf);
            return rightmost_in_subtree(tree, left, left_level,
                                        key_out, val_out);
        }

        /* Internal node: descend */
        if (expected_level == 0) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
        uint32_t child_idx = found ? idx + 1 : idx;
        if (child_idx > 0) {
            turn_blk = blk;
            turn_child_idx = child_idx;
            turn_level = expected_level;
        }
        blk = get_child(tree, node, child_idx);
        expected_level--;
    }
}

/* ── Delete ─────────────────────────────────────────────────── */

/* Remove key at index idx from a leaf node */
static void leaf_remove_at(const bfs_btree_t *tree, uint8_t *buf, uint32_t idx)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;
    uint32_t vs = tree->ops->val_size;

    for (uint32_t i = idx; i < n - 1; i++) {
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i + 1), ks);
        memcpy(leaf_val(tree, buf, i), leaf_val(tree, buf, i + 1), vs);
    }
    hdr_of(buf)->num_keys = bfs_be32(n - 1);
}

/* Remove key at index idx and child at idx+1 from an internal node */
static void internal_remove_at(const bfs_btree_t *tree, uint8_t *buf, uint32_t idx)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;

    for (uint32_t i = idx; i < n - 1; i++)
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i + 1), ks);
    for (uint32_t i = idx + 1; i < n; i++)
        set_child(tree, buf, i, get_child(tree, buf, i + 1));
    hdr_of(buf)->num_keys = bfs_be32(n - 1);
}

static uint32_t leaf_min(const bfs_btree_t *tree)
{
    return leaf_max_keys(tree) / 2;
}

static uint32_t internal_min(const bfs_btree_t *tree)
{
    return internal_max_keys(tree) / 2;
}

bfs_err_t bfs_btree_delete(bfs_btree_t *tree, const void *key)
{
    if (!tree || !tree->bio || !tree->alloc || !key) return BFS_ERR_INVAL;
    tree->free_sink_err = BFS_OK;
    btree_mutation_t mutation = {0};
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    bfs_err_t preflight = mutation_headroom(tree, 2u * tree->height + 1u);
    if (preflight != BFS_OK) return preflight;

    const uint32_t bs = tree->bio->block_size;
    bfs_err_t rc = BFS_OK;

    /* Descend to leaf, recording path */
    path_entry_t path[MAX_TREE_DEPTH];
    /* Merge/collapse only uses the existing path; sibling storage is separate. */
    uint8_t *node_bufs = bfs_bio_alloc_buffer(tree->bio,
                                             (size_t)tree->height * bs);
    if (!node_bufs) return BFS_ERR_NOMEM;
    uint8_t *sib_buf = alloc_buf(tree);
    if (!sib_buf) { free_buf(tree, node_bufs); return BFS_ERR_NOMEM; }
    #define DNBUF(d) (node_bufs + (d) * bs)
    int depth = 0;

    bfs_blk_t blk = tree->root;
    while (1) {
        if (depth >= MAX_TREE_DEPTH) { rc = BFS_ERR_CORRUPT; goto delete_cleanup; }
        if ((uint32_t)depth >= tree->height) {
            rc = BFS_ERR_CORRUPT;
            goto delete_cleanup;
        }
        bfs_err_t err = node_read_at_level(
            tree, blk, DNBUF(depth), (uint16_t)(tree->height - 1 - depth));
        if (err != BFS_OK) { rc = err; goto delete_cleanup; }
        path[depth].blk = blk;

        if (is_leaf(DNBUF(depth)))
            break;

        bool found;
        uint32_t idx = node_search(tree, DNBUF(depth), key, &found);
        path[depth].child_idx = found ? idx + 1 : idx;
        blk = get_child(tree, DNBUF(depth), path[depth].child_idx);
        depth++;
    }

    /* Remove key from leaf */
    uint8_t *leaf = DNBUF(depth);
    bool found;
    uint32_t idx = node_search(tree, leaf, key, &found);
    if (!found) { rc = BFS_ERR_NOTFOUND; goto delete_cleanup; }

    leaf_remove_at(tree, leaf, idx);

    /* If root is a leaf, just COW and done (no minimum fill requirement for root) */
    if (depth == 0) {
        if (num_keys(leaf) == 0) {
            rc = mutation_retire(&mutation, path[0].blk, leaf);
            if (rc != BFS_OK) goto delete_cleanup;
            tree->root = BFS_BLK_NULL;
            tree->generation++;
            tree->height = 0;
            goto delete_cleanup;
        }
        bfs_blk_t new_blk;
        rc = cow_node(tree, &mutation, path[0].blk, leaf, &new_blk);
        if (rc != BFS_OK) goto delete_cleanup;
        tree->root = new_blk;
        tree->generation++;
        goto delete_cleanup;
    }

    /* Check if leaf is underfull and needs rebalancing */
    bool merged = false;

    if (num_keys(leaf) < leaf_min(tree)) {
        uint8_t *parent = DNBUF(depth - 1);
        uint32_t ci = path[depth - 1].child_idx;
        uint32_t parent_nkeys = num_keys(parent);
        uint32_t ks = tree->ops->key_size;
        uint32_t vs = tree->ops->val_size;

        /* Try to borrow from right sibling */
        if (ci < parent_nkeys) {
            bfs_blk_t sib_blk = get_child(tree, parent, ci + 1);
            bfs_err_t sibling_err = node_read_at_level(tree, sib_blk, sib_buf, 0);
            if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
            if (num_keys(sib_buf) > leaf_min(tree)) {
                /* Borrow first key/val from right sibling */
                uint32_t ln = num_keys(leaf);
                memcpy(node_key(tree, leaf, ln), node_key(tree, sib_buf, 0), ks);
                memcpy(leaf_val(tree, leaf, ln), leaf_val(tree, sib_buf, 0), vs);
                hdr_of(leaf)->num_keys = bfs_be32(ln + 1);
                leaf_remove_at(tree, sib_buf, 0);

                /* Update parent separator to new first key of right sibling */
                memcpy(node_key(tree, parent, ci), node_key(tree, sib_buf, 0), ks);

                /* COW the sibling */
                bfs_blk_t new_sib;
                rc = cow_node(tree, &mutation, sib_blk, sib_buf, &new_sib);
                if (rc != BFS_OK) goto delete_cleanup;
                set_child(tree, parent, ci + 1, new_sib);
                goto cow_upward;
            }
        }

        /* Try to borrow from left sibling */
        if (ci > 0) {
            bfs_blk_t sib_blk = get_child(tree, parent, ci - 1);
            bfs_err_t sibling_err = node_read_at_level(tree, sib_blk, sib_buf, 0);
            if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
            if (num_keys(sib_buf) > leaf_min(tree)) {
                /* Borrow last key/val from left sibling */
                uint32_t sn = num_keys(sib_buf);
                uint32_t ln = num_keys(leaf);
                /* Shift leaf entries right */
                for (uint32_t i = ln; i > 0; i--) {
                    memcpy(node_key(tree, leaf, i), node_key(tree, leaf, i - 1), ks);
                    memcpy(leaf_val(tree, leaf, i), leaf_val(tree, leaf, i - 1), vs);
                }
                memcpy(node_key(tree, leaf, 0), node_key(tree, sib_buf, sn - 1), ks);
                memcpy(leaf_val(tree, leaf, 0), leaf_val(tree, sib_buf, sn - 1), vs);
                hdr_of(leaf)->num_keys = bfs_be32(ln + 1);
                hdr_of(sib_buf)->num_keys = bfs_be32(sn - 1);

                /* Update parent separator to new first key of current leaf */
                memcpy(node_key(tree, parent, ci - 1), node_key(tree, leaf, 0), ks);

                bfs_blk_t new_sib;
                rc = cow_node(tree, &mutation, sib_blk, sib_buf, &new_sib);
                if (rc != BFS_OK) goto delete_cleanup;
                set_child(tree, parent, ci - 1, new_sib);
                goto cow_upward;
            }
        }

        /* Merge with a sibling */
        if (ci < parent_nkeys) {
            /* Merge with right sibling into current leaf */
            bfs_blk_t sib_blk = get_child(tree, parent, ci + 1);
            bfs_err_t sibling_err = node_read_at_level(tree, sib_blk, sib_buf, 0);
            if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
            {
                uint32_t ln = num_keys(leaf);
                uint32_t sn = num_keys(sib_buf);
                for (uint32_t i = 0; i < sn; i++) {
                    memcpy(node_key(tree, leaf, ln + i), node_key(tree, sib_buf, i), ks);
                    memcpy(leaf_val(tree, leaf, ln + i), leaf_val(tree, sib_buf, i), vs);
                }
                hdr_of(leaf)->num_keys = bfs_be32(ln + sn);
                hdr_of(leaf)->right_sibling = hdr_of(sib_buf)->right_sibling;
                rc = mutation_retire(&mutation, sib_blk, sib_buf);
                if (rc != BFS_OK) goto delete_cleanup;
                internal_remove_at(tree, parent, ci);
                merged = true;
            }
        } else if (ci > 0) {
            /* Merge current leaf into left sibling */
            bfs_blk_t sib_blk = get_child(tree, parent, ci - 1);
            bfs_err_t sibling_err = node_read_at_level(tree, sib_blk, sib_buf, 0);
            if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
            {
                uint32_t sn = num_keys(sib_buf);
                uint32_t ln = num_keys(leaf);
                for (uint32_t i = 0; i < ln; i++) {
                    memcpy(node_key(tree, sib_buf, sn + i), node_key(tree, leaf, i), ks);
                    memcpy(leaf_val(tree, sib_buf, sn + i), leaf_val(tree, leaf, i), vs);
                }
                hdr_of(sib_buf)->num_keys = bfs_be32(sn + ln);
                hdr_of(sib_buf)->right_sibling = hdr_of(leaf)->right_sibling;
                rc = mutation_retire(&mutation, path[depth].blk, leaf);
                if (rc != BFS_OK) goto delete_cleanup;

                /* Replace leaf with the merged sibling for COW upward */
                memcpy(leaf, sib_buf, tree->bio->block_size);
                path[depth].blk = sib_blk;
                internal_remove_at(tree, parent, ci - 1);
                path[depth - 1].child_idx = ci - 1;
                merged = true;
            }
        }
    }

cow_upward:
    /* COW the leaf */
    {
        bfs_blk_t new_blk;
        rc = cow_node(tree, &mutation, path[depth].blk, leaf, &new_blk);
        if (rc != BFS_OK) goto delete_cleanup;

        /* Walk back up, propagating merges */
        for (int d = depth - 1; d >= 0; d--) {
            uint8_t *node = DNBUF(d);
            uint32_t ci = path[d].child_idx;
            set_child(tree, node, ci, new_blk);

            /* Check if internal node needs rebalancing after merge */
            if (merged && d > 0 && num_keys(node) < internal_min(tree)) {
                uint8_t *pp = DNBUF(d - 1);
                uint32_t pci = path[d - 1].child_idx;
                uint32_t pp_nkeys = num_keys(pp);
                uint32_t ks = tree->ops->key_size;

                /* Try borrow from right sibling */
                if (pci < pp_nkeys) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci + 1);
                    bfs_err_t sibling_err = node_read_at_level(
                        tree, sib_blk, sib_buf, node_level(node));
                    if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
                    if (num_keys(sib_buf) > internal_min(tree)) {
                        uint32_t nn = num_keys(node);
                        /* Bring parent separator down */
                        memcpy(node_key(tree, node, nn), node_key(tree, pp, pci), ks);
                        set_child(tree, node, nn + 1, get_child(tree, sib_buf, 0));
                        hdr_of(node)->num_keys = bfs_be32(nn + 1);
                        /* Move sibling's first key up to parent */
                        memcpy(node_key(tree, pp, pci), node_key(tree, sib_buf, 0), ks);
                        /* Remove first key+child from sibling */
                        uint32_t sn = num_keys(sib_buf);
                        for (uint32_t i = 0; i < sn - 1; i++)
                            memcpy(node_key(tree, sib_buf, i), node_key(tree, sib_buf, i + 1), ks);
                        for (uint32_t i = 0; i < sn; i++)
                            set_child(tree, sib_buf, i, get_child(tree, sib_buf, i + 1));
                        hdr_of(sib_buf)->num_keys = bfs_be32(sn - 1);

                        bfs_blk_t new_sib;
                        rc = cow_node(tree, &mutation, sib_blk, sib_buf, &new_sib);
                        if (rc != BFS_OK) goto delete_cleanup;
                        set_child(tree, pp, pci + 1, new_sib);
                        merged = false;
                        goto cow_this;
                    }
                }

                /* Try borrow from left sibling */
                if (pci > 0) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci - 1);
                    bfs_err_t sibling_err = node_read_at_level(
                        tree, sib_blk, sib_buf, node_level(node));
                    if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
                    if (num_keys(sib_buf) > internal_min(tree)) {
                        uint32_t nn = num_keys(node);
                        uint32_t sn = num_keys(sib_buf);
                        /* Shift node entries right */
                        for (uint32_t i = nn; i > 0; i--)
                            memcpy(node_key(tree, node, i), node_key(tree, node, i - 1), ks);
                        for (uint32_t i = nn + 1; i > 0; i--)
                            set_child(tree, node, i, get_child(tree, node, i - 1));
                        /* Bring parent separator down */
                        memcpy(node_key(tree, node, 0), node_key(tree, pp, pci - 1), ks);
                        set_child(tree, node, 0, get_child(tree, sib_buf, sn));
                        hdr_of(node)->num_keys = bfs_be32(nn + 1);
                        /* Move sibling's last key up to parent */
                        memcpy(node_key(tree, pp, pci - 1), node_key(tree, sib_buf, sn - 1), ks);
                        hdr_of(sib_buf)->num_keys = bfs_be32(sn - 1);

                        bfs_blk_t new_sib;
                        rc = cow_node(tree, &mutation, sib_blk, sib_buf, &new_sib);
                        if (rc != BFS_OK) goto delete_cleanup;
                        set_child(tree, pp, pci - 1, new_sib);
                        merged = false;
                        goto cow_this;
                    }
                }

                /* Merge internal nodes */
                if (pci < pp_nkeys) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci + 1);
                    bfs_err_t sibling_err = node_read_at_level(
                        tree, sib_blk, sib_buf, node_level(node));
                    if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
                    {
                        uint32_t nn = num_keys(node);
                        uint32_t sn = num_keys(sib_buf);
                        memcpy(node_key(tree, node, nn), node_key(tree, pp, pci), ks);
                        for (uint32_t i = 0; i < sn; i++)
                            memcpy(node_key(tree, node, nn + 1 + i), node_key(tree, sib_buf, i), ks);
                        for (uint32_t i = 0; i <= sn; i++)
                            set_child(tree, node, nn + 1 + i, get_child(tree, sib_buf, i));
                        hdr_of(node)->num_keys = bfs_be32(nn + 1 + sn);
                        rc = mutation_retire(&mutation, sib_blk, sib_buf);
                        if (rc != BFS_OK) goto delete_cleanup;
                        internal_remove_at(tree, pp, pci);
                        /* merged stays true, will propagate up */
                    }
                } else if (pci > 0) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci - 1);
                    bfs_err_t sibling_err = node_read_at_level(
                        tree, sib_blk, sib_buf, node_level(node));
                    if (sibling_err != BFS_OK) { rc = sibling_err; goto delete_cleanup; }
                    {
                        uint32_t sn = num_keys(sib_buf);
                        uint32_t nn = num_keys(node);
                        memcpy(node_key(tree, sib_buf, sn), node_key(tree, pp, pci - 1), ks);
                        for (uint32_t i = 0; i < nn; i++)
                            memcpy(node_key(tree, sib_buf, sn + 1 + i), node_key(tree, node, i), ks);
                        for (uint32_t i = 0; i <= nn; i++)
                            set_child(tree, sib_buf, sn + 1 + i, get_child(tree, node, i));
                        hdr_of(sib_buf)->num_keys = bfs_be32(sn + 1 + nn);
                        rc = mutation_retire(&mutation, path[d].blk, node);
                        if (rc != BFS_OK) goto delete_cleanup;
                        memcpy(node, sib_buf, tree->bio->block_size);
                        path[d].blk = sib_blk;
                        internal_remove_at(tree, pp, pci - 1);
                        path[d - 1].child_idx = pci - 1;
                    }
                }
            } else {
                merged = false;
            }

cow_this:
            rc = cow_node(tree, &mutation, path[d].blk, node, &new_blk);
            if (rc != BFS_OK) goto delete_cleanup;
        }

        bfs_blk_t final_root = new_blk;
        if (tree->height > 1 && !is_leaf(DNBUF(0)) &&
            num_keys(DNBUF(0)) == 0) {
            final_root = get_child(tree, DNBUF(0), 0);
            rc = mutation_retire_txn(&mutation, new_blk,
                                     bfs_btree_txn_id(tree));
            if (rc != BFS_OK) goto delete_cleanup;
            tree->height--;
        }
        tree->root = final_root;
        tree->generation++;
    }

delete_cleanup:
    free_buf(tree, sib_buf);
    free_buf(tree, node_bufs);
    #undef DNBUF
    if (rc == BFS_OK)
        mutation_commit(tree, &mutation);
    else
        mutation_abort(tree, &mutation);
    if (rc == BFS_OK && tree->free_sink_err != BFS_OK)
        rc = tree->free_sink_err;
    return rc;
}

/* ── Walk all node blocks (for fsck) ───────────────────────── */

typedef struct {
    bfs_blk_t *slots;
    size_t capacity;
    size_t count;
} block_set_t;

static void block_set_destroy(block_set_t *set)
{
    free(set->slots);
    set->slots = NULL;
    set->capacity = set->count = 0;
}

static bfs_err_t block_set_grow(block_set_t *set)
{
    size_t new_capacity = set->capacity ? set->capacity * 2u : 256u;
    if (new_capacity < set->capacity ||
        new_capacity > SIZE_MAX / sizeof(*set->slots))
        return BFS_ERR_NOMEM;

    bfs_blk_t *new_slots = malloc(new_capacity * sizeof(*new_slots));
    if (!new_slots) return BFS_ERR_NOMEM;
    memset(new_slots, 0, new_capacity * sizeof(*new_slots));

    for (size_t i = 0; i < set->capacity; i++) {
        bfs_blk_t blk = set->slots[i];
        if (blk == BFS_BLK_NULL) continue;
        size_t slot = ((uint32_t)(blk * 2654435761u)) & (new_capacity - 1u);
        while (new_slots[slot] != BFS_BLK_NULL)
            slot = (slot + 1u) & (new_capacity - 1u);
        new_slots[slot] = blk;
    }
    free(set->slots);
    set->slots = new_slots;
    set->capacity = new_capacity;
    return BFS_OK;
}

static bfs_err_t block_set_add(block_set_t *set, bfs_blk_t blk)
{
    if (!set || blk == BFS_BLK_NULL) return BFS_ERR_INVAL;
    if (set->capacity == 0 || set->count >= set->capacity / 2u) {
        bfs_err_t err = block_set_grow(set);
        if (err != BFS_OK) return err;
    }

    size_t slot = ((uint32_t)(blk * 2654435761u)) & (set->capacity - 1u);
    while (set->slots[slot] != BFS_BLK_NULL) {
        if (set->slots[slot] == blk) return BFS_ERR_EXISTS;
        slot = (slot + 1u) & (set->capacity - 1u);
    }
    set->slots[slot] = blk;
    set->count++;
    return BFS_OK;
}

typedef struct {
    bfs_node_walk_cb node_cb;    /* each node, after its subtree */
    bfs_scan_cb entry_cb;        /* each leaf entry in key order, or NULL */
    void *ctx;
    bool stopped;                /* entry_cb returned false */
} walk_visit_t;

static bfs_err_t walk_nodes_recursive(bfs_btree_t *tree, bfs_blk_t blk,
                                      walk_visit_t *visit, int depth,
                                      uint16_t expected_level,
                                      block_set_t *seen, const void *lower,
                                      const void *upper)
{
    if (blk == BFS_BLK_NULL) return BFS_OK;
    if (depth >= MAX_TREE_DEPTH) return BFS_ERR_CORRUPT;
    bfs_err_t err = block_set_add(seen, blk);
    if (err == BFS_ERR_EXISTS) return BFS_ERR_CORRUPT;
    if (err != BFS_OK) return err;
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    /* node_read (not raw bfs_bio_read) so num_keys/level are validated before
     * the child-pointer loop below trusts num_keys. */
    err = node_read_at_level(tree, blk, buf, expected_level);
    if (err != BFS_OK) { free_buf(tree, buf); return err; }

    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    uint32_t n = bfs_be32(hdr->num_keys);
    if ((lower && tree->ops->key_compare(node_key(tree, buf, 0), lower) < 0) ||
        (upper && tree->ops->key_compare(node_key(tree, buf, n - 1), upper) >= 0)) {
        free_buf(tree, buf);
        return BFS_ERR_CORRUPT;
    }
    if (bfs_be16(hdr->level) > 0) {
        /* Internal node — recurse into children */
        uint32_t data_sz = tree->bio->block_size - sizeof(bfs_btnode_hdr_t);
        uint32_t max_keys = (data_sz - 4) / (tree->ops->key_size + 4);
        uint32_t keys_end = sizeof(bfs_btnode_hdr_t) + max_keys * tree->ops->key_size;
        for (uint32_t i = 0; i <= n; i++) {
            if (expected_level == 0) { free_buf(tree, buf); return BFS_ERR_CORRUPT; }
            err = walk_nodes_recursive(
                tree, bfs_load_be32(buf + keys_end + i * sizeof(uint32_t)),
                visit, depth + 1, (uint16_t)(expected_level - 1), seen,
                i == 0 ? lower : node_key(tree, buf, i - 1),
                i == n ? upper : node_key(tree, buf, i));
            if (err != BFS_OK || visit->stopped) { free_buf(tree, buf); return err; }
        }
    } else if (visit->entry_cb) {
        for (uint32_t i = 0; i < n; i++) {
            if (!visit->entry_cb(node_key(tree, buf, i), leaf_val(tree, buf, i),
                                 visit->ctx)) {
                visit->stopped = true;
                free_buf(tree, buf);
                return BFS_OK;
            }
        }
    }
    if (visit->node_cb) visit->node_cb(blk, visit->ctx);
    free_buf(tree, buf);
    return BFS_OK;
}

/* Returns BFS_OK, or the first node-read/structural error encountered. Callers
 * that reference-count via the callback MUST check this — a swallowed read
 * failure silently skips a subtree and corrupts the counts. */
bfs_err_t bfs_btree_walk(bfs_btree_t *tree, bfs_node_walk_cb node_cb,
                         bfs_scan_cb entry_cb, void *ctx)
{
    if (!tree || !tree->bio || !tree->ops || (!node_cb && !entry_cb))
        return BFS_ERR_INVAL;
    if (tree->root == BFS_BLK_NULL)
        return tree->height == 0 ? BFS_OK : BFS_ERR_CORRUPT;
    if (tree->height == 0 || tree->height > MAX_TREE_DEPTH)
        return BFS_ERR_CORRUPT;
    block_set_t seen = {0};
    walk_visit_t visit = { .node_cb = node_cb, .entry_cb = entry_cb, .ctx = ctx };
    bfs_err_t err = walk_nodes_recursive(tree, tree->root, &visit, 0,
                                         (uint16_t)(tree->height - 1), &seen,
                                         NULL, NULL);
    block_set_destroy(&seen);
    return err;
}

bfs_err_t bfs_btree_walk_nodes(bfs_btree_t *tree, bfs_node_walk_cb cb, void *ctx)
{
    if (!cb) return BFS_ERR_INVAL;
    return bfs_btree_walk(tree, cb, NULL, ctx);
}

/* ── Compaction ────────────────────────────────────────────── */

/* Threshold for compaction: fill factor < 90% */
#define BFS_COMPACT_THRESHOLD_NUM 9
#define BFS_COMPACT_THRESHOLD_DEN 10

typedef struct {
    bfs_btree_t *new_tree;
    bfs_err_t rc;
} compact_ctx_t;

static bool compact_cb(const void *key, const void *val, void *ctx)
{
    compact_ctx_t *cc = (compact_ctx_t *)ctx;
    cc->rc = bfs_btree_insert(cc->new_tree, key, val);
    return cc->rc == BFS_OK;
}

bfs_err_t bfs_btree_free_block(bfs_btree_t *tree, bfs_blk_t blk)
{
    if (!tree || !tree->bio || !tree->alloc || blk == BFS_BLK_NULL)
        return BFS_ERR_INVAL;
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_err_t err = node_read(tree, blk, buf);
    if (err == BFS_OK)
        btree_free_node(tree, blk, buf);
    free_buf(tree, buf);
    if (err != BFS_OK) return err;
    return tree->free_sink_err;
}

typedef struct {
    uint64_t keys;
    uint64_t capacity;
    block_set_t seen;
} utilization_ctx_t;

static bfs_err_t utilization_walk_recursive(const bfs_btree_t *tree,
                                            bfs_blk_t blk,
                                            utilization_ctx_t *ctx,
                                            int depth,
                                            uint16_t expected_level,
                                            const void *lower,
                                            const void *upper)
{
    if (blk == BFS_BLK_NULL) return BFS_OK;
    if (depth >= MAX_TREE_DEPTH) return BFS_ERR_CORRUPT;
    bfs_err_t err = block_set_add(&ctx->seen, blk);
    if (err == BFS_ERR_EXISTS) return BFS_ERR_CORRUPT;
    if (err != BFS_OK) return err;
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    err = node_read_at_level(tree, blk, buf, expected_level);
    if (err != BFS_OK) {
        free_buf(tree, buf);
        return err;
    }

    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    uint32_t n = bfs_be32(hdr->num_keys);
    if ((lower && tree->ops->key_compare(node_key(tree, buf, 0), lower) < 0) ||
        (upper && tree->ops->key_compare(node_key(tree, buf, n - 1), upper) >= 0)) {
        free_buf(tree, buf);
        return BFS_ERR_CORRUPT;
    }
    ctx->keys += n;

    if (bfs_be16(hdr->level) > 0) {
        /* Internal node */
        ctx->capacity += internal_max_keys(tree);
        /* Recurse into children */
        for (uint32_t i = 0; i <= n; i++) {
            err = utilization_walk_recursive(
                tree, get_child(tree, buf, i), ctx, depth + 1,
                (uint16_t)(expected_level - 1),
                i == 0 ? lower : node_key(tree, buf, i - 1),
                i == n ? upper : node_key(tree, buf, i));
            if (err != BFS_OK) {
                free_buf(tree, buf);
                return err;
            }
        }
    } else {
        /* Leaf node */
        ctx->capacity += leaf_max_keys(tree);
    }

    free_buf(tree, buf);
    return BFS_OK;
}

static bfs_err_t bfs_btree_needs_compaction(const bfs_btree_t *tree,
                                            bool *needed)
{
    *needed = false;
    if (tree->root == BFS_BLK_NULL) return BFS_OK;
    if (!tree_shape_valid(tree)) return BFS_ERR_CORRUPT;
    utilization_ctx_t ctx = {0};
    bfs_err_t err = utilization_walk_recursive(tree, tree->root, &ctx, 0,
                                               (uint16_t)(tree->height - 1),
                                               NULL, NULL);
    block_set_destroy(&ctx.seen);
    if (err != BFS_OK) return err;
    if (ctx.capacity == 0) return BFS_OK;

    /* Returns true if utilization is < 90% */
    *needed = ctx.keys * BFS_COMPACT_THRESHOLD_DEN <
              ctx.capacity * BFS_COMPACT_THRESHOLD_NUM;
    return BFS_OK;
}

typedef struct {
    bfs_btree_t *tree;
    bfs_err_t err;
} discard_ctx_t;

static void discard_node_cb(bfs_blk_t blk, void *ctx)
{
    discard_ctx_t *discard = (discard_ctx_t *)ctx;
    if (discard->err == BFS_OK)
        discard->err = node_dealloc(discard->tree, blk);
}

static bfs_err_t discard_tree(bfs_btree_t *tree)
{
    if (tree->root == BFS_BLK_NULL) return BFS_OK;
    discard_ctx_t discard = { .tree = tree, .err = BFS_OK };
    bfs_err_t walk_err = bfs_btree_walk_nodes(tree, discard_node_cb, &discard);
    return walk_err != BFS_OK ? walk_err : discard.err;
}

bfs_err_t bfs_btree_compact_build_swap(bfs_btree_t *tree, bfs_blk_t *old_root_out)
{
    if (!tree || !tree->bio || !tree->alloc || !old_root_out)
        return BFS_ERR_INVAL;
    *old_root_out = tree->root;
    if (tree->root == BFS_BLK_NULL) return BFS_OK;

    /* Skip if already well-packed (utilization >= 90%) to avoid write
     * amplification; *old_root_out stays == tree->root to signal "no swap". */
    bool needed;
    bfs_err_t err = bfs_btree_needs_compaction(tree, &needed);
    if (err != BFS_OK || !needed) return err;

    bfs_btree_t new_tree;
    err = bfs_btree_init(&new_tree, tree->bio, tree->alloc, tree->ops,
                         BFS_BLK_NULL, bfs_btree_txn_id(tree));
    if (err != BFS_OK) return err;
    new_tree.txn_id_ptr = tree->txn_id_ptr;

    /* Build a dense copy by re-inserting every key. The new tree's own COW
     * frees are all current-transaction blocks (freed immediately, never
     * deferred), so building it does not touch the pending-free queue. */
    compact_ctx_t ctx = { .new_tree = &new_tree, .rc = BFS_OK };
    bfs_err_t scan_err = bfs_btree_scan(tree, NULL, compact_cb, &ctx);
    if (scan_err != BFS_OK || ctx.rc != BFS_OK) {
        bfs_err_t build_err = scan_err != BFS_OK ? scan_err : ctx.rc;
        bfs_err_t cleanup_err = discard_tree(&new_tree);
        return cleanup_err == BFS_OK ? build_err : cleanup_err;
    }

    /* Swap the root to the freshly-built tree. The OLD nodes (rooted at
     * *old_root_out) stay referenced until the fs caller commits this swap and
     * then frees them post-commit. They must NOT be freed here, mid-transaction:
     * a whole-old-tree free is unbounded and could overflow the deferred-free
     * queue. After the swap-commit they are unreferenced garbage, freed safely. */
    tree->root = new_tree.root;
    tree->generation++;
    tree->height = new_tree.height;
    return BFS_OK;
}
