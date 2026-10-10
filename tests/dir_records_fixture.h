/*
 * BFS — Fixture of the directory record tests: a directory tree on an
 * emulated device whose allocator fails on demand, raw record snapshots and
 * entry collection. The including test defines TEST_IMG.
 */

#ifndef DIR_RECORDS_FIXTURE_H
#define DIR_RECORDS_FIXTURE_H

#include "bfs_dir.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TEST_IMG
#error "define TEST_IMG before including dir_records_fixture.h"
#endif

#define DATA_START 2
#define DIR_INO 5
#define RECORD_BYTES (BFS_DIR_KEY_SIZE + BFS_DIR_VAL_SIZE)

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

static inline bfs_blk_t failing_alloc(bfs_allocator_t *a)
{
    failing_alloc_t *f = (failing_alloc_t *)a->ctx;
    uint32_t call = f->calls++;
    if (call == f->fail_at || (f->fail_after && call >= f->fail_at)) return BFS_BLK_NULL;
    return f->inner->alloc(f->inner);
}

static inline bfs_err_t failing_dealloc(bfs_allocator_t *a, bfs_blk_t blk)
{
    failing_alloc_t *f = (failing_alloc_t *)a->ctx;
    bfs_err_t err = f->inner->dealloc(f->inner, blk);
    return f->dealloc_calls++ == f->dealloc_fail_at ? BFS_ERR_IO : err;
}

static inline bool fixture_open(fixture_t *f, uint32_t block_size, uint32_t block_count)
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

static inline void fixture_close(fixture_t *f)
{
    bfs_bio_close(f->bio);
    unlink(TEST_IMG);
}

static inline void arm_failure(fixture_t *f, uint32_t at, bool after)
{
    f->alloc.calls = 0;
    f->alloc.fail_at = at;
    f->alloc.fail_after = after;
    f->alloc.dealloc_calls = 0;
    f->alloc.dealloc_fail_at = UINT32_MAX;
}

/* Fail the k-th release of a retired node. The tree call then applies its
 * change and still reports the error. */
static inline void arm_dealloc_failure(fixture_t *f, uint32_t at)
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

static inline bool snapshot_cb(const void *key, const void *value, void *ctx)
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
    memcpy(s->data + s->len, key, BFS_DIR_KEY_SIZE); /* Flawfinder: ignore */
    memcpy(s->data + s->len + BFS_DIR_KEY_SIZE, value, BFS_DIR_VAL_SIZE); /* Flawfinder: ignore */
    s->len += RECORD_BYTES;
    return true;
}

static inline bool take_snapshot(bfs_dir_tree_t *dt, snapshot_t *s)
{
    s->len = 0;
    s->failed = false;
    if (dt->tree.root == BFS_BLK_NULL) return true;
    return bfs_btree_scan(&dt->tree, NULL, snapshot_cb, s) == BFS_OK && !s->failed;
}

static inline bool snapshots_equal(const snapshot_t *a, const snapshot_t *b)
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

static inline bool listing_pos_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                                  uint32_t entry_type, const bfs_dir_pos_t *pos, void *ctx)
{
    (void)entry_type;
    listing_t *l = (listing_t *)ctx;
    if (l->count >= l->cap) return false;
    listed_t *item = &l->items[l->count++];
    memcpy(item->name, name, name_len); /* Flawfinder: ignore */
    item->len = name_len;
    item->ino = inode_nr;
    item->pos = *pos;
    return !l->stop_after_one;
}

static inline bool listing_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                              uint32_t entry_type, void *ctx)
{
    static const bfs_dir_pos_t none = {0, 0};
    return listing_pos_cb(name, name_len, inode_nr, entry_type, &none, ctx);
}

static inline bool names_match(const listed_t *a, const listed_t *b)
{
    return a->len == b->len && a->ino == b->ino && memcmp(a->name, b->name, a->len) == 0;
}

#endif /* DIR_RECORDS_FIXTURE_H */
