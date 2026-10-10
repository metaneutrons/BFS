/*
 * BFS — Directory records: hash collisions and ordinals, long names in
 * continuation parts, comments, parent links and positional resume. The
 * all-or-nothing contract of multi-record changes is tested in
 * test_dir_records_atomicity.c.
 */

#define TEST_IMG "test_dir_records.img"
#include "test_harness.h"
#include "dir_records_fixture.h"

/* Names of equal folded FNV-1a hash, found by exhaustive search. */
static const char *const collision_triple[3] = { "k00816385", "k001ab539", "k0005d06f" };
#define COLLISION_TRIPLE_HASH UINT32_C(0x320D1E65)
static const char *const collision_pair[2] = { "k004bcb53", "k00af86bd" };
#define COLLISION_PAIR_HASH UINT32_C(0x00003495)

/* Fill name with len bytes: a numbered prefix, then filler. */
static void make_name(char *name, uint8_t len, unsigned number, char filler)
{
    char prefix[16];
    int prefix_len = snprintf(prefix, sizeof(prefix), "n%04u-", number);
    memset(name, filler, len);
    memcpy(name, prefix, (size_t)prefix_len < len ? (size_t)prefix_len : len); /* Flawfinder: ignore */
}

/* ── Collisions and ordinals ───────────────────────────────── */

static void insert_collision_triple(fixture_t *f, uint32_t first_ino)
{
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_insert(&f->dt, DIR_INO, collision_triple[i], 9,
                                      first_ino + (uint32_t)i, BFS_INODE_FILE), BFS_OK);
}

static void test_collision_ordinals(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 4096, 1024));
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_name_hash(collision_triple[i], 9), COLLISION_TRIPLE_HASH);
    for (int i = 0; i < 2; i++)
        TEST_ASSERT_EQ(bfs_dir_name_hash(collision_pair[i], 9), COLLISION_PAIR_HASH);

    int failures_before = test_fail_count;
    insert_collision_triple(&f, 100);
    if (test_fail_count != failures_before) return;
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
    fixture_close(&f);
}

/* The scan lists the group in ordinal order with each name once; removing
 * every name of the group leaves the directory empty. */
static void check_group_listing(fixture_t *f)
{
    listed_t items[8];
    listing_t l = { items, 0, 8, false };
    TEST_ASSERT_EQ(bfs_dir_scan_cursor_pos(&f->dt, NULL, DIR_INO, NULL, listing_pos_cb, &l),
                   BFS_OK);
    TEST_ASSERT_EQ(l.count, 3);
    for (size_t i = 0; i < l.count; i++) TEST_ASSERT_EQ(l.items[i].pos.ordinal, (uint16_t)i);
    TEST_ASSERT_EQ(l.items[0].ino, 101);
    TEST_ASSERT_EQ(l.items[1].ino, 100);
    TEST_ASSERT_EQ(l.items[2].ino, 102);

    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(bfs_dir_remove(&f->dt, DIR_INO, collision_triple[i], 9), BFS_OK);
    bool maybe = true;
    TEST_ASSERT_EQ(bfs_dir_may_have_entries(&f->dt, DIR_INO, &maybe), BFS_OK);
    TEST_ASSERT(!maybe);
}

/* The lowest free ordinal is reused; insert_at restores a given one. */
static void test_collision_ordinal_reuse(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 4096, 1024));
    int failures_before = test_fail_count;
    insert_collision_triple(&f, 100);
    if (test_fail_count != failures_before) return;

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

    failures_before = test_fail_count;
    check_group_listing(&f);
    if (test_fail_count != failures_before) return;
    fixture_close(&f);
}

/* Plain entries enough for a tree of more than one level, and both
 * collision groups. */
static void fill_resume_directory(fixture_t *f, unsigned plain)
{
    for (unsigned i = 0; i < plain; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "plain%02u", i);
        TEST_ASSERT_EQ(bfs_dir_insert(&f->dt, DIR_INO, name, (uint8_t)len, 1000u + i,
                                      BFS_INODE_FILE), BFS_OK);
    }
    insert_collision_triple(f, 2000);
    for (int i = 0; i < 2; i++)
        TEST_ASSERT_EQ(bfs_dir_insert(&f->dt, DIR_INO, collision_pair[i], 9,
                                      3000u + (uint32_t)i, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT(f->dt.tree.height > 1);
}

/* List one entry per call, as ExNext does, removing every other one, and
 * compare with the full listing; then put the removed entries back. */
static void list_removing_every_other(fixture_t *f, bool exclusive, const listing_t *expected)
{
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
            err = bfs_dir_scan_resume_pos(&f->dt, &cursor, DIR_INO, listing_pos_cb, &seen);
        if (err == BFS_ERR_AGAIN)
            err = bfs_dir_scan_cursor_pos(&f->dt, &cursor, DIR_INO,
                                          have_last ? &last : NULL, listing_pos_cb, &seen);
        TEST_ASSERT_EQ(err, BFS_OK);
        if (seen.count == before) break;
        listed_t *got = &seen.items[seen.count - 1];
        last = got->pos;
        have_last = true;
        if (seen.count % 2 == 0)
            TEST_ASSERT_EQ(bfs_dir_remove(&f->dt, DIR_INO, got->name, got->len), BFS_OK);
    }
    TEST_ASSERT_EQ(seen.count, expected->count);
    for (size_t i = 0; i < seen.count && i < expected->count; i++)
        TEST_ASSERT(names_match(&seen.items[i], &expected->items[i]));
    bfs_btree_cursor_release(&cursor);
    for (size_t i = 1; i < seen.count; i += 2) {
        bfs_err_t err = bfs_dir_insert_at(&f->dt, DIR_INO, seen.items[i].name,
                                          seen.items[i].len, seen.items[i].ino,
                                          BFS_INODE_FILE, &seen.items[i].pos);
        TEST_ASSERT_EQ(err, BFS_OK);
    }
}

/* A listing that removes the entry it was just given still visits every
 * other entry once, inside and outside a collision group. */
static void test_resume_after_removed_entry(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 1024, 4096));
    const unsigned plain = 60;
    int failures_before = test_fail_count;
    fill_resume_directory(&f, plain);
    if (test_fail_count != failures_before) return;

    static listed_t expected_items[128];
    listing_t expected = { expected_items, 0, 128, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, DIR_INO, listing_cb, &expected), BFS_OK);
    TEST_ASSERT_EQ(expected.count, plain + 5);

    for (int exclusive = 0; exclusive < 2; exclusive++) {
        list_removing_every_other(&f, exclusive != 0, &expected);
        if (test_fail_count != failures_before) return;
    }
    fixture_close(&f);
}

/* ── Long names ────────────────────────────────────────────── */

static const uint8_t boundary_lengths[] = {
    1, 32, 33, 34, 72, 73, 74, 107, 108, 113, 114, 153, 154, 193, 194, 233, 234, 254, 255,
};
#define BOUNDARY_COUNT (sizeof(boundary_lengths) / sizeof(boundary_lengths[0]))
#define COPIES 6u

static void insert_boundary_names(fixture_t *f)
{
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
            TEST_ASSERT_EQ(bfs_dir_insert(&f->dt, DIR_INO, name, len, 10000u + number,
                                          BFS_INODE_FILE), BFS_OK);
        }
    }
}

/* Every name is found under another case. */
static void check_case_lookups(fixture_t *f, const listing_t *all)
{
    for (size_t i = 0; i < all->count; i++) {
        char upper[BFS_NAME_MAX];
        for (uint8_t j = 0; j < all->items[i].len; j++)
            upper[j] = (char)bfs_intl_toupper((uint8_t)all->items[i].name[j]);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&f->dt, DIR_INO, upper, all->items[i].len, &ino, &type),
                       BFS_OK);
        TEST_ASSERT_EQ(ino, all->items[i].ino);
    }
}

/* One entry per call gives the same sequence, also where an entry's parts
 * continue in the next leaf. */
static void check_one_per_call(fixture_t *f, const listing_t *all)
{
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    static listed_t one_buffer[BOUNDARY_COUNT * COPIES];
    listing_t one = { one_buffer, 0, BOUNDARY_COUNT * COPIES, true };
    TEST_ASSERT_EQ(bfs_dir_scan_cursor_pos(&f->dt, &cursor, DIR_INO, NULL, listing_pos_cb, &one),
                   BFS_OK);
    while (one.count < all->count) {
        size_t before = one.count;
        bfs_err_t err = bfs_dir_scan_resume_pos(&f->dt, &cursor, DIR_INO, listing_pos_cb, &one);
        if (err == BFS_ERR_AGAIN)
            err = bfs_dir_scan_cursor_pos(&f->dt, &cursor, DIR_INO, &one.items[before - 1].pos,
                                          listing_pos_cb, &one);
        TEST_ASSERT_EQ(err, BFS_OK);
        if (one.count == before) break;
    }
    TEST_ASSERT_EQ(one.count, all->count);
    for (size_t i = 0; i < one.count && i < all->count; i++)
        TEST_ASSERT(names_match(&one.items[i], &all->items[i]));
    bfs_btree_cursor_release(&cursor);
}

/* A case-only rename keeps the records; a different name length is no
 * case-only rename. */
static void check_case_rekey(fixture_t *f, const listing_t *all)
{
    const listed_t *longest = NULL;
    for (size_t i = 0; i < all->count; i++)
        if (all->items[i].len == BFS_NAME_MAX) longest = &all->items[i];
    TEST_ASSERT(longest != NULL);
    char lower[BFS_NAME_MAX];
    for (uint8_t j = 0; j < longest->len; j++)
        lower[j] = (char)(longest->name[j] >= 'A' && longest->name[j] <= 'Z'
                          ? longest->name[j] + 32 : longest->name[j]);
    lower[longest->len - 1] = 'X';
    TEST_ASSERT_EQ(bfs_dir_rekey_case(&f->dt, DIR_INO, longest->name, longest->len,
                                      lower, longest->len), BFS_OK);
    static listed_t check_buffer[BOUNDARY_COUNT * COPIES];
    listing_t check = { check_buffer, 0, BOUNDARY_COUNT * COPIES, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f->dt, DIR_INO, listing_cb, &check), BFS_OK);
    bool found = false;
    for (size_t i = 0; i < check.count; i++)
        if (check.items[i].len == longest->len &&
            memcmp(check.items[i].name, lower, longest->len) == 0) found = true;
    TEST_ASSERT(found);
    TEST_ASSERT_EQ(bfs_dir_rekey_case(&f->dt, DIR_INO, lower, longest->len,
                                      lower, (uint8_t)(longest->len - 1)), BFS_ERR_INVAL);
}

static void long_names_at(uint32_t block_size, uint32_t block_count)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, block_size, block_count));
    int failures_before = test_fail_count;
    insert_boundary_names(&f);
    if (test_fail_count != failures_before) return;

    /* Every name is listed with its spelling. */
    static listed_t buffer[BOUNDARY_COUNT * COPIES];
    listing_t all = { buffer, 0, BOUNDARY_COUNT * COPIES, false };
    TEST_ASSERT_EQ(bfs_dir_scan(&f.dt, DIR_INO, listing_cb, &all), BFS_OK);
    unsigned expected = 0;
    for (size_t b = 0; b < BOUNDARY_COUNT; b++) expected += boundary_lengths[b] < 7 ? 1u : COPIES;
    TEST_ASSERT_EQ(all.count, expected);
    check_case_lookups(&f, &all);
    check_one_per_call(&f, &all);
    check_case_rekey(&f, &all);
    if (test_fail_count != failures_before) return;

    /* Removing everything leaves no record behind. The rekeyed name differs
     * from its listed spelling only in case. */
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

TEST_SUITE_BEGIN("Directory records")
    TEST_RUN(test_collision_ordinals);
    TEST_RUN(test_collision_ordinal_reuse);
    TEST_RUN(test_resume_after_removed_entry);
    TEST_RUN(test_long_names_1k);
    TEST_RUN(test_long_names_4k);
    TEST_RUN(test_long_names_64k);
    TEST_RUN(test_comments_1k);
    TEST_RUN(test_comments_64k);
    TEST_RUN(test_parent_links);
    TEST_RUN(test_walk_reports_and_rejects);
TEST_SUITE_END()
