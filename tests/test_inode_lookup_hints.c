/* SPDX-License-Identifier: MPL-2.0 */
/* Regression coverage for filesystem-owned inode B-tree lookup hints. */

#include "test_harness.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_inode.h"
#include "bfs_internal.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HINT_IMAGE "test_inode_lookup_hints.img"
#define HINT_BLOCK_SIZE 4096u
#define HINT_BLOCK_COUNT 4096u
#define HINT_CACHE_SLOTS 64u
#define HINT_DEFERRED_LIMIT 32u

/* The standalone range-capacity fixture uses the same bootstrap allocator as
 * test_btree.c. Its first field is the allocator interface. */
typedef struct {
    bfs_allocator_t base;
    bfs_blk_t next_block;
    bfs_blk_t max_block;
    bfs_blk_t freed[4096];
    uint32_t freed_count;
} hint_bootstrap_alloc_t;
extern hint_bootstrap_alloc_t *bootstrap_create(bfs_blk_t start,
                                                 bfs_blk_t max);

typedef struct {
    bfs_bio_t *device;
    bfs_cache_t cache;
    bfs_fs_t *fs;
    bool cache_ready;
    bool abandon_on_close;
} hint_fixture_t;

static const bfs_bio_ops_t *probe_base_ops;
static bfs_bio_ops_t probe_ops;
static uint32_t probe_alloc_calls;
static uint32_t probe_free_calls;

static void *probe_alloc_buffer(bfs_bio_t *bio, size_t size)
{
    probe_alloc_calls++;
    if (probe_base_ops->alloc_buffer)
        return probe_base_ops->alloc_buffer(bio, size);
    return malloc(size);
}

static void probe_free_buffer(bfs_bio_t *bio, void *buffer)
{
    probe_free_calls++;
    if (probe_base_ops->free_buffer)
        probe_base_ops->free_buffer(bio, buffer);
    else
        free(buffer);
}

static uint64_t saturated_mutation_epoch(bfs_bio_t *bio)
{
    (void)bio;
    return UINT64_MAX;
}

static void probe_install(hint_fixture_t *fixture)
{
    probe_base_ops = fixture->cache.bio.ops;
    probe_ops = *probe_base_ops;
    probe_ops.alloc_buffer = probe_alloc_buffer;
    probe_ops.free_buffer = probe_free_buffer;
    fixture->cache.bio.ops = &probe_ops;
    probe_alloc_calls = 0;
    probe_free_calls = 0;
}

static void probe_remove(hint_fixture_t *fixture)
{
    if (probe_base_ops) fixture->cache.bio.ops = probe_base_ops;
    probe_base_ops = NULL;
}

static bool fixture_open(hint_fixture_t *fixture, uint32_t options)
{
    memset(fixture, 0, sizeof(*fixture));
    (void)unlink(HINT_IMAGE);
    fixture->device = bio_emu_create(HINT_IMAGE, HINT_BLOCK_SIZE,
                                     HINT_BLOCK_COUNT);
    if (!fixture->device) return false;
    if (bfs_cache_init(&fixture->cache, fixture->device,
                       HINT_CACHE_SLOTS) != BFS_OK)
        goto fail;
    fixture->cache_ready = true;
    fixture->fs = (bfs_fs_t *)calloc(1, sizeof(*fixture->fs));
    if (!fixture->fs) goto fail;
    if (bfs_fs_format(&fixture->cache.bio, "InodeHints", options) != BFS_OK)
        goto fail;
    bfs_cache_invalidate(&fixture->cache);
    bfs_cache_set_deferred_node_limit(&fixture->cache,
                                      HINT_DEFERRED_LIMIT);
    if (bfs_fs_mount(fixture->fs, &fixture->cache.bio) != BFS_OK)
        goto fail;
    return true;

fail:
    if (fixture->fs) {
        if (fixture->fs->mounted) bfs_fs_abandon(fixture->fs);
        free(fixture->fs);
        fixture->fs = NULL;
    }
    if (fixture->cache_ready) bfs_cache_destroy(&fixture->cache);
    if (fixture->device) bfs_bio_close(fixture->device);
    fixture->device = NULL;
    (void)unlink(HINT_IMAGE);
    return false;
}

static void fixture_close(hint_fixture_t *fixture)
{
    probe_remove(fixture);
    if (fixture->fs) {
        if (fixture->fs->mounted) {
            if (fixture->abandon_on_close)
                bfs_fs_abandon(fixture->fs);
            else
                (void)bfs_fs_unmount(fixture->fs);
        }
        free(fixture->fs);
        fixture->fs = NULL;
    }
    if (fixture->cache_ready) {
        bfs_cache_destroy(&fixture->cache);
        fixture->cache_ready = false;
    }
    if (fixture->device) {
        bfs_bio_close(fixture->device);
        fixture->device = NULL;
    }
    (void)unlink(HINT_IMAGE);
}

static bfs_btree_key_hint_t *find_hint(bfs_btree_key_hint_cache_t *cache,
                                       uint32_t key)
{
    if (!cache) return NULL;
    for (uint32_t i = 0; i < BFS_BTREE_KEY_HINT_SLOTS; i++) {
        bfs_btree_key_hint_t *hint = &cache->slots[i];
        if (hint->leaf != BFS_BLK_NULL && hint->key == key) return hint;
    }
    return NULL;
}

static bfs_btree_leaf_hint_t *find_leaf_hint(bfs_btree_key_hint_cache_t *cache,
                                             uint32_t key)
{
    if (!cache) return NULL;
    for (uint32_t i = 0; i < cache->leaf_count; i++) {
        bfs_btree_leaf_hint_t *hint = &cache->leaf_slots[i];
        if (hint->leaf != BFS_BLK_NULL && hint->first_key <= key &&
            key <= hint->last_key)
            return hint;
    }
    return NULL;
}

static void disable_exact_and_last_leaf(hint_fixture_t *fixture, uint32_t key)
{
    bfs_btree_key_hint_t *hint = find_hint(&fixture->fs->inode_key_hints, key);
    if (hint) hint->leaf = BFS_BLK_NULL;
    fixture->fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    fixture->fs->inode_tree.hint_root = BFS_BLK_NULL;
    fixture->fs->inode_tree.hint_generation = 0;
    fixture->fs->inode_tree.hint_mutation_epoch = 0;
}

static bfs_node_validation_t validation_for_tree(const bfs_btree_t *tree)
{
    return (bfs_node_validation_t){
        .key_compare = tree->ops->key_compare,
        .key_size = tree->ops->key_size,
        .val_size = tree->ops->val_size,
        .block_size = tree->bio->block_size,
        .block_count = tree->bio->block_count,
    };
}

static void recalculate_node_crc(const bfs_btree_t *tree, uint8_t *node)
{
    hdr_of(node)->crc32 = 0;
    hdr_of(node)->crc32 = bfs_be32(node_compute_crc(tree, node));
}

static bool read_inode_key_location(bfs_btree_t *tree, uint32_t ino,
                                    uint8_t *leaf, uint32_t *index_out)
{
    uint32_t count = num_keys(leaf);
    for (uint32_t i = 0; i < count; i++) {
        if (bfs_load_be32(node_key(tree, leaf, i)) == ino) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static bool make_many_files(hint_fixture_t *fixture, uint32_t count,
                            uint32_t *inos)
{
    char name[16];
    for (uint32_t i = 0; i < count; i++) {
        int length = snprintf(name, sizeof(name), "hint%03" PRIu32, i);
        if (length <= 0 || (size_t)length >= sizeof(name)) return false;
        if (bfs_fs_create_file(fixture->fs, BFS_ROOT_INO, name,
                               (uint8_t)length, &inos[i]) != BFS_OK)
            return false;
    }
    return true;
}

static void test_warm_hint_skips_traversal_buffer_and_checks_index(void)
{
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    probe_install(&fixture);

    /* Mount validates the root inode, so explicitly clear those legitimate
     * warm entries before measuring the first search in this test. */
    bfs_btree_key_hint_cache_reset(&fixture.fs->inode_key_hints);
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    fixture.fs->inode_tree.hint_root = BFS_BLK_NULL;
    fixture.fs->inode_tree.hint_generation = 0;
    fixture.fs->inode_tree.hint_mutation_epoch = 0;

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    bfs_btree_key_hint_t *hint = find_hint(&fixture.fs->inode_key_hints,
                                          BFS_ROOT_INO);
    TEST_ASSERT(hint != NULL);

    /* Do not let the pre-existing last-leaf shortcut satisfy this oracle. */
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_alloc_calls = 0;
    probe_free_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT_EQ(probe_alloc_calls, 0);

    /* A cached index is only a location hint: reject a stale index and fall
     * back to the current validated leaf contents. Disable the independent
     * range route so this remains an exact-index fallback oracle. */
    fixture.fs->inode_key_hints.leaf_count = 0;
    hint->index = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.inode_nr), BFS_ROOT_INO);
    TEST_ASSERT(hint->index < 2u);
    TEST_ASSERT_EQ(probe_free_calls, 1);
    fixture_close(&fixture);
}

static void test_distinct_key_range_hit_skips_root_traversal(void)
{
    enum { FILE_COUNT = 80 };
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    probe_install(&fixture);

    uint32_t inos[FILE_COUNT];
    TEST_ASSERT(make_many_files(&fixture, FILE_COUNT, inos));
    TEST_ASSERT(fixture.fs->inode_tree.height > 1);
    bfs_btree_key_hint_cache_reset(&fixture.fs->inode_key_hints);
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[0], &inode),
                   BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_count > 0);
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, inos[1]) == NULL);
    TEST_ASSERT(find_leaf_hint(&fixture.fs->inode_key_hints, inos[1]) != NULL);

    /* Neither the exact-key cache nor the single last-leaf shortcut may
     * satisfy this distinct inode lookup. */
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[1], &inode),
                   BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.inode_nr), inos[1]);
    TEST_ASSERT_EQ(probe_alloc_calls, 0);
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, inos[1]) != NULL);

    /* A range is not an authority over current endpoints. A damaged hint
     * falls back, and the freshly traversed leaf repairs it. */
    bfs_btree_leaf_hint_t *range = find_leaf_hint(
        &fixture.fs->inode_key_hints, inos[1]);
    TEST_ASSERT(range != NULL);
    uint32_t correct_last = range->last_key;
    range->last_key++;
    disable_exact_and_last_leaf(&fixture, inos[1]);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[1], &inode),
                   BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(range->last_key, correct_last);

    bfs_btree_t different_owner = {0};
    fixture.fs->inode_key_hints.leaf_owner = &different_owner;
    /* Keep the exact slot active: identity changes must clear it as well. */
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, inos[1]) != NULL);
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[1], &inode),
                   BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_owner,
                   &fixture.fs->inode_tree);

    fixture.fs->inode_key_hints.leaf_bio = fixture.device;
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[1], &inode),
                   BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_bio, &fixture.cache.bio);

    const bfs_btree_ops_t different_layout = *fixture.fs->inode_tree.ops;
    fixture.fs->inode_key_hints.leaf_ops = &different_layout;
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[1], &inode),
                   BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_ops,
                   fixture.fs->inode_tree.ops);

    fixture_close(&fixture);
}

static void test_sparse_range_uses_checked_search_and_misses_fall_back(void)
{
    enum { FILE_COUNT = 12 };
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    probe_install(&fixture);

    uint32_t inos[FILE_COUNT];
    TEST_ASSERT(make_many_files(&fixture, FILE_COUNT, inos));
    TEST_ASSERT_EQ(bfs_fs_delete_file(fixture.fs, BFS_ROOT_INO,
                                      "hint001", 7), BFS_OK);
    bfs_btree_key_hint_cache_reset(&fixture.fs->inode_key_hints);
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[0], &inode),
                   BFS_OK);
    bfs_btree_leaf_hint_t *range =
        find_leaf_hint(&fixture.fs->inode_key_hints, inos[2]);
    TEST_ASSERT(range != NULL);
    TEST_ASSERT(range->first_key < inos[2] && inos[2] < range->last_key);

    /* The removed inode creates a gap before inos[2], so key-first_key is not
     * its actual slot. Verify that the range leaf really is sparse. */
    const bfs_node_validation_t validation = validation_for_tree(
        &fixture.fs->inode_tree);
    const uint8_t *leaf = bfs_bio_peek_valid_node(&fixture.cache.bio,
                                                   range->leaf, &validation);
    TEST_ASSERT(leaf != NULL);
    uint32_t actual_index = 0;
    TEST_ASSERT(read_inode_key_location(&fixture.fs->inode_tree, inos[2],
                                        (uint8_t *)leaf, &actual_index));
    TEST_ASSERT(actual_index != inos[2] - range->first_key);

    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, inos[2]) == NULL);
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[2], &inode),
                   BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.inode_nr), inos[2]);
    TEST_ASSERT_EQ(probe_alloc_calls, 0);

    uint16_t ranges_before_miss = fixture.fs->inode_key_hints.leaf_count;
    disable_exact_and_last_leaf(&fixture, inos[1]);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[1], &inode),
                   BFS_ERR_NOTFOUND);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_count, ranges_before_miss);
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, inos[1]) == NULL);

    fixture_close(&fixture);
}

static void test_missing_and_saturated_epoch_disable_both_shortcuts(void)
{
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    probe_install(&fixture);

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, BFS_ROOT_INO) != NULL);
    TEST_ASSERT(fixture.fs->inode_tree.hint_leaf != BFS_BLK_NULL);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_count > 0);

    uint64_t epoch = 0;
    probe_ops.mutation_epoch = NULL;
    TEST_ASSERT(!bfs_bio_get_mutation_epoch(&fixture.cache.bio, &epoch));
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_tree.hint_leaf, BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_count, 0);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_count, 0);

    probe_ops.mutation_epoch = saturated_mutation_epoch;
    TEST_ASSERT(!bfs_bio_get_mutation_epoch(&fixture.cache.bio, &epoch));
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_count, 0);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, BFS_ROOT_INO,
                                  &inode), BFS_OK);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_count, 0);

    probe_remove(&fixture);
    fixture_close(&fixture);
}

static void test_owned_leaf_update_and_later_cow_return_current_inode(void)
{
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(fixture.fs, BFS_ROOT_INO,
                                      "owned", 5, &ino), BFS_OK);
    bfs_btree_t *tree = &fixture.fs->inode_tree;
    bfs_blk_t owned_root = tree->root;
    uint32_t owned_generation = tree->generation;
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(tree, ino, &inode), BFS_OK);
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, ino) != NULL);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_count > 0);

    inode.uid = bfs_be16(0x4A31);
    TEST_ASSERT_EQ(bfs_inode_write(tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(tree->root, owned_root);
    TEST_ASSERT(tree->generation != owned_generation);
    probe_install(&fixture);
    disable_exact_and_last_leaf(&fixture, ino);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(inode.uid), 0x4A31);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_owner, tree);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_generation,
                   tree->generation);
    probe_remove(&fixture);

    TEST_ASSERT_EQ(bfs_fs_sync(fixture.fs), BFS_OK);
    bfs_blk_t committed_root = tree->root;
    uint32_t second_ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(fixture.fs, BFS_ROOT_INO,
                                      "cow", 3, &second_ino), BFS_OK);
    TEST_ASSERT(second_ino != ino);
    TEST_ASSERT(tree->root != committed_root);
    probe_install(&fixture);
    disable_exact_and_last_leaf(&fixture, ino);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(inode.uid), 0x4A31);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_root, tree->root);
    probe_remove(&fixture);
    fixture_close(&fixture);
}

static void test_direct_mapped_collisions_preserve_key_identity(void)
{
    enum { FILE_COUNT = 80 };
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    uint32_t inos[FILE_COUNT];
    TEST_ASSERT(make_many_files(&fixture, FILE_COUNT, inos));
    TEST_ASSERT_EQ(bfs_fs_sync(fixture.fs), BFS_OK);
    TEST_ASSERT(fixture.fs->inode_tree.height > 1);

    uint32_t first_key_for_slot[BFS_BTREE_KEY_HINT_SLOTS] = {0};
    uint32_t collided_old_key = 0;
    bool found_collision = false;
    bfs_inode_t inode;
    for (uint32_t i = 0; i < FILE_COUNT; i++) {
        TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, inos[i],
                                      &inode), BFS_OK);
        int slot = -1;
        for (uint32_t j = 0; j < BFS_BTREE_KEY_HINT_SLOTS; j++) {
            const bfs_btree_key_hint_t *hint =
                &fixture.fs->inode_key_hints.slots[j];
            if (hint->leaf != BFS_BLK_NULL && hint->key == inos[i]) {
                slot = (int)j;
                break;
            }
        }
        TEST_ASSERT(slot >= 0);
        if (first_key_for_slot[slot] != 0 &&
            first_key_for_slot[slot] != inos[i]) {
            collided_old_key = first_key_for_slot[slot];
            found_collision = true;
            break;
        }
        first_key_for_slot[slot] = inos[i];
    }
    TEST_ASSERT(found_collision);

    /* Isolate exact-slot mismatch: range/last-leaf routes must not satisfy
     * the old colliding key's fallback. */
    TEST_ASSERT(find_hint(&fixture.fs->inode_key_hints, collided_old_key) == NULL);
    fixture.fs->inode_key_hints.leaf_count = 0;
    fixture.fs->inode_tree.hint_leaf = BFS_BLK_NULL;
    probe_install(&fixture);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, collided_old_key,
                                  &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.inode_nr), collided_old_key);
    TEST_ASSERT(probe_alloc_calls > 0);
    probe_remove(&fixture);
    fixture_close(&fixture);
}

static void test_unlinked_and_deleted_inodes_do_not_leak_through_hints(void)
{
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    uint32_t orphan_ino = 0, deleted_ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(fixture.fs, BFS_ROOT_INO,
                                      "orphan", 6, &orphan_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(fixture.fs, BFS_ROOT_INO,
                                      "delete", 6, &deleted_ino), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, orphan_ino,
                                  &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, deleted_ino,
                                  &inode), BFS_OK);

    uint32_t result_ino = 0;
    TEST_ASSERT_EQ(bfs_fs_unlink_open_file(fixture.fs, BFS_ROOT_INO,
                                           "orphan", 6, &result_ino), BFS_OK);
    TEST_ASSERT_EQ(result_ino, orphan_ino);
    TEST_ASSERT_EQ(bfs_inode_read_unlinked(&fixture.fs->inode_tree, orphan_ino,
                                           &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 0);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, orphan_ino,
                                  &inode), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(bfs_fs_reap_unlinked_file(fixture.fs, orphan_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read_unlinked(&fixture.fs->inode_tree, orphan_ino,
                                           &inode), BFS_ERR_NOTFOUND);

    TEST_ASSERT_EQ(bfs_fs_delete_file(fixture.fs, BFS_ROOT_INO,
                                      "delete", 6), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs->inode_tree, deleted_ino,
                                  &inode), BFS_ERR_NOTFOUND);
    fixture_close(&fixture);
}

static void test_invalidation_rechecks_crc_and_inode_identity_after_raw_write(void)
{
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    bfs_btree_t *tree = &fixture.fs->inode_tree;
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(tree, BFS_ROOT_INO, &inode), BFS_OK);
    bfs_btree_key_hint_t *hint = find_hint(&fixture.fs->inode_key_hints,
                                           BFS_ROOT_INO);
    TEST_ASSERT(hint != NULL);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_count > 0);
    TEST_ASSERT(find_leaf_hint(&fixture.fs->inode_key_hints, BFS_ROOT_INO) !=
                NULL);
    bfs_blk_t leaf_block = hint->leaf;

    uint8_t *original = (uint8_t *)malloc(HINT_BLOCK_SIZE);
    uint8_t *changed = (uint8_t *)malloc(HINT_BLOCK_SIZE);
    TEST_ASSERT(original != NULL && changed != NULL);
    TEST_ASSERT_EQ(bfs_bio_read(fixture.device, leaf_block, original), BFS_OK);
    memcpy(changed, original, HINT_BLOCK_SIZE);
    uint32_t index = 0;
    TEST_ASSERT(read_inode_key_location(tree, BFS_ROOT_INO, changed, &index));
    ((bfs_inode_t *)leaf_val(tree, changed, index))->inode_nr = bfs_be32(7777);
    recalculate_node_crc(tree, changed);
    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, leaf_block, changed), BFS_OK);

    uint64_t epoch_before = fixture.cache.mutation_epoch;
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT(fixture.cache.mutation_epoch > epoch_before);
    probe_install(&fixture);
    disable_exact_and_last_leaf(&fixture, BFS_ROOT_INO);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(tree, BFS_ROOT_INO, &inode), BFS_ERR_CORRUPT);
    TEST_ASSERT(probe_alloc_calls > 0);
    /* The B-tree leaf has a valid CRC/order, so its route may be repopulated
     * before full inode validation rejects the bad record. Reusing that
     * current route must still reject the inode identity, never cache it as a
     * successful inode read. */
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_mutation_epoch,
                   fixture.cache.mutation_epoch);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_mutation_epoch > epoch_before);
    disable_exact_and_last_leaf(&fixture, BFS_ROOT_INO);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(tree, BFS_ROOT_INO, &inode), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(probe_alloc_calls, 0);
    probe_remove(&fixture);

    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, leaf_block, original), BFS_OK);
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT_EQ(bfs_inode_read(tree, BFS_ROOT_INO, &inode), BFS_OK);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_count > 0);

    memcpy(changed, original, HINT_BLOCK_SIZE);
    ((bfs_inode_t *)leaf_val(tree, changed, index))->uid ^= bfs_be16(0x0100);
    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, leaf_block, changed), BFS_OK);
    epoch_before = fixture.cache.mutation_epoch;
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT(fixture.cache.mutation_epoch > epoch_before);
    probe_install(&fixture);
    disable_exact_and_last_leaf(&fixture, BFS_ROOT_INO);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(tree, BFS_ROOT_INO, &inode), BFS_ERR_CORRUPT);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.fs->inode_key_hints.leaf_count, 0);
    probe_remove(&fixture);

    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, leaf_block, original), BFS_OK);
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT_EQ(bfs_inode_read(tree, BFS_ROOT_INO, &inode), BFS_OK);
    free(changed);
    free(original);
    fixture_close(&fixture);
}

static void test_internal_path_raw_write_invalidates_both_leaf_shortcuts(void)
{
    enum { FILE_COUNT = 80 };
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, 0));
    uint32_t inos[FILE_COUNT];
    TEST_ASSERT(make_many_files(&fixture, FILE_COUNT, inos));
    bfs_blk_t clone_block = bfs_freespace_alloc(&fixture.fs->freespace, 1);
    TEST_ASSERT(clone_block != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_fs_sync(fixture.fs), BFS_OK);

    bfs_btree_t *tree = &fixture.fs->inode_tree;
    TEST_ASSERT(tree->height > 1);
    bfs_blk_t original_root = tree->root;
    uint32_t original_generation = tree->generation;
    uint8_t *root_image = (uint8_t *)malloc(HINT_BLOCK_SIZE);
    uint8_t *leaf_image = (uint8_t *)malloc(HINT_BLOCK_SIZE);
    TEST_ASSERT(root_image != NULL && leaf_image != NULL);
    TEST_ASSERT_EQ(bfs_bio_read(&fixture.cache.bio, original_root,
                                root_image), BFS_OK);
    TEST_ASSERT_EQ(node_level(root_image), tree->height - 1u);
    bfs_blk_t original_leaf = get_child(tree, root_image, 0);
    TEST_ASSERT(original_leaf != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_bio_read(&fixture.cache.bio, original_leaf,
                                leaf_image), BFS_OK);
    TEST_ASSERT_EQ(node_level(leaf_image), BFS_BTNODE_LEAF);
    TEST_ASSERT(num_keys(leaf_image) > 0);

    uint32_t target_ino = bfs_load_be32(node_key(tree, leaf_image, 0));
    uint32_t target_index = 0;
    TEST_ASSERT(read_inode_key_location(tree, target_ino, leaf_image,
                                        &target_index));
    bfs_inode_t current;
    TEST_ASSERT_EQ(bfs_inode_read(tree, target_ino, &current), BFS_OK);
    bfs_btree_key_hint_t *warm_hint = find_hint(&fixture.fs->inode_key_hints,
                                                target_ino);
    TEST_ASSERT(warm_hint != NULL);
    TEST_ASSERT_EQ(warm_hint->leaf, original_leaf);
    TEST_ASSERT_EQ(warm_hint->index, target_index);
    TEST_ASSERT(tree->hint_leaf == original_leaf);
    TEST_ASSERT(fixture.fs->inode_key_hints.leaf_count > 0);
    TEST_ASSERT(find_leaf_hint(&fixture.fs->inode_key_hints, target_ino) !=
                NULL);
    const bfs_node_validation_t validation = {
        .key_compare = tree->ops->key_compare,
        .key_size = tree->ops->key_size,
        .val_size = tree->ops->val_size,
        .block_size = tree->bio->block_size,
        .block_count = tree->bio->block_count,
    };
    TEST_ASSERT(bfs_bio_peek_valid_node(&fixture.cache.bio, original_leaf,
                                        &validation) != NULL);

    ((bfs_inode_t *)leaf_val(tree, leaf_image, target_index))->uid =
        bfs_be16(0x6B27);
    recalculate_node_crc(tree, leaf_image);
    set_child(tree, root_image, 0, clone_block);
    recalculate_node_crc(tree, root_image);

    uint64_t epoch_before = fixture.cache.mutation_epoch;
    TEST_ASSERT_EQ(bfs_bio_write(&fixture.cache.bio, clone_block,
                                 leaf_image), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_write(&fixture.cache.bio, original_root,
                                 root_image), BFS_OK);
    TEST_ASSERT(fixture.cache.mutation_epoch > epoch_before);
    TEST_ASSERT_EQ(tree->root, original_root);
    TEST_ASSERT_EQ(tree->generation, original_generation);
    TEST_ASSERT(bfs_bio_peek_valid_node(&fixture.cache.bio, original_leaf,
                                        &validation) != NULL);

    probe_install(&fixture);
    disable_exact_and_last_leaf(&fixture, target_ino);
    probe_alloc_calls = 0;
    TEST_ASSERT_EQ(bfs_inode_read(tree, target_ino, &current), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(current.uid), 0x6B27);
    TEST_ASSERT(probe_alloc_calls > 0);
    TEST_ASSERT_EQ(tree->hint_leaf, clone_block);
    bfs_btree_leaf_hint_t *current_range = find_leaf_hint(
        &fixture.fs->inode_key_hints, target_ino);
    TEST_ASSERT(current_range != NULL);
    TEST_ASSERT_EQ(current_range->leaf, clone_block);
    fixture.abandon_on_close = true;
    probe_remove(&fixture);
    free(leaf_image);
    free(root_image);
    fixture_close(&fixture);
}

static void test_range_table_is_sorted_bounded_and_replaces_old_leaves(void)
{
    enum {
        TEST_BLOCK_SIZE = 1024,
        TEST_BLOCK_COUNT = 8192,
        KEY_COUNT = 700,
        MAX_LEAVES = 256,
        CACHE_SLOTS = 128,
    };
    static const bfs_btree_ops_t range_ops = {
        .key_compare = bfs_btree_key_compare_be32,
        .key_size = sizeof(uint32_t),
        .val_size = sizeof(bfs_inode_t),
        .cache_key_order = true,
    };
    bfs_bio_t *device;
    bfs_cache_t cache;
    hint_bootstrap_alloc_t *allocator;
    bfs_btree_t tree;
    bfs_btree_key_hint_cache_t hints;
    uint32_t first_keys[MAX_LEAVES];
    bfs_blk_t leaf_blocks[MAX_LEAVES];
    uint8_t value[sizeof(bfs_inode_t)] = {0};
    uint8_t found_value[sizeof(bfs_inode_t)];

    (void)unlink(HINT_IMAGE);
    device = bio_emu_create(HINT_IMAGE, TEST_BLOCK_SIZE, TEST_BLOCK_COUNT);
    TEST_ASSERT(device != NULL);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, device, CACHE_SLOTS), BFS_OK);
    allocator = bootstrap_create(2, TEST_BLOCK_COUNT);
    TEST_ASSERT(allocator != NULL);
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &cache.bio, &allocator->base,
                                  &range_ops, BFS_BLK_NULL, 1), BFS_OK);

    for (uint32_t i = 1; i <= KEY_COUNT; i++) {
        uint32_t key = bfs_be32(i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, value), BFS_OK);
    }
    TEST_ASSERT(tree.height > 1);

    /* First enumerate the actual leaf boundaries with both hint paths
     * unavailable, then cache one positive lookup per leaf. */
    uint32_t distinct_leaves = 0;
    bfs_blk_t previous_leaf = BFS_BLK_NULL;
    for (uint32_t i = 1; i <= KEY_COUNT; i++) {
        uint32_t key = bfs_be32(i);
        tree.hint_leaf = BFS_BLK_NULL;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, found_value), BFS_OK);
        TEST_ASSERT(tree.hint_leaf != BFS_BLK_NULL);
        if (tree.hint_leaf != previous_leaf) {
            TEST_ASSERT(distinct_leaves < MAX_LEAVES);
            first_keys[distinct_leaves] = i;
            leaf_blocks[distinct_leaves] = tree.hint_leaf;
            distinct_leaves++;
            previous_leaf = tree.hint_leaf;
        }
    }
    TEST_ASSERT(distinct_leaves > BFS_BTREE_LEAF_HINT_SLOTS);

    bfs_btree_key_hint_cache_reset(&hints);
    tree.key_hint_cache = &hints;
    for (uint32_t i = 0; i < distinct_leaves; i++) {
        uint32_t key = bfs_be32(first_keys[i]);
        tree.hint_leaf = BFS_BLK_NULL;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, found_value), BFS_OK);
        TEST_ASSERT_EQ(tree.hint_leaf, leaf_blocks[i]);
    }

    TEST_ASSERT_EQ(hints.leaf_count, BFS_BTREE_LEAF_HINT_SLOTS);
    TEST_ASSERT(hints.leaf_next_victim < BFS_BTREE_LEAF_HINT_SLOTS);
    TEST_ASSERT_EQ(hints.leaf_owner, &tree);
    TEST_ASSERT_EQ(hints.leaf_root, tree.root);
    TEST_ASSERT_EQ(hints.leaf_generation, tree.generation);
    uint64_t epoch = 0;
    TEST_ASSERT(bfs_bio_get_mutation_epoch(&cache.bio, &epoch));
    TEST_ASSERT_EQ(hints.leaf_mutation_epoch, epoch);
    for (uint32_t i = 0; i < hints.leaf_count; i++) {
        TEST_ASSERT(hints.leaf_slots[i].leaf != BFS_BLK_NULL);
        if (i > 0) {
            TEST_ASSERT(hints.leaf_slots[i - 1].first_key <
                        hints.leaf_slots[i].first_key);
            TEST_ASSERT(hints.leaf_slots[i - 1].last_key <
                        hints.leaf_slots[i].first_key);
        }
    }
    TEST_ASSERT(find_leaf_hint(&hints, first_keys[0]) == NULL);
    TEST_ASSERT(find_leaf_hint(&hints,
                               first_keys[distinct_leaves - 1]) != NULL);

    tree.key_hint_cache = NULL;
    bfs_cache_destroy(&cache);
    free(allocator);
    bfs_bio_close(device);
    (void)unlink(HINT_IMAGE);
}

static void test_reload_remount_pointer_lifetime_and_snapshot_view(void)
{
    hint_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, BFS_OPT_SNAPSHOTS));
    bfs_fs_t *original_fs = fixture.fs;
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(original_fs, BFS_ROOT_INO,
                                      "view", 4, &ino), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&original_fs->inode_tree, ino, &inode), BFS_OK);
    inode.uid = bfs_be16(0x1234);
    TEST_ASSERT_EQ(bfs_inode_write(&original_fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(original_fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(original_fs, "before-hints"), BFS_OK);

    TEST_ASSERT_EQ(bfs_inode_read(&original_fs->inode_tree, ino, &inode), BFS_OK);
    inode.uid = bfs_be16(0x5678);
    TEST_ASSERT_EQ(bfs_inode_write(&original_fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(original_fs), BFS_OK);

    bfs_snapshot_record_t record;
    TEST_ASSERT_EQ(bfs_snapshot_find_by_name(original_fs, "before-hints",
                                             NULL, &record), BFS_OK);
    bfs_btree_t snapshot_inode;
    TEST_ASSERT_EQ(bfs_snapshot_open(&record, &fixture.cache.bio,
                                     bfs_freespace_allocator(&original_fs->freespace),
                                     NULL, &snapshot_inode), BFS_OK);
    TEST_ASSERT_EQ(snapshot_inode.key_hint_cache, NULL);
    bfs_inode_t snapshot_value;
    TEST_ASSERT_EQ(bfs_inode_read(&snapshot_inode, ino, &snapshot_value), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(snapshot_value.uid), 0x1234);

    TEST_ASSERT_EQ(bfs_inode_read(&original_fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(inode.uid), 0x5678);
    TEST_ASSERT_EQ(original_fs->inode_tree.key_hint_cache,
                   &original_fs->inode_key_hints);
    TEST_ASSERT(find_hint(&original_fs->inode_key_hints, ino) != NULL);
    TEST_ASSERT(original_fs->inode_key_hints.leaf_count > 0);

    /* Add an impossible stale route: reload must clear it, although its
     * legitimate root-inode validation can repopulate one current route. */
    uint16_t stale_index = original_fs->inode_key_hints.leaf_count;
    TEST_ASSERT(stale_index < BFS_BTREE_LEAF_HINT_SLOTS);
    original_fs->inode_key_hints.leaf_slots[stale_index] =
        (bfs_btree_leaf_hint_t){ UINT32_MAX, UINT32_MAX,
                               original_fs->inode_tree.root };
    original_fs->inode_key_hints.leaf_count++;

    bfs_lock_write(&original_fs->lock);
    bfs_err_t reload_error = bfs_fs_reload_committed_unlocked(original_fs);
    bfs_lock_unlock(&original_fs->lock);
    TEST_ASSERT_EQ(reload_error, BFS_OK);
    TEST_ASSERT_EQ(original_fs->inode_tree.key_hint_cache,
                   &original_fs->inode_key_hints);
    TEST_ASSERT(find_hint(&original_fs->inode_key_hints, ino) == NULL);
    TEST_ASSERT(find_leaf_hint(&original_fs->inode_key_hints, UINT32_MAX) == NULL);
    TEST_ASSERT_EQ(original_fs->inode_key_hints.leaf_count, 1);
    TEST_ASSERT(find_leaf_hint(&original_fs->inode_key_hints, BFS_ROOT_INO) != NULL);
    TEST_ASSERT_EQ(bfs_inode_read(&original_fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(inode.uid), 0x5678);

    bfs_fs_abandon(original_fs);
    bfs_fs_t *remounted = (bfs_fs_t *)calloc(1, sizeof(*remounted));
    TEST_ASSERT(remounted != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount(remounted, &fixture.cache.bio), BFS_OK);
    TEST_ASSERT_EQ(remounted->inode_tree.key_hint_cache,
                   &remounted->inode_key_hints);
    TEST_ASSERT(remounted->inode_tree.key_hint_cache !=
                original_fs->inode_tree.key_hint_cache);
    TEST_ASSERT(find_hint(&remounted->inode_key_hints, ino) == NULL);
    TEST_ASSERT_EQ(remounted->inode_key_hints.leaf_count, 1);
    TEST_ASSERT_EQ(remounted->inode_key_hints.leaf_owner, &remounted->inode_tree);
    TEST_ASSERT(find_leaf_hint(&remounted->inode_key_hints, UINT32_MAX) == NULL);
    TEST_ASSERT_EQ(bfs_inode_read(&remounted->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(inode.uid), 0x5678);

    TEST_ASSERT_EQ(bfs_snapshot_find_by_name(remounted, "before-hints",
                                             NULL, &record), BFS_OK);
    bfs_btree_t remounted_snapshot;
    TEST_ASSERT_EQ(bfs_snapshot_open(&record, &fixture.cache.bio,
                                     bfs_freespace_allocator(&remounted->freespace),
                                     NULL, &remounted_snapshot), BFS_OK);
    TEST_ASSERT_EQ(remounted_snapshot.key_hint_cache, NULL);
    TEST_ASSERT_EQ(bfs_inode_read(&remounted_snapshot, ino, &snapshot_value), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(snapshot_value.uid), 0x1234);

    free(original_fs);
    fixture.fs = remounted;
    fixture_close(&fixture);
}

TEST_SUITE_BEGIN("Inode B-tree lookup hints")
    TEST_RUN(test_warm_hint_skips_traversal_buffer_and_checks_index);
    TEST_RUN(test_distinct_key_range_hit_skips_root_traversal);
    TEST_RUN(test_sparse_range_uses_checked_search_and_misses_fall_back);
    TEST_RUN(test_missing_and_saturated_epoch_disable_both_shortcuts);
    TEST_RUN(test_owned_leaf_update_and_later_cow_return_current_inode);
    TEST_RUN(test_direct_mapped_collisions_preserve_key_identity);
    TEST_RUN(test_unlinked_and_deleted_inodes_do_not_leak_through_hints);
    TEST_RUN(test_invalidation_rechecks_crc_and_inode_identity_after_raw_write);
    TEST_RUN(test_internal_path_raw_write_invalidates_both_leaf_shortcuts);
    TEST_RUN(test_range_table_is_sorted_bounded_and_replaces_old_leaves);
    TEST_RUN(test_reload_remount_pointer_lifetime_and_snapshot_view);
TEST_SUITE_END()
