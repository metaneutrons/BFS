/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Directory B+tree
 *
 * Records (see bfs_ondisk.h): a 12-byte key, ordered as plain bytes,
 *   owner u32 | kind u8 | name_hash u32 | ordinal u16 | part u8
 * and a 40-byte value. An entry is a head record (part 0) with the first 33
 * name bytes, followed in key order by up to six continuation parts of 40
 * bytes. The hash is that of the case-folded name, so names are compared only
 * among the entries of one hash; the ordinal tells those apart.
 *
 * Every mutation changes all records of an entry, parent link or comment, or
 * none of them. A partial change is undone; if the undo fails, sticky_err is
 * set and the caller must not publish the transaction.
 */

#include "bfs_dir.h"
#include <string.h>

#define KEY_SIZE    BFS_DIR_KEY_SIZE
#define VAL_SIZE    BFS_DIR_VAL_SIZE
#define INLINE_NAME BFS_DIR_INLINE_NAME
#define PART_BYTES  BFS_DIR_PART_BYTES
#define MAX_PARTS   BFS_DIR_MAX_PARTS
#define MAX_ORDINAL 0xFFFFu

/* ── Amiga international case folding ──────────────────────── */

/* a-z → A-Z and à-þ (0xE0-0xFE) → À-Þ (0xC0-0xDE), except ÷ (0xF7). A table
 * keeps the per-byte cost of hashing and comparing names to one load. */
static const uint8_t fold_table[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xF7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xFF,
};

uint8_t bfs_intl_toupper(uint8_t c)
{
    return fold_table[c];
}

/* ── FNV-1a hash ───────────────────────────────────────────── */

uint32_t bfs_dir_name_hash(const char *name, uint8_t len)
{
    uint32_t h = BFS_DIR_HASH_FNV_OFFSET;
    if (!name && len != 0) return h;
    const uint8_t *bytes = (const uint8_t *)name;
    for (uint8_t i = 0; i < len; i++) {
        h ^= fold_table[bytes[i]];
        h *= BFS_DIR_HASH_FNV_PRIME;
    }
    return h;
}

/* ── Record helpers ────────────────────────────────────────── */

static void make_key(uint8_t key[KEY_SIZE], uint32_t owner, uint8_t kind,
                     uint32_t hash, uint16_t ordinal, uint8_t part)
{
    bfs_store_be32(key + BFS_DIR_KEY_OWNER, owner);
    key[BFS_DIR_KEY_KIND] = kind;
    bfs_store_be32(key + BFS_DIR_KEY_HASH, hash);
    bfs_store_be16(key + BFS_DIR_KEY_ORDINAL, ordinal);
    key[BFS_DIR_KEY_PART] = part;
}

/* Byte order of the 12 key bytes, compared as three big-endian words. */
static int dir_key_compare(const void *a, const void *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    uint32_t u = bfs_load_be32(x), v = bfs_load_be32(y);
    if (u == v) {
        u = bfs_load_be32(x + 4);
        v = bfs_load_be32(y + 4);
        if (u == v) {
            u = bfs_load_be32(x + 8);
            v = bfs_load_be32(y + 8);
        }
    }
    return u < v ? -1 : u > v;
}

static bool all_zero(const uint8_t *bytes, uint32_t count)
{
    uint32_t i = 0, any = 0;
    for (; i + 4 <= count; i += 4) {
        any |= bfs_load_be32(bytes + i);
    }
    for (; i < count; i++) any |= bytes[i];
    return any == 0;
}

/* Continuation parts of a name of this length. */
static uint8_t name_parts(uint8_t len)
{
    return len <= INLINE_NAME
               ? 0 : (uint8_t)((len - INLINE_NAME + PART_BYTES - 1) / PART_BYTES);
}

static bool is_dot_name(const char *name, uint8_t len)
{
    return (len == 1 && name[0] == '.') ||
           (len == 2 && name[0] == '.' && name[1] == '.');
}

static bool names_equal_folded(const char *a, uint8_t a_len,
                               const char *b, uint8_t b_len)
{
    if (a_len != b_len) return false;
    for (uint8_t i = 0; i < a_len; i++)
        if (fold_table[(uint8_t)a[i]] != fold_table[(uint8_t)b[i]]) return false;
    return true;
}

static bool inode_number_valid(uint32_t ino)
{
    return ino != 0 && ino < 0x80000000u;
}

/* Each record is checked on its own; what depends on several records (the
 * parts of a long name, the hash of the whole name) is checked where they are
 * assembled. Validated leaves hold only records that pass. */
static bool dir_entry_ok(const void *key, const void *val)
{
    const uint8_t *k = (const uint8_t *)key;
    const uint8_t *v = (const uint8_t *)val;
    uint32_t owner = bfs_load_be32(k + BFS_DIR_KEY_OWNER);
    uint8_t kind = k[BFS_DIR_KEY_KIND];
    uint32_t hash = bfs_load_be32(k + BFS_DIR_KEY_HASH);
    uint16_t ordinal = bfs_load_be16(k + BFS_DIR_KEY_ORDINAL);
    uint8_t part = k[BFS_DIR_KEY_PART];
    if (owner >= 0x80000000u) return false;

    if (kind == BFS_DIR_KIND_ENTRY) {
        if (part > MAX_PARTS) return false;
        if (part != 0) return owner != 0; /* the root's name "/" has no parts */
        uint32_t ino = bfs_load_be32(v + BFS_DIR_HEAD_INODE);
        uint8_t type = v[BFS_DIR_HEAD_TYPE];
        uint8_t len = v[BFS_DIR_HEAD_NAME_LEN];
        const char *name = (const char *)(v + BFS_DIR_HEAD_NAME);
        uint8_t inline_len = len < INLINE_NAME ? len : INLINE_NAME;
        if (!inode_number_valid(ino) || type > BFS_INODE_HARDLINK || len == 0 ||
            v[BFS_DIR_HEAD_FLAGS] != 0 || is_dot_name(name, len) ||
            !all_zero(v + BFS_DIR_HEAD_NAME + inline_len, INLINE_NAME - inline_len))
            return false;
        if (len <= INLINE_NAME && bfs_dir_name_hash(name, len) != hash) return false;
        if (owner == 0)
            return len == 1 && name[0] == '/' && ordinal == 0 &&
                   ino == BFS_ROOT_INO && type == BFS_INODE_DIR;
        return true;
    }
    if (hash != 0 || ordinal != 0 || owner == 0) return false;
    if (kind == BFS_DIR_KIND_PARENT)
        return part == 0 && owner != BFS_ROOT_INO &&
               inode_number_valid(bfs_load_be32(v + BFS_DIR_PARENT_INODE)) &&
               all_zero(v + 4, VAL_SIZE - 4);
    if (kind == BFS_DIR_KIND_COMMENT) {
        if (part > 1) return false;
        if (part == 1) return true;
        uint8_t len = v[0];
        uint8_t inline_len = len < BFS_DIR_COMMENT_INLINE ? len : BFS_DIR_COMMENT_INLINE;
        return len != 0 && len <= BFS_DIR_COMMENT_MAX &&
               all_zero(v + 1 + inline_len, BFS_DIR_COMMENT_INLINE - inline_len);
    }
    return false;
}

static const bfs_btree_ops_t dir_ops = {
    .key_compare = dir_key_compare,
    .key_size = KEY_SIZE,
    .val_size = VAL_SIZE,
    .cache_key_order = true,
    .entry_ok = dir_entry_ok,
};

static void latch_undo_failure(bfs_dir_tree_t *dt, bfs_err_t err)
{
    if (dt->sticky_err == BFS_OK) dt->sticky_err = err;
}

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_dir_init(bfs_dir_tree_t *dt, bfs_bio_t *bio,
                   bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id)
{
    if (!dt) return BFS_ERR_INVAL;
    dt->sticky_err = BFS_OK;
    return bfs_btree_init(&dt->tree, bio, alloc, &dir_ops, root, txn_id);
}

/* ── Records of one entry, parent link or comment ──────────── */

typedef struct {
    uint32_t owner;
    uint8_t kind;
    uint32_t hash;
    uint16_t ordinal;
    uint8_t head[VAL_SIZE];
    uint8_t parts[MAX_PARTS][VAL_SIZE];
    uint8_t part_count;
} dir_records_t;

static void build_entry(dir_records_t *r, uint32_t owner, uint32_t hash,
                        uint16_t ordinal, const char *name, uint8_t len,
                        uint32_t ino, uint8_t type)
{
    memset(r, 0, sizeof(*r));
    r->owner = owner;
    r->kind = BFS_DIR_KIND_ENTRY;
    r->hash = hash;
    r->ordinal = ordinal;
    bfs_store_be32(r->head + BFS_DIR_HEAD_INODE, ino);
    r->head[BFS_DIR_HEAD_TYPE] = type;
    r->head[BFS_DIR_HEAD_NAME_LEN] = len;
    memcpy(r->head + BFS_DIR_HEAD_NAME, name, len < INLINE_NAME ? len : INLINE_NAME); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    r->part_count = name_parts(len);
    for (uint8_t i = 0; i < r->part_count; i++) {
        uint32_t offset = INLINE_NAME + (uint32_t)i * PART_BYTES;
        uint32_t chunk = len - offset;
        if (chunk > PART_BYTES) chunk = PART_BYTES;
        memcpy(r->parts[i], name + offset, chunk); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    }
}

static void records_key(const dir_records_t *r, uint8_t part, uint8_t key[KEY_SIZE])
{
    make_key(key, r->owner, r->kind, r->hash, r->ordinal, part);
}

/* A tree call that fails with free_sink_err set may still have applied its
 * change: btree.c publishes a finished mutation even when retiring its old
 * nodes failed. The undo cannot know which, and its own tree calls clear
 * free_sink_err, so latch the error first: the transaction must not be
 * published. Returns whether the failed step may have been applied. */
static bool step_may_have_applied(bfs_dir_tree_t *dt)
{
    if (dt->tree.free_sink_err == BFS_OK) return false;
    latch_undo_failure(dt, dt->tree.free_sink_err);
    return true;
}

static void undo_insert(bfs_dir_tree_t *dt, const dir_records_t *r,
                        uint8_t first_part, uint8_t last_part)
{
    uint8_t key[KEY_SIZE];
    for (uint8_t part = first_part; part != 0 && part <= last_part; part++) {
        records_key(r, part, key);
        bfs_err_t err = bfs_btree_delete(&dt->tree, key);
        if (err != BFS_OK) latch_undo_failure(dt, err);
    }
}

/* Parts first, head last: until the head exists, nothing is visible. */
static bfs_err_t insert_records(bfs_dir_tree_t *dt, const dir_records_t *r)
{
    uint8_t key[KEY_SIZE];
    for (uint8_t part = r->part_count; part >= 1; part--) {
        records_key(r, part, key);
        bfs_err_t err = bfs_btree_insert(&dt->tree, key, r->parts[part - 1]);
        if (err != BFS_OK) {
            bool applied = step_may_have_applied(dt);
            undo_insert(dt, r, applied ? part : (uint8_t)(part + 1), r->part_count);
            return err;
        }
    }
    records_key(r, 0, key);
    bfs_err_t err = bfs_btree_insert(&dt->tree, key, r->head);
    if (err != BFS_OK) {
        if (step_may_have_applied(dt)) (void)bfs_btree_delete(&dt->tree, key);
        undo_insert(dt, r, 1, r->part_count);
    }
    return err;
}

static void undo_delete(bfs_dir_tree_t *dt, const dir_records_t *r, uint8_t deleted_parts)
{
    uint8_t key[KEY_SIZE];
    for (uint8_t part = 1; part <= deleted_parts; part++) {
        records_key(r, part, key);
        bfs_err_t err = bfs_btree_insert(&dt->tree, key, r->parts[part - 1]);
        if (err != BFS_OK) latch_undo_failure(dt, err);
    }
    records_key(r, 0, key);
    bfs_err_t err = bfs_btree_insert(&dt->tree, key, r->head);
    if (err != BFS_OK) latch_undo_failure(dt, err);
}

/* Head first, so a failure leaves at most invisible parts, which are restored. */
static bfs_err_t delete_records(bfs_dir_tree_t *dt, const dir_records_t *r)
{
    uint8_t key[KEY_SIZE];
    records_key(r, 0, key);
    bfs_err_t err = bfs_btree_delete(&dt->tree, key);
    if (err != BFS_OK) {
        if (step_may_have_applied(dt)) undo_delete(dt, r, 0);
        return err;
    }
    for (uint8_t part = 1; part <= r->part_count; part++) {
        records_key(r, part, key);
        err = bfs_btree_delete(&dt->tree, key);
        if (err != BFS_OK) {
            bool applied = step_may_have_applied(dt);
            undo_delete(dt, r, applied ? part : (uint8_t)(part - 1));
            return err;
        }
    }
    return BFS_OK;
}

/* Rewrite the values of existing records from old_r to new_r (same keys). */
static bfs_err_t update_records(bfs_dir_tree_t *dt, const dir_records_t *old_r,
                                const dir_records_t *new_r)
{
    uint8_t key[KEY_SIZE];
    for (uint8_t part = 0; part <= new_r->part_count; part++) {
        records_key(new_r, part, key);
        const uint8_t *value = part == 0 ? new_r->head : new_r->parts[part - 1];
        bfs_err_t err = bfs_btree_update(&dt->tree, key, value);
        if (err != BFS_OK) {
            uint8_t changed = step_may_have_applied(dt) ? (uint8_t)(part + 1) : part;
            for (uint8_t undo = 0; undo < changed; undo++) {
                records_key(old_r, undo, key);
                const uint8_t *old_value = undo == 0 ? old_r->head : old_r->parts[undo - 1];
                bfs_err_t undo_err = bfs_btree_update(&dt->tree, key, old_value);
                if (undo_err != BFS_OK) latch_undo_failure(dt, undo_err);
            }
            return err;
        }
    }
    return BFS_OK;
}

/* Read the head and parts of the entry at r->owner/hash/ordinal into r. */
static bfs_err_t load_entry(bfs_dir_tree_t *dt, dir_records_t *r)
{
    uint8_t key[KEY_SIZE];
    records_key(r, 0, key);
    bfs_err_t err = bfs_btree_search(&dt->tree, key, r->head);
    if (err != BFS_OK) return err;
    uint8_t len = r->head[BFS_DIR_HEAD_NAME_LEN];
    r->part_count = name_parts(len);
    for (uint8_t part = 1; part <= r->part_count; part++) {
        records_key(r, part, key);
        err = bfs_btree_search(&dt->tree, key, r->parts[part - 1]);
        if (err != BFS_OK) return err == BFS_ERR_NOTFOUND ? BFS_ERR_CORRUPT : err;
    }
    return BFS_OK;
}

/* Assemble the name held by r; checks the tail padding and the hash. */
static bfs_err_t records_name(const dir_records_t *r, char name[BFS_NAME_MAX],
                              uint8_t *len_out)
{
    uint8_t len = r->head[BFS_DIR_HEAD_NAME_LEN];
    memcpy(name, r->head + BFS_DIR_HEAD_NAME, len < INLINE_NAME ? len : INLINE_NAME); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    for (uint8_t i = 0; i < r->part_count; i++) {
        uint32_t offset = INLINE_NAME + (uint32_t)i * PART_BYTES;
        uint32_t chunk = len - offset;
        if (chunk > PART_BYTES) chunk = PART_BYTES;
        if (!all_zero(r->parts[i] + chunk, PART_BYTES - chunk)) return BFS_ERR_CORRUPT;
        memcpy(name + offset, r->parts[i], chunk); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    }
    if (bfs_dir_name_hash(name, len) != r->hash) return BFS_ERR_CORRUPT;
    *len_out = len;
    return BFS_OK;
}

/* ── Scan ──────────────────────────────────────────────────── */

typedef struct {
    uint32_t owner;
    /* Lookups and inserts stop at the end of one hash. */
    bool hash_bound;
    uint32_t bound_hash;
    /* Resume points: skip the entry at skip, or every entry with skip_hash. */
    bool skip_entry;
    bfs_dir_pos_t skip;
    bool skip_group;
    uint32_t skip_hash;
    /* The entry being assembled from its head and parts. */
    bool partial;
    uint8_t parts_expected;
    uint8_t parts_seen;
    uint8_t name_len;
    uint32_t ino;
    uint8_t type;
    bfs_dir_pos_t pos;
    char name[BFS_NAME_MAX];
    bfs_dir_scan_cb cb;
    bfs_dir_scan_pos_cb pos_cb;
    void *ctx;
    bfs_err_t err;
    bool boundary_stop;
} dir_scan_ctx_t;

static bool scan_deliver(dir_scan_ctx_t *sc)
{
    if (sc->pos_cb)
        return sc->pos_cb(sc->name, sc->name_len, sc->ino, sc->type, &sc->pos, sc->ctx);
    return sc->cb(sc->name, sc->name_len, sc->ino, sc->type, sc->ctx);
}

static bool scan_corrupt(dir_scan_ctx_t *sc)
{
    sc->err = BFS_ERR_CORRUPT;
    return false;
}

/* A continuation part of the entry the scan is assembling. */
static bool scan_continuation(dir_scan_ctx_t *sc, const uint8_t *v, uint32_t hash,
                              uint16_t ordinal, uint8_t part)
{
    if (!sc->partial || hash != sc->pos.hash || ordinal != sc->pos.ordinal ||
        part != sc->parts_seen + 1)
        return scan_corrupt(sc);
    uint32_t offset = INLINE_NAME + (uint32_t)sc->parts_seen * PART_BYTES;
    uint32_t chunk = sc->name_len - offset;
    if (chunk > PART_BYTES) chunk = PART_BYTES;
    if (!all_zero(v + chunk, PART_BYTES - chunk)) return scan_corrupt(sc);
    memcpy(sc->name + offset, v, chunk); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (++sc->parts_seen < sc->parts_expected) return true;
    sc->partial = false;
    if (bfs_dir_name_hash(sc->name, sc->name_len) != hash) return scan_corrupt(sc);
    return scan_deliver(sc);
}

/* Called for each record; reports an entry when its last record is seen, so a
 * callback that stops the scan stops it at the end of that entry. */
static bool dir_scan_record(const void *key, const void *val, void *ctx)
{
    dir_scan_ctx_t *sc = (dir_scan_ctx_t *)ctx;
    const uint8_t *k = (const uint8_t *)key;
    const uint8_t *v = (const uint8_t *)val;
    uint32_t hash = bfs_load_be32(k + BFS_DIR_KEY_HASH);
    uint16_t ordinal = bfs_load_be16(k + BFS_DIR_KEY_ORDINAL);
    uint8_t part = k[BFS_DIR_KEY_PART];

    if (bfs_load_be32(k + BFS_DIR_KEY_OWNER) != sc->owner ||
        k[BFS_DIR_KEY_KIND] != BFS_DIR_KIND_ENTRY ||
        (sc->hash_bound && hash != sc->bound_hash)) {
        if (sc->partial) return scan_corrupt(sc);
        sc->boundary_stop = true;
        return false;
    }
    if (sc->skip_entry) {
        if (hash == sc->skip.hash && ordinal == sc->skip.ordinal) return true;
        sc->skip_entry = false;
    }
    if (sc->skip_group) {
        if (hash == sc->skip_hash) return true;
        sc->skip_group = false;
    }

    if (part == 0) {
        if (sc->partial) return scan_corrupt(sc);
        uint8_t len = v[BFS_DIR_HEAD_NAME_LEN];
        sc->ino = bfs_load_be32(v + BFS_DIR_HEAD_INODE);
        sc->type = v[BFS_DIR_HEAD_TYPE];
        sc->name_len = len;
        sc->pos.hash = hash;
        sc->pos.ordinal = ordinal;
        memcpy(sc->name, v + BFS_DIR_HEAD_NAME, len < INLINE_NAME ? len : INLINE_NAME); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        sc->parts_expected = name_parts(len);
        sc->parts_seen = 0;
        /* Leaf validation has checked a short name's hash (dir_entry_ok). */
        if (sc->parts_expected == 0) return scan_deliver(sc);
        sc->partial = true;
        return true;
    }

    return scan_continuation(sc, v, hash, ordinal, part);
}

static bfs_err_t run_scan(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                          const uint8_t start[KEY_SIZE], dir_scan_ctx_t *sc)
{
    bfs_err_t err = bfs_btree_scan_cursor(&dt->tree, cursor, start, dir_scan_record, sc);
    if (cursor && (sc->boundary_stop || sc->err != BFS_OK)) cursor->stopped = false;
    if (sc->err != BFS_OK) return sc->err;
    if (err != BFS_OK) return err;
    return sc->partial ? BFS_ERR_CORRUPT : BFS_OK;
}

/* ── Finding an entry by name ──────────────────────────────── */

typedef struct {
    const char *name;
    uint8_t len;
    bool stop_on_match;
    bool found;
    uint32_t ino;
    uint32_t type;
    bfs_dir_pos_t pos;
    /* The lowest free ordinal of the hash: the first gap, else the end. */
    uint32_t next_ordinal;
    bool gap;
    uint32_t free_ordinal;
} dir_find_t;

static bool find_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                    uint32_t entry_type, const bfs_dir_pos_t *pos, void *ctx)
{
    dir_find_t *find = (dir_find_t *)ctx;
    if (!find->gap && pos->ordinal != find->next_ordinal) {
        find->gap = true;
        find->free_ordinal = find->next_ordinal;
    }
    find->next_ordinal = (uint32_t)pos->ordinal + 1;
    if (!find->found && names_equal_folded(name, name_len, find->name, find->len)) {
        find->found = true;
        find->ino = inode_nr;
        find->type = entry_type;
        find->pos = *pos;
        if (find->stop_on_match) return false;
    }
    return true;
}

/* Walk every entry whose name has this hash. */
static bfs_err_t find_in_hash(bfs_dir_tree_t *dt, uint32_t owner, uint32_t hash,
                              dir_find_t *find)
{
    uint8_t start[KEY_SIZE];
    make_key(start, owner, BFS_DIR_KIND_ENTRY, hash, 0, 0);
    dir_scan_ctx_t sc = {
        .owner = owner, .hash_bound = true, .bound_hash = hash,
        .pos_cb = find_cb, .ctx = find, .err = BFS_OK,
    };
    return run_scan(dt, NULL, start, &sc);
}

/* Find an entry by name. The common case, a short name alone with its hash at
 * ordinal 0, is one search that the tree's location hints can answer. */
static bfs_err_t find_entry(bfs_dir_tree_t *dt, uint32_t owner,
                            const char *name, uint8_t len, dir_find_t *find)
{
    uint32_t hash = bfs_dir_name_hash(name, len);
    memset(find, 0, sizeof(*find));
    find->name = name;
    find->len = len;
    find->stop_on_match = true;

    uint8_t key[KEY_SIZE], head[VAL_SIZE];
    make_key(key, owner, BFS_DIR_KIND_ENTRY, hash, 0, 0);
    bfs_err_t err = bfs_btree_search(&dt->tree, key, head);
    if (err == BFS_OK) {
        uint8_t stored_len = head[BFS_DIR_HEAD_NAME_LEN];
        if (stored_len <= INLINE_NAME &&
            names_equal_folded((const char *)(head + BFS_DIR_HEAD_NAME), stored_len,
                               name, len)) {
            find->found = true;
            find->ino = bfs_load_be32(head + BFS_DIR_HEAD_INODE);
            find->type = head[BFS_DIR_HEAD_TYPE];
            find->pos.hash = hash;
            find->pos.ordinal = 0;
            return BFS_OK;
        }
    } else if (err == BFS_ERR_NOTFOUND) {
        /* Ordinal 0 can be free while a later ordinal of the hash is used. */
        uint8_t found[KEY_SIZE];
        err = bfs_btree_lower_bound(&dt->tree, key, found);
        if (err == BFS_ERR_NOTFOUND) return BFS_OK;
        if (err != BFS_OK && err != BFS_ERR_AGAIN) return err;
        if (memcmp(found, key, BFS_DIR_KEY_ORDINAL) != 0) return BFS_OK;
    } else {
        return err;
    }
    return find_in_hash(dt, owner, hash, find);
}

/* ── Lookup ────────────────────────────────────────────────── */

bfs_err_t bfs_dir_lookup_pos(bfs_dir_tree_t *dt, uint32_t parent_id,
                             const char *name, uint8_t name_len,
                             uint32_t *inode_nr_out, uint32_t *type_out,
                             bfs_dir_pos_t *pos_out)
{
    if (!dt || !name || name_len == 0) return BFS_ERR_INVAL;
    dir_find_t find;
    bfs_err_t err = find_entry(dt, parent_id, name, name_len, &find);
    if (err != BFS_OK) return err;
    if (!find.found) return BFS_ERR_NOTFOUND;
    if (inode_nr_out) *inode_nr_out = find.ino;
    if (type_out) *type_out = find.type;
    if (pos_out) *pos_out = find.pos;
    return BFS_OK;
}

bfs_err_t bfs_dir_lookup(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t *inode_nr_out, uint32_t *type_out)
{
    return bfs_dir_lookup_pos(dt, parent_id, name, name_len, inode_nr_out,
                              type_out, NULL);
}

/* ── Insert ────────────────────────────────────────────────── */

static bfs_err_t dir_insert(bfs_dir_tree_t *dt, uint32_t parent_id,
                            const char *name, uint8_t name_len,
                            uint32_t inode_nr, uint32_t entry_type,
                            const bfs_dir_pos_t *at)
{
    if (!dt || !name || name_len == 0 || is_dot_name(name, name_len) ||
        !inode_number_valid(inode_nr) || entry_type > BFS_INODE_HARDLINK ||
        parent_id >= 0x80000000u)
        return BFS_ERR_INVAL;
    if (parent_id == 0 &&
        (name_len != 1 || name[0] != '/' || inode_nr != BFS_ROOT_INO ||
         entry_type != BFS_INODE_DIR))
        return BFS_ERR_INVAL;

    uint32_t hash = bfs_dir_name_hash(name, name_len);
    dir_find_t find;
    memset(&find, 0, sizeof(find));
    find.name = name;
    find.len = name_len;
    bfs_err_t err = find_in_hash(dt, parent_id, hash, &find);
    if (err != BFS_OK) return err;
    if (find.found) return BFS_ERR_EXISTS;
    uint32_t ordinal = find.gap ? find.free_ordinal : find.next_ordinal;
    if (at) {
        /* A taken ordinal fails in insert_records with BFS_ERR_EXISTS. */
        if (at->hash != hash) return BFS_ERR_INVAL;
        ordinal = at->ordinal;
    }
    if (ordinal > MAX_ORDINAL) return BFS_ERR_OVERFLOW;
    if (parent_id == 0 && ordinal != 0) return BFS_ERR_EXISTS;

    dir_records_t records;
    build_entry(&records, parent_id, hash, (uint16_t)ordinal, name, name_len,
                inode_nr, (uint8_t)entry_type);
    return insert_records(dt, &records);
}

bfs_err_t bfs_dir_insert(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t inode_nr, uint32_t entry_type)
{
    return dir_insert(dt, parent_id, name, name_len, inode_nr, entry_type, NULL);
}

bfs_err_t bfs_dir_insert_at(bfs_dir_tree_t *dt, uint32_t parent_id,
                            const char *name, uint8_t name_len,
                            uint32_t inode_nr, uint32_t entry_type,
                            const bfs_dir_pos_t *pos)
{
    if (!pos) return BFS_ERR_INVAL;
    return dir_insert(dt, parent_id, name, name_len, inode_nr, entry_type, pos);
}

/* Load the records of the entry named name, or return NOTFOUND. */
static bfs_err_t load_named_entry(bfs_dir_tree_t *dt, uint32_t parent_id,
                                  const char *name, uint8_t name_len,
                                  dir_records_t *records)
{
    dir_find_t find;
    bfs_err_t err = find_entry(dt, parent_id, name, name_len, &find);
    if (err != BFS_OK) return err;
    if (!find.found) return BFS_ERR_NOTFOUND;
    memset(records, 0, sizeof(*records));
    records->owner = parent_id;
    records->kind = BFS_DIR_KIND_ENTRY;
    records->hash = find.pos.hash;
    records->ordinal = find.pos.ordinal;
    return load_entry(dt, records);
}

bfs_err_t bfs_dir_replace(bfs_dir_tree_t *dt, uint32_t parent_id,
                          const char *name, uint8_t name_len,
                          uint32_t inode_nr, uint32_t entry_type,
                          uint32_t *old_inode_out, uint32_t *old_type_out)
{
    if (!dt || !name || name_len == 0 || !inode_number_valid(inode_nr) ||
        entry_type > BFS_INODE_HARDLINK)
        return BFS_ERR_INVAL;
    dir_find_t find;
    bfs_err_t err = find_entry(dt, parent_id, name, name_len, &find);
    if (err != BFS_OK) return err;
    if (!find.found) return BFS_ERR_NOTFOUND;

    uint8_t key[KEY_SIZE], head[VAL_SIZE];
    make_key(key, parent_id, BFS_DIR_KIND_ENTRY, find.pos.hash, find.pos.ordinal, 0);
    err = bfs_btree_search(&dt->tree, key, head);
    if (err != BFS_OK) return err == BFS_ERR_NOTFOUND ? BFS_ERR_CORRUPT : err;
    /* Only the inode and type change; the stored spelling stays. */
    bfs_store_be32(head + BFS_DIR_HEAD_INODE, inode_nr);
    head[BFS_DIR_HEAD_TYPE] = (uint8_t)entry_type;
    err = bfs_btree_update(&dt->tree, key, head);
    if (err != BFS_OK) return err;
    if (old_inode_out) *old_inode_out = find.ino;
    if (old_type_out) *old_type_out = find.type;
    return BFS_OK;
}

bfs_err_t bfs_dir_rekey_case(bfs_dir_tree_t *dt, uint32_t parent_id,
                             const char *old_name, uint8_t old_len,
                             const char *new_name, uint8_t new_len)
{
    if (!dt || !old_name || !new_name || old_len == 0 || new_len == 0 ||
        !names_equal_folded(old_name, old_len, new_name, new_len))
        return BFS_ERR_INVAL;
    dir_records_t old_records;
    bfs_err_t err = load_named_entry(dt, parent_id, old_name, old_len, &old_records);
    if (err != BFS_OK) return err;
    dir_records_t new_records;
    build_entry(&new_records, parent_id, old_records.hash, old_records.ordinal,
                new_name, new_len,
                bfs_load_be32(old_records.head + BFS_DIR_HEAD_INODE),
                old_records.head[BFS_DIR_HEAD_TYPE]);
    /* Equal folded names have equal lengths, so the keys and parts stay. */
    return update_records(dt, &old_records, &new_records);
}

/* ── Remove ────────────────────────────────────────────────── */

bfs_err_t bfs_dir_remove(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len)
{
    if (!dt || !name || name_len == 0) return BFS_ERR_INVAL;
    dir_records_t records;
    bfs_err_t err = load_named_entry(dt, parent_id, name, name_len, &records);
    if (err != BFS_OK) return err;
    char stored[BFS_NAME_MAX];
    uint8_t stored_len;
    err = records_name(&records, stored, &stored_len);
    if (err != BFS_OK) return err;
    return delete_records(dt, &records);
}

/* ── Scan entry points ─────────────────────────────────────── */

/* Scan parent_id's entries after the position after, or after every entry
 * whose name hashes to *group_hash, or from the first entry. */
static bfs_err_t scan_from(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                           uint32_t parent_id, const bfs_dir_pos_t *after,
                           const uint32_t *group_hash, bfs_dir_scan_cb cb,
                           bfs_dir_scan_pos_cb pos_cb, void *ctx)
{
    uint8_t start[KEY_SIZE];
    dir_scan_ctx_t sc = {
        .owner = parent_id, .cb = cb, .pos_cb = pos_cb, .ctx = ctx,
        .err = BFS_OK,
    };
    if (after) {
        make_key(start, parent_id, BFS_DIR_KIND_ENTRY, after->hash, after->ordinal, 0);
        sc.skip_entry = true;
        sc.skip = *after;
    } else if (group_hash) {
        make_key(start, parent_id, BFS_DIR_KIND_ENTRY, *group_hash, 0, 0);
        sc.skip_group = true;
        sc.skip_hash = *group_hash;
    } else {
        make_key(start, parent_id, BFS_DIR_KIND_ENTRY, 0, 0, 0);
    }
    return run_scan(dt, cursor, start, &sc);
}

bfs_err_t bfs_dir_may_have_entries(bfs_dir_tree_t *dt, uint32_t parent_id,
                                   bool *maybe)
{
    if (!dt || !maybe) return BFS_ERR_INVAL;
    uint8_t start[KEY_SIZE], found[KEY_SIZE];
    make_key(start, parent_id, BFS_DIR_KIND_ENTRY, 0, 0, 0);
    bfs_err_t err = bfs_btree_lower_bound(&dt->tree, start, found);
    if (err == BFS_ERR_NOTFOUND) {
        *maybe = false;
        return BFS_OK;
    }
    if (err != BFS_OK && err != BFS_ERR_AGAIN) return err;
    /* A found key, or the bound above which all greater keys lie: entries of
     * parent_id are possible only if it is still in parent_id's entry range. */
    *maybe = bfs_load_be32(found + BFS_DIR_KEY_OWNER) == parent_id &&
             found[BFS_DIR_KEY_KIND] == BFS_DIR_KIND_ENTRY;
    return BFS_OK;
}

bfs_err_t bfs_dir_scan_cursor(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                              uint32_t parent_id, const char *name,
                              uint8_t name_len, bfs_dir_scan_cb cb, void *ctx)
{
    if (!dt || !cb || (name_len != 0 && !name)) return BFS_ERR_INVAL;
    if (name_len == 0)
        return scan_from(dt, cursor, parent_id, NULL, NULL, cb, NULL, ctx);
    bfs_dir_pos_t pos;
    bfs_err_t err = bfs_dir_lookup_pos(dt, parent_id, name, name_len, NULL, NULL, &pos);
    if (err == BFS_OK)
        return scan_from(dt, cursor, parent_id, &pos, NULL, cb, NULL, ctx);
    if (err != BFS_ERR_NOTFOUND) return err;
    /* The entry is gone; its ordinal is unknown, so continue after its hash. */
    uint32_t group_hash = bfs_dir_name_hash(name, name_len);
    return scan_from(dt, cursor, parent_id, NULL, &group_hash, cb, NULL, ctx);
}

bfs_err_t bfs_dir_scan_cursor_pos(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                                  uint32_t parent_id, const bfs_dir_pos_t *after,
                                  bfs_dir_scan_pos_cb cb, void *ctx)
{
    if (!dt || !cb) return BFS_ERR_INVAL;
    return scan_from(dt, cursor, parent_id, after, NULL, NULL, cb, ctx);
}

static bfs_err_t scan_resume(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                             uint32_t parent_id, bfs_dir_scan_cb cb,
                             bfs_dir_scan_pos_cb pos_cb, void *ctx)
{
    if (!dt || !dt->tree.bio || !dt->tree.bio->ops || !dt->tree.ops || (!cb && !pos_cb))
        return BFS_ERR_INVAL;
    if (!cursor) return BFS_ERR_AGAIN;

    /* The stop is the last record of the entry reported last. */
    const uint8_t *stop_key =
        (const uint8_t *)bfs_btree_cursor_stop_key(&dt->tree, cursor);
    if (!stop_key || bfs_load_be32(stop_key + BFS_DIR_KEY_OWNER) != parent_id ||
        stop_key[BFS_DIR_KEY_KIND] != BFS_DIR_KIND_ENTRY)
        return BFS_ERR_AGAIN;

    dir_scan_ctx_t sc = {
        .owner = parent_id, .cb = cb, .pos_cb = pos_cb, .ctx = ctx, .err = BFS_OK,
    };
    bfs_err_t err = bfs_btree_scan_cursor_resume(&dt->tree, cursor, dir_scan_record, &sc);
    if (sc.boundary_stop || sc.err != BFS_OK) cursor->stopped = false;
    if (sc.err != BFS_OK) return sc.err;
    if (err != BFS_OK) return err;
    return sc.partial ? BFS_ERR_CORRUPT : BFS_OK;
}

bfs_err_t bfs_dir_scan_resume(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                              uint32_t parent_id, bfs_dir_scan_cb cb, void *ctx)
{
    if (!cb) return BFS_ERR_INVAL;
    return scan_resume(dt, cursor, parent_id, cb, NULL, ctx);
}

bfs_err_t bfs_dir_scan_resume_pos(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                                  uint32_t parent_id, bfs_dir_scan_pos_cb cb,
                                  void *ctx)
{
    if (!cb) return BFS_ERR_INVAL;
    return scan_resume(dt, cursor, parent_id, NULL, cb, ctx);
}

bfs_err_t bfs_dir_scan(bfs_dir_tree_t *dt, uint32_t parent_id,
                         bfs_dir_scan_cb cb, void *ctx)
{
    return bfs_dir_scan_cursor(dt, NULL, parent_id, NULL, 0, cb, ctx);
}

bfs_err_t bfs_dir_scan_after(bfs_dir_tree_t *dt, uint32_t parent_id,
                             const char *name, uint8_t name_len,
                             bfs_dir_scan_cb cb, void *ctx)
{
    if (!name || name_len == 0) return BFS_ERR_INVAL;
    return bfs_dir_scan_cursor(dt, NULL, parent_id, name, name_len, cb, ctx);
}

/* ── Parent links ──────────────────────────────────────────── */

static bool parent_link_valid(uint32_t dir_ino)
{
    return inode_number_valid(dir_ino) && dir_ino != BFS_ROOT_INO;
}

bfs_err_t bfs_dir_parent_get(bfs_dir_tree_t *dt, uint32_t dir_ino,
                             uint32_t *parent_out)
{
    if (!dt || !parent_link_valid(dir_ino)) return BFS_ERR_INVAL;
    uint8_t key[KEY_SIZE], value[VAL_SIZE];
    make_key(key, dir_ino, BFS_DIR_KIND_PARENT, 0, 0, 0);
    bfs_err_t err = bfs_btree_search(&dt->tree, key, value);
    if (err != BFS_OK) return err;
    if (parent_out) *parent_out = bfs_load_be32(value + BFS_DIR_PARENT_INODE);
    return BFS_OK;
}

bfs_err_t bfs_dir_parent_insert(bfs_dir_tree_t *dt, uint32_t dir_ino,
                                uint32_t parent)
{
    if (!dt || !parent_link_valid(dir_ino) || !inode_number_valid(parent))
        return BFS_ERR_INVAL;
    uint8_t key[KEY_SIZE], value[VAL_SIZE];
    make_key(key, dir_ino, BFS_DIR_KIND_PARENT, 0, 0, 0);
    memset(value, 0, sizeof(value));
    bfs_store_be32(value + BFS_DIR_PARENT_INODE, parent);
    return bfs_btree_insert(&dt->tree, key, value);
}

bfs_err_t bfs_dir_parent_replace(bfs_dir_tree_t *dt, uint32_t dir_ino,
                                 uint32_t parent, uint32_t *old_parent_out)
{
    if (!dt || !parent_link_valid(dir_ino) || !inode_number_valid(parent))
        return BFS_ERR_INVAL;
    uint32_t old_parent;
    bfs_err_t err = bfs_dir_parent_get(dt, dir_ino, &old_parent);
    if (err != BFS_OK) return err;
    uint8_t key[KEY_SIZE], value[VAL_SIZE];
    make_key(key, dir_ino, BFS_DIR_KIND_PARENT, 0, 0, 0);
    memset(value, 0, sizeof(value));
    bfs_store_be32(value + BFS_DIR_PARENT_INODE, parent);
    err = bfs_btree_update(&dt->tree, key, value);
    if (err == BFS_OK && old_parent_out) *old_parent_out = old_parent;
    return err;
}

bfs_err_t bfs_dir_parent_remove(bfs_dir_tree_t *dt, uint32_t dir_ino)
{
    if (!dt || !parent_link_valid(dir_ino)) return BFS_ERR_INVAL;
    uint8_t key[KEY_SIZE];
    make_key(key, dir_ino, BFS_DIR_KIND_PARENT, 0, 0, 0);
    return bfs_btree_delete(&dt->tree, key);
}

/* ── Comments ──────────────────────────────────────────────── */

static void comment_records(dir_records_t *r, uint32_t ino, const char *text, uint8_t len)
{
    memset(r, 0, sizeof(*r));
    r->owner = ino;
    r->kind = BFS_DIR_KIND_COMMENT;
    r->head[0] = len;
    uint8_t first = len < BFS_DIR_COMMENT_INLINE ? len : BFS_DIR_COMMENT_INLINE;
    memcpy(r->head + 1, text, first); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (len > BFS_DIR_COMMENT_INLINE) {
        r->part_count = 1;
        memcpy(r->parts[0], text + first, len - first); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    }
}

static bfs_err_t load_comment(bfs_dir_tree_t *dt, uint32_t ino, dir_records_t *r)
{
    memset(r, 0, sizeof(*r));
    r->owner = ino;
    r->kind = BFS_DIR_KIND_COMMENT;
    uint8_t key[KEY_SIZE];
    records_key(r, 0, key);
    bfs_err_t err = bfs_btree_search(&dt->tree, key, r->head);
    if (err != BFS_OK) return err;
    if (r->head[0] > BFS_DIR_COMMENT_INLINE) {
        r->part_count = 1;
        records_key(r, 1, key);
        err = bfs_btree_search(&dt->tree, key, r->parts[0]);
        if (err != BFS_OK) return err == BFS_ERR_NOTFOUND ? BFS_ERR_CORRUPT : err;
        uint8_t rest = (uint8_t)(r->head[0] - BFS_DIR_COMMENT_INLINE);
        if (!all_zero(r->parts[0] + rest, VAL_SIZE - rest)) return BFS_ERR_CORRUPT;
    }
    return BFS_OK;
}

bfs_err_t bfs_dir_comment_get(bfs_dir_tree_t *dt, uint32_t ino,
                              char text[BFS_DIR_COMMENT_MAX], uint8_t *len_out)
{
    if (!dt || !text || !len_out || !inode_number_valid(ino)) return BFS_ERR_INVAL;
    dir_records_t r;
    bfs_err_t err = load_comment(dt, ino, &r);
    if (err != BFS_OK) return err;
    uint8_t len = r.head[0];
    uint8_t first = len < BFS_DIR_COMMENT_INLINE ? len : BFS_DIR_COMMENT_INLINE;
    memcpy(text, r.head + 1, first); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (len > first) memcpy(text + first, r.parts[0], len - first); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    *len_out = len;
    return BFS_OK;
}

bfs_err_t bfs_dir_comment_insert(bfs_dir_tree_t *dt, uint32_t ino,
                                 const char *text, uint8_t len)
{
    if (!dt || !text || len == 0 || len > BFS_DIR_COMMENT_MAX || !inode_number_valid(ino))
        return BFS_ERR_INVAL;
    dir_records_t r;
    comment_records(&r, ino, text, len);
    return insert_records(dt, &r);
}

bfs_err_t bfs_dir_comment_remove(bfs_dir_tree_t *dt, uint32_t ino)
{
    if (!dt || !inode_number_valid(ino)) return BFS_ERR_INVAL;
    dir_records_t r;
    bfs_err_t err = load_comment(dt, ino, &r);
    if (err != BFS_OK) return err;
    return delete_records(dt, &r);
}

/* ── Whole-tree walk (checkers) ────────────────────────────── */

typedef struct {
    const bfs_dir_walk_ops_t *ops;
    void *ctx;
    bfs_err_t err;
    /* An entry or comment whose parts are still due. */
    bool partial;
    uint8_t kind;
    uint32_t owner;
    bfs_dir_pos_t pos;
    uint8_t parts_expected;
    uint8_t parts_seen;
    uint8_t len;
    uint32_t ino;
    uint8_t type;
    char text[BFS_NAME_MAX];
} dir_walk_ctx_t;

static bool walk_corrupt(dir_walk_ctx_t *walk)
{
    walk->err = BFS_ERR_CORRUPT;
    return false;
}

static bool walk_deliver(dir_walk_ctx_t *walk)
{
    walk->partial = false;
    if (walk->kind == BFS_DIR_KIND_COMMENT)
        return !walk->ops->comment ||
               walk->ops->comment(walk->owner, walk->text, walk->len, walk->ctx);
    if (bfs_dir_name_hash(walk->text, walk->len) != walk->pos.hash) return walk_corrupt(walk);
    return !walk->ops->entry ||
           walk->ops->entry(walk->owner, walk->text, walk->len, walk->ino, walk->type,
                            &walk->pos, walk->ctx);
}

/* A part continues the record before it, or it is an orphan. */
static bool walk_continuation(dir_walk_ctx_t *walk, const uint8_t *k, const uint8_t *v)
{
    if (!walk->partial || k[BFS_DIR_KEY_KIND] != walk->kind ||
        bfs_load_be32(k + BFS_DIR_KEY_OWNER) != walk->owner ||
        bfs_load_be32(k + BFS_DIR_KEY_HASH) != walk->pos.hash ||
        bfs_load_be16(k + BFS_DIR_KEY_ORDINAL) != walk->pos.ordinal ||
        k[BFS_DIR_KEY_PART] != walk->parts_seen + 1)
        return walk_corrupt(walk);
    uint32_t offset = walk->kind == BFS_DIR_KIND_COMMENT
        ? BFS_DIR_COMMENT_INLINE
        : INLINE_NAME + (uint32_t)walk->parts_seen * PART_BYTES;
    uint32_t chunk = walk->len - offset;
    if (chunk > PART_BYTES) chunk = PART_BYTES;
    if (!all_zero(v + chunk, PART_BYTES - chunk)) return walk_corrupt(walk);
    memcpy(walk->text + offset, v, chunk); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (++walk->parts_seen < walk->parts_expected) return true;
    return walk_deliver(walk);
}

static bool dir_walk_record(const void *key, const void *val, void *ctx)
{
    dir_walk_ctx_t *walk = (dir_walk_ctx_t *)ctx;
    const uint8_t *k = (const uint8_t *)key;
    const uint8_t *v = (const uint8_t *)val;

    if (k[BFS_DIR_KEY_PART] != 0) return walk_continuation(walk, k, v);
    if (walk->partial) return walk_corrupt(walk);

    uint32_t owner = bfs_load_be32(k + BFS_DIR_KEY_OWNER);
    uint8_t kind = k[BFS_DIR_KEY_KIND];
    uint32_t hash = bfs_load_be32(k + BFS_DIR_KEY_HASH);
    uint16_t ordinal = bfs_load_be16(k + BFS_DIR_KEY_ORDINAL);

    walk->kind = kind;
    walk->owner = owner;
    walk->pos.hash = hash;
    walk->pos.ordinal = ordinal;
    walk->parts_seen = 0;
    if (kind == BFS_DIR_KIND_PARENT)
        return !walk->ops->parent_link ||
               walk->ops->parent_link(owner, bfs_load_be32(v + BFS_DIR_PARENT_INODE),
                                      walk->ctx);
    if (kind == BFS_DIR_KIND_COMMENT) {
        walk->len = v[0];
        uint8_t first = walk->len < BFS_DIR_COMMENT_INLINE ? walk->len : BFS_DIR_COMMENT_INLINE;
        memcpy(walk->text, v + 1, first); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        walk->parts_expected = walk->len > BFS_DIR_COMMENT_INLINE ? 1 : 0;
    } else {
        walk->len = v[BFS_DIR_HEAD_NAME_LEN];
        walk->ino = bfs_load_be32(v + BFS_DIR_HEAD_INODE);
        walk->type = v[BFS_DIR_HEAD_TYPE];
        memcpy(walk->text, v + BFS_DIR_HEAD_NAME, /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
               walk->len < INLINE_NAME ? walk->len : INLINE_NAME);
        walk->parts_expected = name_parts(walk->len);
    }
    walk->partial = true;
    if (walk->parts_expected == 0) return walk_deliver(walk);
    return true;
}

bfs_err_t bfs_dir_walk(bfs_dir_tree_t *dt, const bfs_dir_walk_ops_t *ops, void *ctx)
{
    if (!dt || !ops) return BFS_ERR_INVAL;
    dir_walk_ctx_t walk = { .ops = ops, .ctx = ctx, .err = BFS_OK };
    bfs_err_t err = bfs_btree_scan(&dt->tree, NULL, dir_walk_record, &walk);
    if (walk.err != BFS_OK) return walk.err;
    if (err != BFS_OK) return err;
    return walk.partial ? BFS_ERR_CORRUPT : BFS_OK;
}
