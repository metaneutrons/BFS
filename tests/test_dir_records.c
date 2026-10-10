/*
 * BFS — Directory records: hash collisions and ordinals, long names in
 * continuation parts, comments, parent links, positional resume, and the
 * all-or-nothing contract of multi-record changes.
 */

#include "test_harness.h"
#include "bfs_dir.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_IMG "test_dir_records.img"
#define DATA_START 2
#define DIR_INO 5
#define RECORD_BYTES (BFS_DIR_KEY_SIZE + BFS_DIR_VAL_SIZE)

/* Names of equal folded FNV-1a hash, found by exhaustive search. */
static const char *const collision_triple[3] = { "k00816385", "k001ab539", "k0005d06f" };
#define COLLISION_TRIPLE_HASH UINT32_C(0x320D1E65)
static const char *const collision_pair[2] = { "k004bcb53", "k00af86bd" };
#define COLLISION_PAIR_HASH UINT32_C(0x00003495)

typedef struct {
    bfs_allocator_t base;
    bfs_allocator_t *inner;
    uint32_t calls;
    uint32_t fail_at;   /* UINT32_MAX: never */
    bool fail_after;    /* fail every call from fail_at on, not just that one */
    uint32_t dealloc_calls;
    uint32_t dealloc_fail_at; /* UINT32_MAX: never; fails that call once */
} failing_alloc_t;

typedef struct {
    bfs_bio_t *bio;
    bfs_freespace_t space;
    bfs_dir_tree_t dt;
    failing_alloc_t alloc;
} fixture_t;

static bfs_blk_t failing_alloc(bfs_allocator_t *a)
{
    failing_alloc_t *f = (failing_alloc_t *)a->ctx;
    uint32_t call = f->calls++;
    if (call == f->fail_at || (f->fail_after && call >= f->fail_at)) return BFS_BLK_NULL;
    return f->inner->alloc(f->inner);
}

static bfs_err_t failing_dealloc(bfs_allocator_t *a, bfs_blk_t blk)
{
    failing_alloc_t *f = (failing_alloc_t *)a->ctx;
    bfs_err_t err = f->inner->dealloc(f->inner, blk);
    return f->dealloc_calls++ == f->dealloc_fail_at ? BFS_ERR_IO : err;
}

static bool fixture_open(fixture_t *f, uint32_t block_size, uint32_t block_count)
{
    unlink(TEST_IMG);
    memset(f, 0, sizeof(*f));
    f->bio = bio_emu_create(TEST_IMG, block_size, block_count);
    if (!f->bio) return false;
    bfs_freespace_init(&f->space, f->bio, BFS_BLK_NULL, 1);
    if (bfs_freespace_add(&f->space, DATA_START, block_count - DATA_START) != BFS_OK)
        return false;
    bfs_freespace_refill_reserve(&f->space);
    f->alloc.inner = bfs_freespace_allocator(&f->space);
    f->alloc.base.alloc = failing_alloc;
    f->alloc.base.dealloc = failing_dealloc;
    f->alloc.base.ctx = &f->alloc;
    f->alloc.fail_at = UINT32_MAX;
    f->alloc.dealloc_fail_at = UINT32_MAX;
    return bfs_dir_init(&f->dt, f->bio, &f->alloc.base, BFS_BLK_NULL, 1) == BFS_OK;
}

static void fixture_close(fixture_t *f)
{
    bfs_bio_close(f->bio);
    unlink(TEST_IMG);
}

static void arm_failure(fixture_t *f, uint32_t at, bool after)
{
    f->alloc.calls = 0;
    f->alloc.fail_at = at;
    f->alloc.fail_after = after;
    f->alloc.dealloc_calls = 0;
    f->alloc.dealloc_fail_at = UINT32_MAX;
}

/* Fail the k-th release of a retired node. The tree call then applies its
 * change and still reports the error. */
static void arm_dealloc_failure(fixture_t *f, uint32_t at)
{
    arm_failure(f, UINT32_MAX, false);
    f->alloc.dealloc_fail_at = at;
}

/* ── Raw record snapshots ──────────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    bool failed;
} snapshot_t;

static bool snapshot_cb(const void *key, const void *value, void *ctx)
{
    snapshot_t *s = (snapshot_t *)ctx;
    if (s->len + RECORD_BYTES > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64u * RECORD_BYTES;
        uint8_t *data = realloc(s->data, cap);
        if (!data) {
            s->failed = true;
            return false;
        }
        s->data = data;
        s->cap = cap;
    }
    memcpy(s->data + s->len, key, BFS_DIR_KEY_SIZE);
    memcpy(s->data + s->len + BFS_DIR_KEY_SIZE, value, BFS_DIR_VAL_SIZE);
    s->len += RECORD_BYTES;
    return true;
}

static bool take_snapshot(bfs_dir_tree_t *dt, snapshot_t *s)
{
    s->len = 0;
    s->failed = false;
    if (dt->tree.root == BFS_BLK_NULL) return true;
    return bfs_btree_scan(&dt->tree, NULL, snapshot_cb, s) == BFS_OK && !s->failed;
}

static bool snapshots_equal(const snapshot_t *a, const snapshot_t *b)
{
    return a->len == b->len && memcmp(a->data, b->data, a->len) == 0;
}

/* ── Entry collection ──────────────────────────────────────── */

typedef struct {
    char name[BFS_NAME_MAX];
    uint8_t len;
    uint32_t ino;
    bfs_dir_pos_t pos;
} listed_t;

typedef struct {
    listed_t *items;
    size_t count;
    size_t cap;
    bool stop_after_one;
} listing_t;

static bool listing_pos_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                           uint32_t entry_type, const bfs_dir_pos_t *pos, void *ctx)
{
    (void)entry_type;
    listing_t *l = (listing_t *)ctx;
    if (l->count >= l->cap) return false;
    listed_t *item = &l->items[l->count++];
    memcpy(item->name, name, name_len);
    item->len = name_len;
    item->ino = inode_nr;
    item->pos = *pos;
    return !l->stop_after_one;
}

static bool listing_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                       uint32_t entry_type, void *ctx)
{
    static const bfs_dir_pos_t none = {0, 0};
    return listing_pos_cb(name, name_len, inode_nr, entry_type, &none, ctx);
}

static bool names_match(const listed_t *a, const listed_t *b)
{
    return a->len == b->len && a->ino == b->ino && memcmp(a->name, b->name, a->len) == 0;
}

/* Fill name with len bytes: a numbered prefix, then filler. */
static void make_name(char *name, uint8_t len, unsigned number, char filler)
{
    char prefix[16];
    int prefix_len = snprintf(prefix, sizeof(prefix), "n%04u-", number);
    memset(name, filler, len);
    memcpy(name, prefix, (size_t)prefix_len < len ? (size_t)prefix_len : len);
}

/* ── Collisions and ordinals ───────────────────────────────── */

static void test_collision_ordinals(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 4096, 1024));
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_name_hash(collision_triple[i], 9), COLLISION_TRIPLE_HASH);
    for (int i = 0; i < 2; i++)
        TEST_ASSERT_EQ(bfs_dir_name_hash(collision_pair[i], 9), COLLISION_PAIR_HASH);

    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, collision_triple[i], 9,
                                      100u + (uint32_t)i, BFS_INODE_FILE), BFS_OK);
    for (int i = 0; i < 3; i++) {
        uint32_t ino, type;
        bfs_dir_pos_t pos;
        TEST_ASSERT_EQ(bfs_dir_lookup_pos(&f.dt, DIR_INO, collision_triple[i], 9,
                                          &ino, &type, &pos), BFS_OK);
        TEST_ASSERT_EQ(ino, 100u + (uint32_t)i);
        TEST_ASSERT_EQ(pos.hash, COLLISION_TRIPLE_HASH);
        TEST_ASSERT_EQ(pos.ordinal, (uint16_t)i);
    }

    /* A case alias of a colliding name is the same name. */
    TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, "K001AB539", 9, 200, BFS_INODE_FILE),
                   BFS_ERR_EXISTS);

    /* The lowest free ordinal is reused; insert_at restores a given one. */
    TEST_ASSERT_EQ(bfs_dir_remove(&f.dt, DIR_INO, collision_triple[1], 9), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_remove(&f.dt, DIR_INO, collision_triple[0], 9), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, collision_triple[1], 9, 101, BFS_INODE_FILE),
                   BFS_OK);
    uint32_t ino, type;
    bfs_dir_pos_t pos;
    TEST_ASSERT_EQ(bfs_dir_lookup_pos(&f.dt, DIR_INO, collision_triple[1], 9, &ino, &type, &pos),
                   BFS_OK);
    TEST_ASSERT_EQ(pos.ordinal, 0);
    bfs_dir_pos_t taken = { COLLISION_TRIPLE_HASH, 2 };
    TEST_ASSERT_EQ(bfs_dir_insert_at(&f.dt, DIR_INO, collision_triple[0], 9, 100,
                                     BFS_INODE_FILE, &taken), BFS_ERR_EXISTS);
    bfs_dir_pos_t wrong_hash = { COLLISION_PAIR_HASH, 1 };
    TEST_ASSERT_EQ(bfs_dir_insert_at(&f.dt, DIR_INO, collision_triple[0], 9, 100,
                                     BFS_INODE_FILE, &wrong_hash), BFS_ERR_INVAL);
    bfs_dir_pos_t original = { COLLISION_TRIPLE_HASH, 1 };
    TEST_ASSERT_EQ(bfs_dir_insert_at(&f.dt, DIR_INO, collision_triple[0], 9, 100,
                                     BFS_INODE_FILE, &original), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup_pos(&f.dt, DIR_INO, collision_triple[0], 9, &ino, &type, &pos),
                   BFS_OK);
    TEST_ASSERT_EQ(ino, 100);
    TEST_ASSERT_EQ(pos.ordinal, 1);

    /* The scan lists the group in ordinal order with each name once. */
    listed_t items[8];
    listing_t l = { items, 0, 8, false };
    TEST_ASSERT_EQ(bfs_dir_scan_cursor_pos(&f.dt, NULL, DIR_INO, NULL, listing_pos_cb, &l),
                   BFS_OK);
    TEST_ASSERT_EQ(l.count, 3);
    for (size_t i = 0; i < l.count; i++) TEST_ASSERT_EQ(l.items[i].pos.ordinal, (uint16_t)i);
    TEST_ASSERT_EQ(l.items[0].ino, 101);
    TEST_ASSERT_EQ(l.items[1].ino, 100);
    TEST_ASSERT_EQ(l.items[2].ino, 102);

    /* Removing every name of the group leaves the directory empty. */
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_remove(&f.dt, DIR_INO, collision_triple[i], 9), BFS_OK);
    bool maybe = true;
    TEST_ASSERT_EQ(bfs_dir_may_have_entries(&f.dt, DIR_INO, &maybe), BFS_OK);
    TEST_ASSERT(!maybe);
    fixture_close(&f);
}

/* A listing that removes the entry it was just given still visits every
 * other entry once, inside and outside a collision group. */
static void test_resume_after_removed_entry(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 1024, 4096));
    const unsigned plain = 60;
    for (unsigned i = 0; i < plain; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "plain%02u", i);
        TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, name, (uint8_t)len, 1000u + i,
                                      BFS_INODE_FILE), BFS_OK);
    }
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, collision_triple[i], 9,
                                      2000u + (uint32_t)i, BFS_INODE_FILE), BFS_OK);
    for (int i = 0; i < 2; i++)
        TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, collision_pair[i], 9,
                                      3000u + (uint32_t)i, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT(f.dt.tree.height > 1);

    static listed_t expected_items[128];
    listing_t expected = { expected_items, 0, 128, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, DIR_INO, listing_cb, &expected), BFS_OK);
    TEST_ASSERT_EQ(expected.count, plain + 5);

    /* One entry per call, as ExNext does, removing every other one. */
    for (int exclusive = 0; exclusive < 2; exclusive++) {
        bfs_btree_cursor_t cursor;
        bfs_btree_cursor_init(&cursor);
        static listed_t seen_items[128];
        listing_t seen = { seen_items, 0, 128, true };
        bfs_dir_pos_t last;
        bool have_last = false;
        for (;;) {
            size_t before = seen.count;
            bfs_err_t err = BFS_ERR_AGAIN;
            if (have_last && exclusive)
                err = bfs_dir_scan_resume_pos(&f.dt, &cursor, DIR_INO, listing_pos_cb, &seen);
            if (err == BFS_ERR_AGAIN)
                err = bfs_dir_scan_cursor_pos(&f.dt, &cursor, DIR_INO,
                                              have_last ? &last : NULL, listing_pos_cb, &seen);
            TEST_ASSERT_EQ(err, BFS_OK);
            if (seen.count == before) break;
            listed_t *got = &seen.items[seen.count - 1];
            last = got->pos;
            have_last = true;
            if (seen.count % 2 == 0)
                TEST_ASSERT_EQ(bfs_dir_remove(&f.dt, DIR_INO, got->name, got->len), BFS_OK);
        }
        TEST_ASSERT_EQ(seen.count, expected.count);
        for (size_t i = 0; i < seen.count && i < expected.count; i++)
            TEST_ASSERT(names_match(&seen.items[i], &expected.items[i]));
        bfs_btree_cursor_release(&cursor);
        /* Put the removed entries back for the second round. */
        for (size_t i = 1; i < seen.count; i += 2) {
            bfs_err_t err = bfs_dir_insert_at(&f.dt, DIR_INO, seen.items[i].name,
                                              seen.items[i].len, seen.items[i].ino,
                                              BFS_INODE_FILE, &seen.items[i].pos);
            TEST_ASSERT_EQ(err, BFS_OK);
        }
    }
    fixture_close(&f);
}

/* ── Long names ────────────────────────────────────────────── */

static const uint8_t boundary_lengths[] = {
    1, 32, 33, 34, 72, 73, 74, 107, 108, 113, 114, 153, 154, 193, 194, 233, 234, 254, 255,
};
#define BOUNDARY_COUNT (sizeof(boundary_lengths) / sizeof(boundary_lengths[0]))
#define COPIES 6u

static void long_names_at(uint32_t block_size, uint32_t block_count)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, block_size, block_count));
    char name[BFS_NAME_MAX];
    unsigned number = 0;
    for (size_t b = 0; b < BOUNDARY_COUNT; b++) {
        for (unsigned copy = 0; copy < COPIES; copy++, number++) {
            uint8_t len = boundary_lengths[b];
            if (len < 7 && copy > 0) continue; /* short names cannot be numbered */
            if (len < 7) {
                memset(name, 'q', len);
            } else {
                make_name(name, len, number, 'x');
            }
            TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, name, len, 10000u + number,
                                          BFS_INODE_FILE), BFS_OK);
        }
    }

    /* Every name is found under another case, and listed with its spelling. */
    static listed_t buffer[BOUNDARY_COUNT * COPIES];
    listing_t all = { buffer, 0, BOUNDARY_COUNT * COPIES, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, DIR_INO, listing_cb, &all), BFS_OK);
    unsigned expected = 0;
    for (size_t b = 0; b < BOUNDARY_COUNT; b++) expected += boundary_lengths[b] < 7 ? 1u : COPIES;
    TEST_ASSERT_EQ(all.count, expected);
    for (size_t i = 0; i < all.count; i++) {
        char upper[BFS_NAME_MAX];
        for (uint8_t j = 0; j < all.items[i].len; j++)
            upper[j] = (char)bfs_intl_toupper((uint8_t)all.items[i].name[j]);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&f.dt, DIR_INO, upper, all.items[i].len, &ino, &type),
                       BFS_OK);
        TEST_ASSERT_EQ(ino, all.items[i].ino);
    }

    /* One entry per call gives the same sequence, also where an entry's parts
     * continue in the next leaf. */
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    static listed_t one_buffer[BOUNDARY_COUNT * COPIES];
    listing_t one = { one_buffer, 0, BOUNDARY_COUNT * COPIES, true };
    TEST_ASSERT_EQ(bfs_dir_scan_cursor_pos(&f.dt, &cursor, DIR_INO, NULL, listing_pos_cb, &one),
                   BFS_OK);
    while (one.count < all.count) {
        size_t before = one.count;
        bfs_err_t err = bfs_dir_scan_resume_pos(&f.dt, &cursor, DIR_INO, listing_pos_cb, &one);
        if (err == BFS_ERR_AGAIN)
            err = bfs_dir_scan_cursor_pos(&f.dt, &cursor, DIR_INO, &one.items[before - 1].pos,
                                          listing_pos_cb, &one);
        TEST_ASSERT_EQ(err, BFS_OK);
        if (one.count == before) break;
    }
    TEST_ASSERT_EQ(one.count, all.count);
    for (size_t i = 0; i < one.count && i < all.count; i++)
        TEST_ASSERT(names_match(&one.items[i], &all.items[i]));
    bfs_btree_cursor_release(&cursor);

    /* A case-only rename keeps the records; a different name length is no
     * case-only rename. */
    listed_t *longest = NULL;
    for (size_t i = 0; i < all.count; i++)
        if (all.items[i].len == BFS_NAME_MAX) longest = &all.items[i];
    TEST_ASSERT(longest != NULL);
    if (longest) {
        char lower[BFS_NAME_MAX];
        for (uint8_t j = 0; j < longest->len; j++)
            lower[j] = (char)(longest->name[j] >= 'A' && longest->name[j] <= 'Z'
                              ? longest->name[j] + 32 : longest->name[j]);
        lower[longest->len - 1] = 'X';
        TEST_ASSERT_EQ(bfs_dir_rekey_case(&f.dt, DIR_INO, longest->name, longest->len,
                                          lower, longest->len), BFS_OK);
        static listed_t check_buffer[BOUNDARY_COUNT * COPIES];
        listing_t check = { check_buffer, 0, BOUNDARY_COUNT * COPIES, false };
        TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, DIR_INO, listing_cb, &check), BFS_OK);
        bool found = false;
        for (size_t i = 0; i < check.count; i++)
            if (check.items[i].len == longest->len &&
                memcmp(check.items[i].name, lower, longest->len) == 0) found = true;
        TEST_ASSERT(found);
        TEST_ASSERT_EQ(bfs_dir_rekey_case(&f.dt, DIR_INO, lower, longest->len,
                                          lower, (uint8_t)(longest->len - 1)), BFS_ERR_INVAL);
    }

    /* Removing everything leaves no record behind. */
    for (size_t i = 0; i < all.count; i++)
        TEST_ASSERT_EQ(bfs_dir_remove(&f.dt, DIR_INO, all.items[i].name, all.items[i].len),
                       BFS_OK);
    snapshot_t s = {0};
    TEST_ASSERT(take_snapshot(&f.dt, &s));
    TEST_ASSERT_EQ(s.len, 0);
    free(s.data);
    fixture_close(&f);
}

static void test_long_names_1k(void) { long_names_at(1024, 8192); }
static void test_long_names_4k(void) { long_names_at(4096, 2048); }
static void test_long_names_64k(void) { long_names_at(65536, 256); }

/* ── Comments and parent links ─────────────────────────────── */

static void comments_at(uint32_t block_size, uint32_t block_count)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, block_size, block_count));
    static const uint8_t lengths[] = { 1, 38, 39, 40, 41, 78, 79 };
    char text[BFS_DIR_COMMENT_MAX];
    for (size_t i = 0; i < sizeof(lengths); i++) {
        for (uint8_t j = 0; j < lengths[i]; j++) text[j] = (char)('a' + (j + i) % 26);
        TEST_ASSERT_EQ(bfs_dir_comment_insert(&f.dt, 50u + (uint32_t)i, text, lengths[i]),
                       BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_dir_comment_insert(&f.dt, 50, "again", 5), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(bfs_dir_comment_insert(&f.dt, 70, text, 0), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_dir_comment_insert(&f.dt, 70, text, 80), BFS_ERR_INVAL);
    for (size_t i = 0; i < sizeof(lengths); i++) {
        char got[BFS_DIR_COMMENT_MAX];
        uint8_t len = 0;
        TEST_ASSERT_EQ(bfs_dir_comment_get(&f.dt, 50u + (uint32_t)i, got, &len), BFS_OK);
        TEST_ASSERT_EQ(len, lengths[i]);
        for (uint8_t j = 0; j < len; j++)
            TEST_ASSERT_EQ(got[j], (char)('a' + (j + i) % 26));
    }
    /* Comments are not entries. */
    bool maybe = true;
    TEST_ASSERT_EQ(bfs_dir_may_have_entries(&f.dt, 50, &maybe), BFS_OK);
    TEST_ASSERT(!maybe);
    for (size_t i = 0; i < sizeof(lengths); i++)
        TEST_ASSERT_EQ(bfs_dir_comment_remove(&f.dt, 50u + (uint32_t)i), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_comment_remove(&f.dt, 50), BFS_ERR_NOTFOUND);
    char got[BFS_DIR_COMMENT_MAX];
    uint8_t len;
    TEST_ASSERT_EQ(bfs_dir_comment_get(&f.dt, 50, got, &len), BFS_ERR_NOTFOUND);
    snapshot_t s = {0};
    TEST_ASSERT(take_snapshot(&f.dt, &s));
    TEST_ASSERT_EQ(s.len, 0);
    free(s.data);
    fixture_close(&f);
}

static void test_comments_1k(void) { comments_at(1024, 1024); }
static void test_comments_64k(void) { comments_at(65536, 256); }

static void test_parent_links(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 4096, 1024));
    TEST_ASSERT_EQ(bfs_dir_parent_insert(&f.dt, BFS_ROOT_INO, 7), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_dir_parent_insert(&f.dt, 7, 0), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_dir_parent_insert(&f.dt, 7, BFS_ROOT_INO), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_parent_insert(&f.dt, 7, 9), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, 7, "child", 5, 8, BFS_INODE_DIR), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_parent_insert(&f.dt, 8, 7), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_comment_insert(&f.dt, 7, "dir", 3), BFS_OK);

    /* The directory lists only its entry, before its link and comment. */
    listed_t items[4];
    listing_t l = { items, 0, 4, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, 7, listing_cb, &l), BFS_OK);
    TEST_ASSERT_EQ(l.count, 1);

    uint32_t parent = 0, old = 0;
    TEST_ASSERT_EQ(bfs_dir_parent_get(&f.dt, 8, &parent), BFS_OK);
    TEST_ASSERT_EQ(parent, 7);
    TEST_ASSERT_EQ(bfs_dir_parent_replace(&f.dt, 8, 9, &old), BFS_OK);
    TEST_ASSERT_EQ(old, 7);
    TEST_ASSERT_EQ(bfs_dir_parent_get(&f.dt, 8, &parent), BFS_OK);
    TEST_ASSERT_EQ(parent, 9);
    TEST_ASSERT_EQ(bfs_dir_parent_remove(&f.dt, 8), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_parent_get(&f.dt, 8, &parent), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_dir_parent_replace(&f.dt, 8, 9, NULL), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_dir_parent_get(&f.dt, BFS_ROOT_INO, &parent), BFS_ERR_INVAL);
    fixture_close(&f);
}

/* ── Walk ──────────────────────────────────────────────────── */

typedef struct {
    unsigned entries;
    unsigned links;
    unsigned comments;
} walk_count_t;

static bool walk_entry(uint32_t parent, const char *name, uint8_t name_len, uint32_t inode_nr,
                       uint32_t entry_type, const bfs_dir_pos_t *pos, void *ctx)
{
    (void)parent; (void)name; (void)name_len; (void)inode_nr; (void)entry_type; (void)pos;
    ((walk_count_t *)ctx)->entries++;
    return true;
}

static bool walk_link(uint32_t dir_ino, uint32_t parent, void *ctx)
{
    (void)dir_ino; (void)parent;
    ((walk_count_t *)ctx)->links++;
    return true;
}

static bool walk_comment(uint32_t ino, const char *text, uint8_t len, void *ctx)
{
    (void)ino; (void)text; (void)len;
    ((walk_count_t *)ctx)->comments++;
    return true;
}

static const bfs_dir_walk_ops_t walk_ops = { walk_entry, walk_link, walk_comment };

static void make_key(uint8_t key[BFS_DIR_KEY_SIZE], uint32_t owner, uint8_t kind,
                     uint32_t hash, uint16_t ordinal, uint8_t part)
{
    memset(key, 0, BFS_DIR_KEY_SIZE);
    bfs_store_be32(key + BFS_DIR_KEY_OWNER, owner);
    key[BFS_DIR_KEY_KIND] = kind;
    bfs_store_be32(key + BFS_DIR_KEY_HASH, hash);
    key[BFS_DIR_KEY_ORDINAL] = (uint8_t)(ordinal >> 8);
    key[BFS_DIR_KEY_ORDINAL + 1] = (uint8_t)ordinal;
    key[BFS_DIR_KEY_PART] = part;
}

static void test_walk_reports_and_rejects(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 1024, 2048));
    char name[BFS_NAME_MAX];
    memset(name, 'w', sizeof(name));
    TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, name, BFS_NAME_MAX, 40, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&f.dt, DIR_INO, "sub", 3, 41, BFS_INODE_DIR), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_parent_insert(&f.dt, 41, DIR_INO), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_comment_insert(&f.dt, 40, name, 79), BFS_OK);
    walk_count_t count = {0};
    TEST_ASSERT_EQ(bfs_dir_walk(&f.dt, &walk_ops, &count), BFS_OK);
    TEST_ASSERT_EQ(count.entries, 2);
    TEST_ASSERT_EQ(count.links, 1);
    TEST_ASSERT_EQ(count.comments, 1);

    /* A continuation part whose head is missing. */
    uint8_t key[BFS_DIR_KEY_SIZE], value[BFS_DIR_VAL_SIZE];
    memset(value, 0, sizeof(value));
    value[0] = 'z';
    make_key(key, DIR_INO, BFS_DIR_KIND_ENTRY, 0x12345678u, 0, 1);
    TEST_ASSERT_EQ(bfs_btree_insert(&f.dt.tree, key, value), BFS_OK);
    memset(&count, 0, sizeof(count));
    TEST_ASSERT_EQ(bfs_dir_walk(&f.dt, &walk_ops, &count), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(bfs_btree_delete(&f.dt.tree, key), BFS_OK);

    /* A name whose last part is missing. */
    uint32_t hash = bfs_dir_name_hash(name, BFS_NAME_MAX);
    make_key(key, DIR_INO, BFS_DIR_KIND_ENTRY, hash, 0, 6);
    TEST_ASSERT_EQ(bfs_btree_delete(&f.dt.tree, key), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_walk(&f.dt, &walk_ops, &count), BFS_ERR_CORRUPT);
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&f.dt, DIR_INO, name, BFS_NAME_MAX, &ino, &type),
                   BFS_ERR_CORRUPT);
    listed_t items[4];
    listing_t l = { items, 0, 4, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, DIR_INO, listing_cb, &l), BFS_ERR_CORRUPT);
    fixture_close(&f);
}

/* ── Failure atomicity ─────────────────────────────────────── */

typedef enum {
    OP_INSERT,
    OP_REMOVE,
    OP_REKEY,
    OP_COMMENT_INSERT,
    OP_COMMENT_REMOVE,
} record_op_t;

static const char *op_label(record_op_t op)
{
    switch (op) {
    case OP_INSERT: return "insert";
    case OP_REMOVE: return "remove";
    case OP_REKEY: return "rekey";
    case OP_COMMENT_INSERT: return "comment insert";
    default: return "comment remove";
    }
}

/* A directory of fill short entries, plus the long entry and its comment
 * when the operation removes or changes them. */
static bool build_case(fixture_t *f, uint32_t fill, record_op_t op, char *long_name)
{
    if (!fixture_open(f, 1024, 4096)) return false;
    for (uint32_t i = 0; i < fill; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "s%04u", (unsigned)i);
        if (bfs_dir_insert(&f->dt, DIR_INO, name, (uint8_t)len, 500u + i, BFS_INODE_FILE) != BFS_OK)
            return false;
        if (i % 3 == 0 && bfs_dir_comment_insert(&f->dt, 500u + i, "c", 1) != BFS_OK)
            return false;
    }
    memset(long_name, 'L', BFS_NAME_MAX);
    if (op == OP_REMOVE || op == OP_REKEY)
        return bfs_dir_insert(&f->dt, DIR_INO, long_name, BFS_NAME_MAX, 77, BFS_INODE_FILE) == BFS_OK;
    if (op == OP_COMMENT_REMOVE)
        return bfs_dir_comment_insert(&f->dt, 77, long_name, 79) == BFS_OK;
    return true;
}

static bfs_err_t run_op(fixture_t *f, record_op_t op, const char *long_name)
{
    char lower[BFS_NAME_MAX];
    switch (op) {
    case OP_INSERT:
        return bfs_dir_insert(&f->dt, DIR_INO, long_name, BFS_NAME_MAX, 77, BFS_INODE_FILE);
    case OP_REMOVE:
        return bfs_dir_remove(&f->dt, DIR_INO, long_name, BFS_NAME_MAX);
    case OP_REKEY:
        memset(lower, 'l', sizeof(lower));
        return bfs_dir_rekey_case(&f->dt, DIR_INO, long_name, BFS_NAME_MAX, lower, BFS_NAME_MAX);
    case OP_COMMENT_INSERT:
        return bfs_dir_comment_insert(&f->dt, 77, long_name, 79);
    default:
        return bfs_dir_comment_remove(&f->dt, 77);
    }
}

/* Fail the k-th allocation of the operation, once or from then on, for every
 * k it makes when it succeeds. A failed operation leaves every record as it
 * was, or, only when undoing also fails, sets sticky_err. */
static void check_failure_atomicity(record_op_t op)
{
    static const uint32_t fills[] = { 10, 25, 40, 55 };
    bool later_record_failed = false;
    for (size_t fi = 0; fi < sizeof(fills) / sizeof(fills[0]); fi++) {
        fixture_t f;
        char long_name[BFS_NAME_MAX];
        TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
        arm_failure(&f, UINT32_MAX, false);
        TEST_ASSERT_EQ(run_op(&f, op, long_name), BFS_OK);
        uint32_t calls = f.alloc.calls;
        uint32_t height = f.dt.tree.height;
        fixture_close(&f);
        TEST_ASSERT(calls > 0);
        if (calls > height) later_record_failed = true;

        for (uint32_t k = 0; k < calls; k++) {
            for (int after = 0; after < 2; after++) {
                TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
                snapshot_t before = {0}, now = {0};
                TEST_ASSERT(take_snapshot(&f.dt, &before));
                arm_failure(&f, k, after != 0);
                bfs_err_t err = run_op(&f, op, long_name);
                arm_failure(&f, UINT32_MAX, false);
                if (err == BFS_OK) {
                    fprintf(stderr, "    %s fill %u: allocation %u failure ignored\n",
                            op_label(op), (unsigned)fills[fi], (unsigned)k);
                    TEST_ASSERT(err != BFS_OK);
                } else if (f.dt.sticky_err == BFS_OK) {
                    TEST_ASSERT(take_snapshot(&f.dt, &now));
                    if (!snapshots_equal(&before, &now))
                        fprintf(stderr, "    %s fill %u: allocation %u%s changed records\n",
                                op_label(op), (unsigned)fills[fi], (unsigned)k,
                                after ? "+" : "");
                    TEST_ASSERT(snapshots_equal(&before, &now));
                } else {
                    /* Only a failing undo latches, and only if failures persist. */
                    TEST_ASSERT(after != 0);
                }
                free(before.data);
                free(now.data);
                fixture_close(&f);
            }
        }
    }
    /* Some fill made a record after the first allocate, so failures hit the
     * middle of an entry and its undo ran. */
    TEST_ASSERT(later_record_failed);
}

/* A failed release of a retired node leaves the change applied but reports
 * an error. A failed operation must then either leave every record as it was
 * or set sticky_err; it must never report failure with a changed tree and no
 * latched error, because the change could be committed. */
static void check_release_failure(record_op_t op)
{
    static const uint32_t fills[] = { 10, 40 };
    uint32_t injected = 0;
    for (size_t fi = 0; fi < sizeof(fills) / sizeof(fills[0]); fi++) {
        fixture_t f;
        char long_name[BFS_NAME_MAX];
        TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
        arm_failure(&f, UINT32_MAX, false);
        TEST_ASSERT_EQ(run_op(&f, op, long_name), BFS_OK);
        uint32_t releases = f.alloc.dealloc_calls;
        fixture_close(&f);
        for (uint32_t k = 0; k < releases; k++) {
            TEST_ASSERT(build_case(&f, fills[fi], op, long_name));
            snapshot_t before = {0}, now = {0};
            TEST_ASSERT(take_snapshot(&f.dt, &before));
            arm_dealloc_failure(&f, k);
            bfs_err_t err = run_op(&f, op, long_name);
            arm_failure(&f, UINT32_MAX, false);
            TEST_ASSERT(err != BFS_OK);
            injected++;
            TEST_ASSERT(take_snapshot(&f.dt, &now));
            bool unchanged = snapshots_equal(&before, &now);
            if (!unchanged && f.dt.sticky_err == BFS_OK)
                fprintf(stderr, "    %s fill %u: release %u failure left a silent change\n",
                        op_label(op), (unsigned)fills[fi], (unsigned)k);
            TEST_ASSERT(unchanged || f.dt.sticky_err != BFS_OK);
            free(before.data);
            free(now.data);
            fixture_close(&f);
        }
    }
    TEST_ASSERT(injected > 0);
}

static void test_failure_insert(void) { check_failure_atomicity(OP_INSERT); }
static void test_failure_remove(void) { check_failure_atomicity(OP_REMOVE); }
static void test_failure_rekey(void) { check_failure_atomicity(OP_REKEY); }
static void test_failure_comment_insert(void) { check_failure_atomicity(OP_COMMENT_INSERT); }
static void test_failure_comment_remove(void) { check_failure_atomicity(OP_COMMENT_REMOVE); }
static void test_release_failure_insert(void) { check_release_failure(OP_INSERT); }
static void test_release_failure_remove(void) { check_release_failure(OP_REMOVE); }
static void test_release_failure_rekey(void) { check_release_failure(OP_REKEY); }
static void test_release_failure_comments(void)
{
    check_release_failure(OP_COMMENT_INSERT);
    check_release_failure(OP_COMMENT_REMOVE);
}

TEST_SUITE_BEGIN("Directory records")
    TEST_RUN(test_collision_ordinals);
    TEST_RUN(test_resume_after_removed_entry);
    TEST_RUN(test_long_names_1k);
    TEST_RUN(test_long_names_4k);
    TEST_RUN(test_long_names_64k);
    TEST_RUN(test_comments_1k);
    TEST_RUN(test_comments_64k);
    TEST_RUN(test_parent_links);
    TEST_RUN(test_walk_reports_and_rejects);
    TEST_RUN(test_failure_insert);
    TEST_RUN(test_failure_remove);
    TEST_RUN(test_failure_rekey);
    TEST_RUN(test_failure_comment_insert);
    TEST_RUN(test_failure_comment_remove);
    TEST_RUN(test_release_failure_insert);
    TEST_RUN(test_release_failure_remove);
    TEST_RUN(test_release_failure_rekey);
    TEST_RUN(test_release_failure_comments);
TEST_SUITE_END()
