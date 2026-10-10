/* SPDX-License-Identifier: MPL-2.0 */
/* Portable offline/in-handler BFS structural checker. */

#include "bfs_fsck.h"

#include "bfs_alloc.h"
#include "bfs_btree.h"
#include "bfs_crc32.h"
#include "bfs_dir.h"
#include "bfs_extent.h"
#include "bfs_inode.h"
#include "bfs_ondisk.h"
#include "bfs_refcount.h"
#include "bfs_snapshot.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    bfs_fs_t *fs;
    bfs_fsck_report_t report;
    uint8_t *block_map;
    uint8_t *reference_map;
    uint8_t *data_buffer;
    uint32_t block_count;
    bool reference_saturated;
} check_state_t;

/* Inode numbers that own a comment record, in ascending order. */
typedef struct {
    uint32_t *items;
    size_t count;
    size_t capacity;
    size_t matched;
    bool failed;
} comment_set_t;

/* A directory inode and the directory that holds it. */
typedef struct {
    uint32_t dir;
    uint32_t parent;
} dir_pair_t;

typedef struct {
    check_state_t *state;
    bfs_btree_t *inode_tree;
    comment_set_t *comments;
    /* Directory entries that name a directory, ascending by directory, once
     * the namespace check has matched them with the parent links. */
    bool named_checked;
    const dir_pair_t *named;
    size_t named_count;
} check_context_t;

#define BFS_REFERENCE_MAX 255u

static const bfs_btree_ops_t snapshot_ops = {
    .key_compare = bfs_btree_key_compare_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_snapshot_record_t),
    .cache_key_order = true,
};

static void check_error(check_state_t *state)
{
    state->report.errors++;
}

static void check_warning(check_state_t *state)
{
    state->report.warnings++;
}

static void mark(check_state_t *state, uint32_t block, uint8_t type)
{
    if (block >= state->block_count) {
        check_error(state);
        return;
    }
    uint8_t previous = state->block_map[block] & 0x7fu;
    if (previous && previous != type) check_error(state);
    state->block_map[block] = (state->block_map[block] & 0x80u) | type;
}

static void mark_reference(check_state_t *state, uint32_t block, uint8_t type)
{
    mark(state, block, type);
    if (block >= state->block_count) return;
    if (state->reference_map[block] == BFS_REFERENCE_MAX) {
        state->reference_saturated = true;
        return;
    }
    state->reference_map[block]++;
}

static void mark_range(check_state_t *state, uint32_t start, uint32_t count,
                       uint8_t type, bool reference)
{
    uint64_t end = (uint64_t)start + count;
    if (start >= state->block_count || end > state->block_count) {
        check_error(state);
        if (start >= state->block_count) return;
        count = state->block_count - start;
    }
    for (uint32_t index = 0; index < count; index++) {
        if (reference) mark_reference(state, start + index, type);
        else mark(state, start + index, type);
    }
}

static void node_cb(bfs_blk_t block, void *context)
{
    mark((check_state_t *)context, block, 2);
}

static void reference_node_cb(bfs_blk_t block, void *context)
{
    mark_reference((check_state_t *)context, block, 2);
}

static bool free_cb(const void *key, const void *value, void *context)
{
    check_state_t *state = (check_state_t *)context;
    mark_range(state, bfs_load_be32(key), bfs_load_be32(value), 1, false);
    return true;
}

static bool extent_data_cb(const void *key, const void *value, void *context)
{
    (void)key;
    check_state_t *state = (check_state_t *)context;
    const bfs_extent_val_t *extent = (const bfs_extent_val_t *)value;
    bfs_blk_t disk_block = bfs_be32(extent->disk_block);
    uint32_t length = bfs_be32(extent->length);

    mark_range(state, disk_block, length, 3, true);
    if (!state->fs->data_checksums) return true;
    if (length != 1 || !state->data_buffer ||
        bfs_bio_read(state->fs->bio, disk_block, state->data_buffer) != BFS_OK ||
        bfs_be32(extent->data_crc32) == 0 ||
        bfs_be32(extent->data_crc32) !=
        bfs_crc32(0, state->data_buffer, state->fs->bio->block_size))
        check_error(state);
    return true;
}

static void mark_inode_extents(check_state_t *state, const bfs_inode_t *inode)
{
    bfs_extent_tree_t tree;
    if (bfs_extent_open(&tree, state->fs->bio, &state->fs->freespace,
                        inode, bfs_txn_id(&state->fs->txn)) != BFS_OK) {
        check_error(state);
        return;
    }
    if (bfs_extent_is_inline(&tree)) {
        /* One record at logical block 0, checked like a tree record. */
        const bfs_extent_val_t extent = {
            bfs_be32(tree.inline_start), bfs_be32(tree.inline_length),
            bfs_be32(tree.inline_crc),
        };
        extent_data_cb(NULL, &extent, state);
        return;
    }
    if (tree.tree.root == BFS_BLK_NULL) return;
    if (bfs_btree_walk_nodes(&tree.tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    if (bfs_btree_scan(&tree.tree, NULL, extent_data_cb, state) != BFS_OK)
        check_error(state);
}

static const dir_pair_t *find_pair(const dir_pair_t *items, size_t count, uint32_t dir)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (items[mid].dir == dir) return &items[mid];
        if (items[mid].dir < dir) low = mid + 1;
        else high = mid;
    }
    return NULL;
}

static bool named_contains(const check_context_t *check, uint32_t ino)
{
    return find_pair(check->named, check->named_count, ino) != NULL;
}

static bool comment_set_contains(const comment_set_t *set, uint32_t ino)
{
    size_t low = 0, high = set->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (set->items[mid] == ino) return true;
        if (set->items[mid] < ino) low = mid + 1;
        else high = mid;
    }
    return false;
}

static bool inode_extent_cb(const void *key, const void *value, void *context)
{
    check_context_t *check = (check_context_t *)context;
    const bfs_inode_t *inode = (const bfs_inode_t *)value;
    uint32_t ino = bfs_load_be32(key);
    if (!bfs_inode_valid(check->inode_tree, ino, inode)) check_error(check->state);
    /* Every directory but the root has an entry; the namespace check has
     * already matched those entries with the parent links. */
    if (check->named_checked && bfs_be32(inode->type) == BFS_INODE_DIR &&
        ino != BFS_ROOT_INO && !named_contains(check, ino))
        check_error(check->state);
    mark_inode_extents(check->state, inode);
    if (check->comments && !check->comments->failed) {
        bool flagged = (bfs_be32(inode->flags) & BFS_INODE_FLAG_HAS_COMMENT) != 0;
        bool present = comment_set_contains(check->comments, ino);
        if (flagged != present) check_error(check->state);
        if (present) check->comments->matched++;
    }
    return true;
}

/* Grow an array by copying; the AmigaOS stdlib shim has no realloc. */
static bool grow_items(void **items, size_t count, size_t *capacity, size_t item_size)
{
    if (count < *capacity) return true;
    size_t next = *capacity ? *capacity * 2u : 16u;
    if (next > SIZE_MAX / item_size) return false;
    uint8_t *bigger = malloc(next * item_size);
    if (!bigger) return false;
    if (count) memcpy(bigger, *items, count * item_size);
    free(*items);
    *items = bigger;
    *capacity = next;
    return true;
}

typedef struct {
    dir_pair_t *items;
    size_t count;
    size_t capacity;
} dir_pair_list_t;

#define NAME_GROUP_MAX 16u

/* State of one namespace check over a directory tree. */
typedef struct {
    check_context_t *check;
    dir_pair_list_t named;   /* directory entries that name a directory */
    dir_pair_list_t links;   /* parent links, ascending by directory */
    uint32_t roots;
    uint32_t checked_owner; /* last owner found to be a directory */
    bool failed;
    /* Names that share the current owner and hash. */
    bool group_valid;
    uint32_t group_owner;
    uint32_t group_hash;
    size_t group_count;
    uint8_t group_lens[NAME_GROUP_MAX];
    char group_names[NAME_GROUP_MAX][BFS_NAME_MAX];
} namespace_check_t;

static bool pair_add(namespace_check_t *ns, dir_pair_list_t *list,
                     uint32_t dir, uint32_t parent)
{
    if (!grow_items((void **)&list->items, list->count, &list->capacity,
                    sizeof(*list->items))) {
        ns->failed = true;
        return false;
    }
    list->items[list->count].dir = dir;
    list->items[list->count].parent = parent;
    list->count++;
    return true;
}

static void pair_sift(dir_pair_t *items, size_t root, size_t count)
{
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= count) return;
        if (child + 1 < count && items[child + 1].dir > items[child].dir) child++;
        if (items[root].dir >= items[child].dir) return;
        dir_pair_t swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
}

static void pair_sort(dir_pair_list_t *list)
{
    for (size_t index = list->count / 2; index-- > 0;)
        pair_sift(list->items, index, list->count);
    for (size_t end = list->count; end-- > 1;) {
        dir_pair_t swap = list->items[0];
        list->items[0] = list->items[end];
        list->items[end] = swap;
        pair_sift(list->items, 0, end);
    }
}

static bool names_equal_folded(const char *a, uint8_t a_len, const char *b, uint8_t b_len)
{
    if (a_len != b_len) return false;
    for (uint8_t index = 0; index < a_len; index++)
        if (bfs_intl_toupper((uint8_t)a[index]) != bfs_intl_toupper((uint8_t)b[index]))
            return false;
    return true;
}

static bool ns_entry_cb(uint32_t parent, const char *name, uint8_t name_len,
                        uint32_t inode_nr, uint32_t entry_type,
                        const bfs_dir_pos_t *pos, void *ctx)
{
    namespace_check_t *ns = (namespace_check_t *)ctx;
    check_state_t *state = ns->check->state;
    if (parent == 0) {
        ns->roots++;
        if (inode_nr != BFS_ROOT_INO) check_error(state);
    } else if (parent != ns->checked_owner) {
        /* Entries belong to directories. Owners arrive in ascending order. */
        bfs_inode_t owner;
        if (bfs_inode_read(ns->check->inode_tree, parent, &owner) != BFS_OK ||
            bfs_be32(owner.type) != BFS_INODE_DIR)
            check_error(state);
        ns->checked_owner = parent;
    }

    /* Two names that differ only by case cannot share a directory. */
    if (!ns->group_valid || ns->group_owner != parent || ns->group_hash != pos->hash) {
        ns->group_valid = true;
        ns->group_owner = parent;
        ns->group_hash = pos->hash;
        ns->group_count = 0;
    }
    for (size_t index = 0; index < ns->group_count; index++)
        if (names_equal_folded(ns->group_names[index], ns->group_lens[index], name, name_len))
            check_error(state);
    if (ns->group_count < NAME_GROUP_MAX) {
        memcpy(ns->group_names[ns->group_count], name, name_len);
        ns->group_lens[ns->group_count++] = name_len;
    } else {
        check_warning(state);
    }

    bfs_inode_t inode;
    if (bfs_inode_read(ns->check->inode_tree, inode_nr, &inode) != BFS_OK ||
        bfs_be32(inode.type) != entry_type)
        check_error(state);
    if (entry_type == BFS_INODE_DIR && parent != 0)
        return pair_add(ns, &ns->named, inode_nr, parent);
    return true;
}

static bool ns_parent_cb(uint32_t dir_ino, uint32_t parent, void *ctx)
{
    namespace_check_t *ns = (namespace_check_t *)ctx;
    return pair_add(ns, &ns->links, dir_ino, parent);
}

static bool ns_comment_cb(uint32_t ino, const char *text, uint8_t len, void *ctx)
{
    (void)text;
    (void)len;
    namespace_check_t *ns = (namespace_check_t *)ctx;
    comment_set_t *set = ns->check->comments;
    if (!set) return true;
    if (set->count && set->items[set->count - 1] >= ino) {
        check_error(ns->check->state);
        return true;
    }
    if (!grow_items((void **)&set->items, set->count, &set->capacity, sizeof(*set->items))) {
        set->failed = true;
        return false;
    }
    set->items[set->count++] = ino;
    return true;
}

/* Every directory other than the root has exactly one entry, in the directory
 * that its parent link names, and nothing else has a parent link. */
static void check_parent_links(check_state_t *state, namespace_check_t *ns)
{
    pair_sort(&ns->named);
    size_t named = 0, links = 0;
    while (named < ns->named.count || links < ns->links.count) {
        if (named + 1 < ns->named.count &&
            ns->named.items[named].dir == ns->named.items[named + 1].dir) {
            check_error(state); /* a directory with two names */
            named++;
            continue;
        }
        if (named < ns->named.count && links < ns->links.count &&
            ns->named.items[named].dir == ns->links.items[links].dir) {
            if (ns->named.items[named].parent != ns->links.items[links].parent)
                check_error(state);
            named++;
            links++;
            continue;
        }
        check_error(state); /* an entry without a link, or a link without an entry */
        if (named < ns->named.count &&
            (links >= ns->links.count ||
             ns->named.items[named].dir < ns->links.items[links].dir))
            named++;
        else
            links++;
    }
}

/* Following parent links from every directory reaches the root, so no
 * directory lies in a cycle cut off from it. named is sorted. */
static void check_reachable(check_state_t *state, const dir_pair_list_t *named)
{
    for (size_t index = 0; index < named->count; index++) {
        uint32_t dir = named->items[index].parent;
        size_t steps = 0;
        while (dir != BFS_ROOT_INO) {
            const dir_pair_t *pair = find_pair(named->items, named->count, dir);
            if (!pair || ++steps > named->count) {
                check_error(state);
                break;
            }
            dir = pair->parent;
        }
    }
}

/* Check the namespace of one directory tree against its inode tree, mark the
 * extents of every inode, and check that HAS_COMMENT is set exactly for the
 * inodes that own a comment record. */
static void mark_inode_payloads(check_state_t *state, bfs_dir_tree_t *dir_tree,
                                bfs_btree_t *tree)
{
    comment_set_t comments = {0};
    check_context_t context = { .state = state, .inode_tree = tree,
                                .comments = dir_tree ? &comments : NULL };
    namespace_check_t *ns = NULL;
    if (dir_tree) {
        ns = calloc(1, sizeof(*ns));
        if (!ns) {
            check_error(state);
            context.comments = NULL;
        } else {
            ns->check = &context;
            static const bfs_dir_walk_ops_t ops = {
                .entry = ns_entry_cb, .parent_link = ns_parent_cb, .comment = ns_comment_cb,
            };
            if (bfs_dir_walk(dir_tree, &ops, ns) != BFS_OK || ns->failed || comments.failed) {
                check_error(state);
            } else if (ns->roots != 1) {
                check_error(state);
            } else {
                check_parent_links(state, ns);
                check_reachable(state, &ns->named);
                context.named_checked = true;
                context.named = ns->named.items;
                context.named_count = ns->named.count;
            }
        }
    }
    if (bfs_btree_scan(tree, NULL, inode_extent_cb, &context) != BFS_OK)
        check_error(state);
    if (context.comments && !comments.failed && comments.matched != comments.count)
        check_error(state); /* a comment without its inode */
    free(comments.items);
    if (ns) {
        free(ns->named.items);
        free(ns->links.items);
        free(ns);
    }
}

static bool snapshot_mark_cb(uint32_t id, const bfs_snapshot_record_t *record, void *context)
{
    (void)id;
    check_state_t *state = (check_state_t *)context;
    uint64_t transaction_id = bfs_snapshot_record_txn_id(record);
    bfs_dir_tree_t dir_tree;
    bfs_btree_t inode_tree;
    bool dir_valid = bfs_dir_init(&dir_tree, state->fs->bio,
                                  bfs_freespace_allocator(&state->fs->freespace),
                                  bfs_be32(record->dir_tree_root),
                                  transaction_id) == BFS_OK;

    if (!dir_valid ||
        bfs_btree_walk_nodes(&dir_tree.tree, reference_node_cb, state) != BFS_OK) {
        check_error(state);
    }
    if (bfs_inode_init(&inode_tree, state->fs->bio, bfs_freespace_allocator(&state->fs->freespace),
                       bfs_be32(record->inode_tree_root), transaction_id) != BFS_OK) {
        check_error(state);
        return true;
    }
    if (bfs_btree_walk_nodes(&inode_tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    mark_inode_payloads(state, dir_valid ? &dir_tree : NULL, &inode_tree);
    return true;
}

static bool refcount_check_cb(const void *key, const void *value, void *context)
{
    check_state_t *state = (check_state_t *)context;
    bfs_blk_t block = bfs_load_be32(key);
    uint32_t stored = bfs_load_be32(value);

    if (block == BFS_BLK_NULL || block >= state->block_count || stored < 2) {
        check_error(state);
    } else if (state->reference_map[block] <= 1 ||
               (state->reference_map[block] != BFS_REFERENCE_MAX &&
                stored != state->reference_map[block])) {
        check_error(state);
    }
    if (block < state->block_count) state->block_map[block] |= 0x80u;
    return true;
}

static void scan(check_state_t *state)
{
    bfs_fs_t *fs = state->fs;
    const bfs_superblock_t *superblock = &fs->txn.sb;
    uint32_t data_start = bfs_data_start_block(fs->bio->block_size);
    uint64_t backup_offset = bfs_sb_backup_offset(superblock);
    uint64_t backup_block = backup_offset / fs->bio->block_size;

    mark_range(state, 0, data_start, 2, false);
    if (backup_block >= state->block_count) check_error(state);
    else mark(state, (uint32_t)backup_block, 2);
    for (uint32_t index = 0; index < bfs_be32(superblock->emergency_count); index++)
        mark(state, bfs_be32(superblock->emergency_pool[index]), 2);
    for (uint32_t index = 0; index < fs->freespace.reserve_count; index++)
        mark(state, fs->freespace.reserve[index], 2);

    if (bfs_btree_walk_nodes(&fs->freespace.tree, node_cb, state) != BFS_OK)
        check_error(state);
    if (bfs_btree_walk_nodes(&fs->dir_tree.tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    if (bfs_btree_walk_nodes(&fs->inode_tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    if (fs->has_snapshots &&
        bfs_btree_walk_nodes(&fs->refcount.tree, node_cb, state) != BFS_OK)
        check_error(state);

    bfs_blk_t snapshot_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snapshot_root != BFS_BLK_NULL) {
        bfs_btree_t snapshot_tree;
        if (bfs_btree_init(&snapshot_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                           &snapshot_ops, snapshot_root, bfs_txn_id(&fs->txn)) != BFS_OK ||
            bfs_btree_walk_nodes(&snapshot_tree, node_cb, state) != BFS_OK ||
            bfs_snapshot_list(fs, snapshot_mark_cb, state) != BFS_OK)
            check_error(state);
    }

    if (bfs_btree_scan(&fs->freespace.tree, NULL, free_cb, state) != BFS_OK)
        check_error(state);
    mark_inode_payloads(state, &fs->dir_tree, &fs->inode_tree);

    if (fs->has_snapshots) {
        if (bfs_btree_scan(&fs->refcount.tree, NULL, refcount_check_cb, state) != BFS_OK)
            check_error(state);
        for (uint32_t block = 0; block < state->block_count; block++) {
            if (state->reference_map[block] > 1 && !(state->block_map[block] & 0x80u))
                check_error(state);
        }
        if (state->reference_saturated) check_warning(state);
    } else {
        /* Without snapshots every node and data block has one owner. */
        for (uint32_t block = 0; block < state->block_count; block++) {
            if (state->reference_map[block] > 1) check_error(state);
        }
    }
}

bfs_err_t bfs_fs_check(bfs_fs_t *fs, bool repair, bfs_fsck_report_t *report)
{
    check_state_t state;
    if (report) memset(report, 0, sizeof(*report));
    memset(&state, 0, sizeof(state));
    if (!fs || !fs->mounted || !fs->bio || !report) return BFS_ERR_INVAL;
    if (repair && fs->read_only) return BFS_ERR_UNSUPPORTED;

    state.fs = fs;
    state.block_count = fs->bio->block_count;
    state.block_map = calloc(state.block_count, 1);
    state.reference_map = calloc(state.block_count, 1);
    if (fs->data_checksums) state.data_buffer = malloc(fs->bio->block_size);
    if (!state.block_map || !state.reference_map || (fs->data_checksums && !state.data_buffer)) {
        free(state.data_buffer);
        free(state.reference_map);
        free(state.block_map);
        return BFS_ERR_NOMEM;
    }

    scan(&state);
    for (uint32_t block = 0; block < state.block_count; block++) {
        if ((state.block_map[block] & 0x7fu) == 0) state.report.leaked_blocks++;
    }
    if (state.report.leaked_blocks) check_warning(&state);

    bfs_err_t result = state.report.errors ? BFS_ERR_CORRUPT : BFS_OK;
    if (repair && result == BFS_OK) {
        for (uint32_t block = 0; block < state.block_count; block++) {
            if ((state.block_map[block] & 0x7fu) != 0) continue;
            result = bfs_freespace_free(&fs->freespace, block, 1);
            if (result != BFS_OK) break;
            state.report.repaired_blocks++;
        }
        if (result == BFS_OK && state.report.repaired_blocks)
            result = bfs_fs_sync(fs);
    }

    *report = state.report;
    free(state.data_buffer);
    free(state.reference_map);
    free(state.block_map);
    return result;
}
