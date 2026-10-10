/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — host replica of the checked AmigaDOS compare workload.
 *
 * Drives the fs-compare-bench phases through the core API with the calls
 * the Amiga handler issues (its cache slots for Buffers = 30, node write
 * retention, deferred writes of transaction-owned nodes). Mode
 * "sync" commits after every close and delete, as the handler does today;
 * mode "group" commits once at the end of each mutating phase. Under
 * valgrind --tool=callgrind every phase is dumped separately, which gives a
 * deterministic per-phase instruction count. See tools/core-workload-profile.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#endif
#endif
#ifndef CALLGRIND_DUMP_STATS_AT
#define CALLGRIND_ZERO_STATS do { } while (0)
#define CALLGRIND_DUMP_STATS_AT(name) do { (void)(name); } while (0)
#endif

#include "bfs_cache.h"
#include "bfs_dir.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_inode.h"
#include "block_device_emu.h"

#define SMALL_COUNT 40u
#define SMALL_BYTES 1024u
#define BUFFER_BYTES 65536u
#define LARGE_BYTES (8u * 1024u * 1024u)
#define BLOCK_SIZE 4096u
#define BLOCK_COUNT 65408u
/* The handler's slots for Buffers = 30 (bfs_cache_mount_slots). */
#define MOUNT_BUFFERS 30u
#define LIST_COUNT 1000u
/* fs-compare-bench lists with ED_COMMENT into 4096 bytes: 44 bytes per entry
 * named fNNN, so 93 entries per ExAll call. */
#define EXALL_ENTRIES_PER_CALL 93u

static bool group_commit;
static uint8_t expected[BUFFER_BYTES];
static uint8_t received[BUFFER_BYTES];

static void sample_stamp(void *context, bfs_inode_stamp_t *stamp)
{
    (void)context;
    stamp->days = 1;
    stamp->mins = 2;
    stamp->ticks = 3;
}

static void require(bfs_err_t err, const char *what)
{
    if (err == BFS_OK) return;
    fprintf(stderr, "FAIL\t%s\t%d\n", what, (int)err);
    exit(1);
}

static void small_name(char *name, size_t capacity, unsigned index)
{
    if (snprintf(name, capacity, "f%03u", index) != 4) {
        fprintf(stderr, "FAIL\tname\n");
        exit(1);
    }
}

static void commit_operation(bfs_fs_t *fs)
{
    if (!group_commit) require(bfs_fs_sync(fs), "sync");
}

static void commit_phase(bfs_fs_t *fs)
{
    if (group_commit) require(bfs_fs_sync(fs), "sync");
}

static void create_small_files(bfs_fs_t *fs, uint32_t dir)
{
    char name[16];
    for (unsigned i = 0; i < SMALL_COUNT; i++) {
        uint32_t ino, type;
        bfs_file_t file;
        small_name(name, sizeof(name), i);
        if (bfs_dir_lookup(&fs->dir_tree, dir, name, 4, &ino, &type) != BFS_ERR_NOTFOUND)
            require(BFS_ERR_EXISTS, "create-lookup");
        require(bfs_fs_reserve(fs, 5), "reserve");
        require(bfs_fs_create_file_with_stamp(fs, dir, name, 4, sample_stamp, NULL, &ino),
                "create");
        require(bfs_file_open(&file, fs, ino), "open");
        if (bfs_file_write_with_stamp(&file, expected, SMALL_BYTES, sample_stamp, NULL, 0) !=
            (int32_t)SMALL_BYTES)
            require(BFS_ERR_IO, "small-write");
        commit_operation(fs);
    }
    commit_phase(fs);
}

static void lookup_small_files(bfs_fs_t *fs)
{
    char name[16];
    for (unsigned pass = 0; pass < 10; pass++) {
        for (unsigned i = 0; i < SMALL_COUNT; i++) {
            uint32_t dir, ino, type;
            require(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "perf", 4, &dir, &type),
                    "lookup-dir");
            small_name(name, sizeof(name), i);
            require(bfs_dir_lookup(&fs->dir_tree, dir, name, 4, &ino, &type), "lookup");
        }
    }
}

static void read_small_files(bfs_fs_t *fs, uint32_t dir)
{
    char name[16];
    for (unsigned i = 0; i < SMALL_COUNT; i++) {
        uint32_t ino, type;
        bfs_file_t file;
        small_name(name, sizeof(name), i);
        require(bfs_dir_lookup(&fs->dir_tree, dir, name, 4, &ino, &type), "read-lookup");
        require(bfs_file_open(&file, fs, ino), "read-open");
        if (bfs_file_read(&file, received, SMALL_BYTES) != (int32_t)SMALL_BYTES ||
            memcmp(received, expected, SMALL_BYTES) != 0 ||
            bfs_file_read(&file, received, 1) != 0)
            require(BFS_ERR_CORRUPT, "small-read");
    }
}

/* ExNext as the handler runs it: one entry per call, served from a batch that
 * one scan gathered. The scan stops on the last entry it gathers, so the next
 * one resumes after it; the batch doubles up to 32 while it is used up. The
 * batch's inodes are read in one ascending batch, and an entry uses its copy
 * while the inode tree and its pending inodes are unchanged. */
#define EXNEXT_BATCH_MAX 32u
#define EXNEXT_BATCH_FIRST 8u

typedef struct {
    uint32_t ino[EXNEXT_BATCH_MAX];
    bfs_dir_pos_t pos[EXNEXT_BATCH_MAX];
    uint32_t count, next, limit;
    bool stopped_at_last;
    uint32_t sorted[EXNEXT_BATCH_MAX];
    uint8_t read_index[EXNEXT_BATCH_MAX];
    bfs_inode_t inodes[EXNEXT_BATCH_MAX];
    bfs_err_t results[EXNEXT_BATCH_MAX];
    bfs_blk_t inode_root;
    uint32_t inode_generation, pending_version;
} exnext_batch_t;

static void exnext_read_inodes(bfs_fs_t *fs, exnext_batch_t *b)
{
    uint8_t order[EXNEXT_BATCH_MAX];
    for (uint32_t i = 0; i < b->count; i++) {
        uint32_t at = i;
        while (at > 0 && b->ino[order[at - 1]] > b->ino[i]) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = (uint8_t)i;
    }
    for (uint32_t k = 0; k < b->count; k++) {
        b->sorted[k] = b->ino[order[k]];
        b->read_index[order[k]] = (uint8_t)k;
    }
    b->inode_root = fs->inode_tree.root;
    b->inode_generation = fs->inode_tree.generation;
    b->pending_version = bfs_inode_pending_version(&fs->inode_tree);
    require(bfs_inode_read_sorted(&fs->inode_tree, b->sorted, b->count, b->inodes,
                                  b->results), "exnext-inodes");
}

static bool exnext_gather(const char *name, uint8_t name_len, uint32_t ino,
                          uint32_t type, const bfs_dir_pos_t *pos, void *context)
{
    (void)name;
    (void)name_len;
    (void)type;
    exnext_batch_t *b = context;
    b->ino[b->count] = ino;
    b->pos[b->count] = *pos;
    if (++b->count == b->limit) {
        b->stopped_at_last = true;
        return false;
    }
    return true;
}

static void list_exnext(bfs_fs_t *fs, uint32_t dir, uint32_t expected_count)
{
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    for (unsigned pass = 0; pass < 10; pass++) {
        exnext_batch_t b = { .limit = EXNEXT_BATCH_FIRST };
        bfs_dir_pos_t last = {0, 0};
        bool consumed_stop = false;
        uint32_t count = 0;
        for (;;) {
            if (b.next == b.count) {
                if (b.count > 0 && b.limit < EXNEXT_BATCH_MAX) b.limit *= 2;
                b.count = b.next = 0;
                b.stopped_at_last = false;
                bfs_err_t err = BFS_ERR_AGAIN;
                if (count && consumed_stop)
                    err = bfs_dir_scan_resume_pos(&fs->dir_tree, &cursor, dir, exnext_gather, &b);
                if (err == BFS_ERR_AGAIN)
                    err = bfs_dir_scan_cursor_pos(&fs->dir_tree, &cursor, dir,
                                                  count ? &last : NULL, exnext_gather, &b);
                require(err, "exnext");
                if (b.count == 0) break;
                exnext_read_inodes(fs, &b);
            }
            uint32_t i = b.next++;
            uint8_t k = b.read_index[i];
            bfs_inode_t inode;
            if (b.results[k] == BFS_OK && b.inode_root == fs->inode_tree.root &&
                b.inode_generation == fs->inode_tree.generation &&
                b.pending_version == bfs_inode_pending_version(&fs->inode_tree))
                inode = b.inodes[k];
            else
                require(bfs_inode_read(&fs->inode_tree, b.ino[i], &inode), "list-inode");
            if (bfs_be32(inode.flags) & BFS_INODE_FLAG_HAS_COMMENT) require(BFS_ERR_CORRUPT, "list-comment");
            last = b.pos[i];
            consumed_stop = b.next == b.count && b.stopped_at_last;
            count++;
        }
        if (count != expected_count) require(BFS_ERR_CORRUPT, "exnext-count");
    }
    bfs_btree_cursor_release(&cursor);
}

/* The handler's ExAll batch: entries gathered in directory order. */
#define EXALL_BATCH 64u

typedef struct {
    uint32_t inos[EXALL_BATCH];
    bfs_dir_pos_t pos[EXALL_BATCH];
    uint32_t count;
    uint32_t limit;
    bool stopped;
} gather_t;

static bool gather_entry(const char *name, uint8_t name_len, uint32_t ino,
                         uint32_t type, const bfs_dir_pos_t *pos, void *context)
{
    (void)name;
    (void)name_len;
    (void)type;
    gather_t *gather = context;
    if (gather->count == gather->limit) {
        gather->stopped = true;
        return false;
    }
    gather->inos[gather->count] = ino;
    gather->pos[gather->count] = *pos;
    gather->count++;
    return true;
}

static int compare_ino(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* ExAll as the handler runs it: each call fills one buffer of 93 entries in
 * batches of up to 64, reading each batch's inodes in ascending order, and
 * continues after the position consumed last. */
static void list_exall(bfs_fs_t *fs, uint32_t dir, uint32_t expected_count)
{
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    for (unsigned pass = 0; pass < 10; pass++) {
        uint32_t count = 0;
        bfs_dir_pos_t last = {0, 0};
        bool call_full;
        do {
            uint32_t room = EXALL_ENTRIES_PER_CALL;
            bool batch_full;
            call_full = false;
            do {
                gather_t gather = { .limit = room < EXALL_BATCH ? room : EXALL_BATCH };
                require(bfs_dir_scan_cursor_pos(&fs->dir_tree, &cursor, dir,
                                                count ? &last : NULL, gather_entry, &gather),
                        "exall");
                uint32_t sorted[EXALL_BATCH];
                bfs_inode_t inodes[EXALL_BATCH];
                bfs_err_t results[EXALL_BATCH];
                memcpy(sorted, gather.inos, gather.count * sizeof(sorted[0])); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
                qsort(sorted, gather.count, sizeof(sorted[0]), compare_ino);
                require(bfs_inode_read_sorted(&fs->inode_tree, sorted, gather.count,
                                              inodes, results), "exall-inodes");
                for (uint32_t i = 0; i < gather.count; i++) {
                    require(results[i], "list-inode");
                    if (bfs_be32(inodes[i].flags) & BFS_INODE_FLAG_HAS_COMMENT)
                        require(BFS_ERR_CORRUPT, "list-comment");
                }
                if (gather.count) last = gather.pos[gather.count - 1];
                count += gather.count;
                room -= gather.count;
                batch_full = gather.stopped && gather.count == EXALL_BATCH;
                call_full = gather.stopped && room == 0;
            } while (batch_full && room > 0);
        } while (call_full);
        if (count != expected_count) require(BFS_ERR_CORRUPT, "exall-count");
    }
    bfs_btree_cursor_release(&cursor);
}

/* fs-compare-bench creates its listing directory with empty files. */
static void create_listing(bfs_fs_t *fs, uint32_t dir)
{
    char name[16];
    for (unsigned i = 0; i < LIST_COUNT; i++) {
        uint32_t ino;
        bfs_file_t file;
        small_name(name, sizeof(name), i);
        require(bfs_fs_reserve(fs, 5), "reserve");
        require(bfs_fs_create_file_with_stamp(fs, dir, name, 4, sample_stamp, NULL, &ino),
                "list-create");
        require(bfs_file_open(&file, fs, ino), "list-open");
        commit_operation(fs);
    }
    commit_phase(fs);
}

static uint32_t write_large_file(bfs_fs_t *fs, uint32_t dir)
{
    uint32_t ino;
    bfs_file_t file;
    require(bfs_fs_reserve(fs, 5), "reserve");
    require(bfs_fs_create_file_with_stamp(fs, dir, "big", 3, sample_stamp, NULL, &ino),
            "create-big");
    require(bfs_file_open(&file, fs, ino), "open-big");
    for (uint32_t offset = 0; offset < LARGE_BYTES; offset += BUFFER_BYTES) {
        if (bfs_file_write_with_stamp(&file, expected, BUFFER_BYTES, sample_stamp, NULL, 0) !=
            (int32_t)BUFFER_BYTES)
            require(BFS_ERR_IO, "large-write");
    }
    commit_operation(fs);
    commit_phase(fs);
    return ino;
}

static void read_large_file(bfs_fs_t *fs, uint32_t ino)
{
    bfs_file_t file;
    require(bfs_file_open(&file, fs, ino), "open-big-read");
    for (uint32_t offset = 0; offset < LARGE_BYTES; offset += BUFFER_BYTES) {
        if (bfs_file_read(&file, received, BUFFER_BYTES) != (int32_t)BUFFER_BYTES ||
            memcmp(received, expected, BUFFER_BYTES) != 0)
            require(BFS_ERR_CORRUPT, "large-read");
    }
    if (bfs_file_read(&file, received, 1) != 0) require(BFS_ERR_CORRUPT, "large-eof");
}

static void delete_small_files(bfs_fs_t *fs, uint32_t dir)
{
    char name[16];
    for (unsigned i = 0; i < SMALL_COUNT; i++) {
        small_name(name, sizeof(name), i);
        require(bfs_fs_delete_file(fs, dir, name, 4), "delete");
        commit_operation(fs);
    }
    commit_phase(fs);
}

static uint32_t append_file(bfs_fs_t *fs, uint32_t dir, const char *name,
                            uint32_t step, uint32_t total)
{
    uint32_t ino;
    bfs_file_t file;
    require(bfs_fs_reserve(fs, 5), "reserve");
    /* name is one of the workload's short string literals. */
    require(bfs_fs_create_file_with_stamp(fs, dir, name, (uint8_t)strlen(name), /* Flawfinder: ignore */
                                          sample_stamp, NULL, &ino), "create-append");
    require(bfs_file_open(&file, fs, ino), "open-append");
    for (uint32_t offset = 0; offset < total; offset += step) {
        if (bfs_file_write_with_stamp(&file, expected + offset % BUFFER_BYTES, step,
                                      sample_stamp, NULL, 0) != (int32_t)step)
            require(BFS_ERR_IO, "append-write");
    }
    commit_operation(fs);
    commit_phase(fs);
    return ino;
}

static void read_appended_file(bfs_fs_t *fs, uint32_t ino, uint32_t total)
{
    bfs_file_t file;
    require(bfs_file_open(&file, fs, ino), "open-append-read");
    for (uint32_t offset = 0; offset < total; offset += BUFFER_BYTES) {
        if (bfs_file_read(&file, received, BUFFER_BYTES) != (int32_t)BUFFER_BYTES ||
            memcmp(received, expected, BUFFER_BYTES) != 0)
            require(BFS_ERR_CORRUPT, "append-read");
    }
    if (bfs_file_read(&file, received, 1) != 0) require(BFS_ERR_CORRUPT, "append-eof");
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[2], "sync") != 0 && strcmp(argv[2], "group") != 0)) {
        fprintf(stderr, "Usage: core-workload IMAGE sync|group\n");
        return 2;
    }
    group_commit = strcmp(argv[2], "group") == 0;
    const char *image = argv[1];
    (void)unlink(image);
    bfs_bio_t *device = bio_emu_create(image, BLOCK_SIZE, BLOCK_COUNT);
    if (!device) {
        fprintf(stderr, "FAIL\timage\n");
        return 1;
    }
    for (uint32_t i = 0; i < BUFFER_BYTES; i++)
        expected[i] = (uint8_t)((i * 31u + 17u) & 0xffu);

    static bfs_cache_t cache;
    static bfs_fs_t fs;
    uint32_t dir;
    require(bfs_fs_format(device, "BFSTest", 0), "format");
    uint32_t slots = bfs_cache_mount_slots(MOUNT_BUFFERS, BLOCK_SIZE);
    require(bfs_cache_init(&cache, device, slots), "cache");
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, slots / 2);
    require(bfs_fs_mount(&fs, &cache.bio), "mount");
    require(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "perf", 4, &dir), "mkdir");
    require(bfs_fs_sync(&fs), "sync");
    CALLGRIND_ZERO_STATS;

    create_small_files(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("SMALL_CREATE_40");
    lookup_small_files(&fs);
    CALLGRIND_DUMP_STATS_AT("LOOKUP_400");
    read_small_files(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("SMALL_READ_40");
    list_exnext(&fs, dir, SMALL_COUNT);
    CALLGRIND_DUMP_STATS_AT("LIST_EXNEXT_400");
    list_exall(&fs, dir, SMALL_COUNT);
    CALLGRIND_DUMP_STATS_AT("LIST_EXALL_400");
    uint32_t big = write_large_file(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("SEQ_WRITE_8M");
    read_large_file(&fs, big);
    CALLGRIND_DUMP_STATS_AT("SEQ_READ_8M");
    delete_small_files(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("SMALL_DELETE_40");
    uint32_t append4k = append_file(&fs, dir, "append4k", 4096u, 1024u * 1024u);
    CALLGRIND_DUMP_STATS_AT("APPEND_4K_1M");
    uint32_t append1k = append_file(&fs, dir, "append1k", 1024u, 256u * 1024u);
    CALLGRIND_DUMP_STATS_AT("APPEND_1K_256K");
    read_appended_file(&fs, append4k, 1024u * 1024u);
    read_appended_file(&fs, append1k, 256u * 1024u);
    CALLGRIND_DUMP_STATS_AT("APPEND_READ_1280K");
    uint32_t listing;
    require(bfs_fs_mkdir(&fs, dir, "list", 4, &listing), "mkdir-list");
    create_listing(&fs, listing);
    CALLGRIND_DUMP_STATS_AT("LIST_CREATE_1000");
    list_exnext(&fs, listing, LIST_COUNT);
    CALLGRIND_DUMP_STATS_AT("LIST_EXNEXT_10000");
    list_exall(&fs, listing, LIST_COUNT);
    CALLGRIND_DUMP_STATS_AT("LIST_EXALL_10000");

    require(bfs_fs_unmount(&fs), "unmount");
    bfs_cache_destroy(&cache);
    bfs_bio_close(device);
    (void)unlink(image);
    printf("COMPILER\t%s\n", __VERSION__);
    printf("PASS\t%s\n", group_commit ? "group" : "sync");
    return 0;
}
