/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Directory B+tree
 *
 * Composite key layout (fixed size for B+tree):
 *   [parent_id:4][name_hash:4][name_len:1][name:BFS_NAME_MAX]
 *   Total: 4 + 4 + 1 + 255 = 264 bytes
 *
 * The name is case-folded before hashing and comparison.
 * Ordering: parent_id first, then name_hash, then name bytes.
 */

#include "bfs_dir.h"
#include <string.h>

#define DIR_KEY_SIZE (4 + 4 + 1 + BFS_NAME_MAX)  /* 264 bytes */

/* ── Amiga international case folding ──────────────────────── */

uint8_t bfs_intl_toupper(uint8_t c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    /* International characters: à-þ (0xE0-0xFE) → À-Þ (0xC0-0xDE) */
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    return c;
}

/* ── FNV-1a hash ───────────────────────────────────────────── */

uint32_t bfs_dir_name_hash(const char *name, uint8_t len)
{
    uint32_t h = 0x811C9DC5; /* FNV offset basis */
    for (uint8_t i = 0; i < len; i++) {
        h ^= bfs_intl_toupper((uint8_t)name[i]);
        h *= 0x01000193; /* FNV prime */
    }
    return h;
}

/* ── Key construction ──────────────────────────────────────── */

static void make_dir_key(uint8_t *key_buf, uint32_t parent_id,
                         const char *name, uint8_t name_len)
{
    memset(key_buf, 0, DIR_KEY_SIZE);
    /* parent_id (big-endian) */
    *(uint32_t *)key_buf = bfs_be32(parent_id);
    /* name_hash (big-endian) — hash of folded name for consistent ordering */
    *(uint32_t *)(key_buf + 4) = bfs_be32(bfs_dir_name_hash(name, name_len));
    /* name_len */
    key_buf[8] = name_len;
    /* Store ORIGINAL name (comparison folds both sides) */
    memcpy(key_buf + 9, name, name_len);
}

/* ── B+tree key comparison ─────────────────────────────────── */

static int dir_key_compare(const void *a, const void *b)
{
    const uint8_t *ka = (const uint8_t *)a;
    const uint8_t *kb = (const uint8_t *)b;

    /* Compare parent_id */
    uint32_t pa = bfs_be32(*(const uint32_t *)ka);
    uint32_t pb = bfs_be32(*(const uint32_t *)kb);
    if (pa != pb) return (pa < pb) ? -1 : 1;

    /* Compare name_hash */
    uint32_t ha = bfs_be32(*(const uint32_t *)(ka + 4));
    uint32_t hb = bfs_be32(*(const uint32_t *)(kb + 4));
    if (ha != hb) return (ha < hb) ? -1 : 1;

    /* Compare name bytes with case folding */
    uint8_t la = ka[8], lb = kb[8];
    uint8_t minlen = la < lb ? la : lb;
    for (uint8_t i = 0; i < minlen; i++) {
        uint8_t ca = bfs_intl_toupper(ka[9 + i]);
        uint8_t cb = bfs_intl_toupper(kb[9 + i]);
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    if (la != lb) return (la < lb) ? -1 : 1;
    return 0;
}

static const bfs_btree_ops_t dir_ops = {
    .key_compare = dir_key_compare,
    .key_size = DIR_KEY_SIZE,
    .val_size = sizeof(bfs_dir_val_t),
};

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_dir_init(bfs_dir_tree_t *dt, bfs_bio_t *bio,
                   bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id)
{
    return bfs_btree_init(&dt->tree, bio, alloc, &dir_ops, root, txn_id);
}

/* ── Lookup ────────────────────────────────────────────────── */

bfs_err_t bfs_dir_lookup(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t *inode_nr_out, uint32_t *type_out)
{
    uint8_t key[DIR_KEY_SIZE];
    make_dir_key(key, parent_id, name, name_len);

    bfs_dir_val_t val;
    bfs_err_t err = bfs_btree_search(&dt->tree, key, &val);
    if (err != BFS_OK) return err;

    if (inode_nr_out) *inode_nr_out = bfs_be32(val.inode_nr);
    if (type_out) *type_out = bfs_be32(val.entry_type);
    return BFS_OK;
}

/* ── Insert ────────────────────────────────────────────────── */

bfs_err_t bfs_dir_insert(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t inode_nr, uint32_t entry_type)
{
    uint8_t key[DIR_KEY_SIZE];
    make_dir_key(key, parent_id, name, name_len);

    bfs_dir_val_t val = {
        .inode_nr = bfs_be32(inode_nr),
        .entry_type = bfs_be32(entry_type),
    };

    return bfs_btree_insert(&dt->tree, key, &val);
}

/* ── Remove ────────────────────────────────────────────────── */

bfs_err_t bfs_dir_remove(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len)
{
    uint8_t key[DIR_KEY_SIZE];
    make_dir_key(key, parent_id, name, name_len);
    return bfs_btree_delete(&dt->tree, key);
}

/* ── Scan ──────────────────────────────────────────────────── */

typedef struct {
    uint32_t parent_id;
    bfs_dir_scan_cb cb;
    void *ctx;
} dir_scan_ctx_t;

static bool dir_scan_cb(const void *key, const void *val, void *ctx)
{
    dir_scan_ctx_t *sc = (dir_scan_ctx_t *)ctx;
    const uint8_t *k = (const uint8_t *)key;
    const bfs_dir_val_t *v = (const bfs_dir_val_t *)val;

    uint32_t pid = bfs_be32(*(const uint32_t *)k);
    if (pid != sc->parent_id) return false; /* different parent, stop */

    uint8_t name_len = k[8];
    return sc->cb((const char *)(k + 9), name_len,
                  bfs_be32(v->inode_nr), bfs_be32(v->entry_type), sc->ctx);
}

bfs_err_t bfs_dir_scan(bfs_dir_tree_t *dt, uint32_t parent_id,
                         bfs_dir_scan_cb cb, void *ctx)
{
    /* Build a start key with parent_id and zeros for the rest */
    uint8_t start_key[DIR_KEY_SIZE];
    memset(start_key, 0, DIR_KEY_SIZE);
    *(uint32_t *)start_key = bfs_be32(parent_id);

    dir_scan_ctx_t sc = { .parent_id = parent_id, .cb = cb, .ctx = ctx };
    return bfs_btree_scan(&dt->tree, start_key, dir_scan_cb, &sc);
}
