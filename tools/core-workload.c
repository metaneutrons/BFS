/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — host replica of the checked AmigaDOS compare workload.
 *
 * Drives the fs-compare-bench phases through the core API with the calls
 * the Amiga handler issues (30 cache slots, node write retention, deferred
 * writes of transaction-owned nodes). Mode
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
#define CACHE_SLOTS 30u

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

/* One listed entry: the handler reads its inode for the FileInfoBlock or
 * ExAllData and checks the comment flag. */
typedef struct {
    bfs_fs_t *fs;
    uint32_t count;
    bool stop_after_one;
    char name[BFS_NAME_MAX];
    uint8_t name_len;
} list_ctx_t;

static bool list_entry(const char *name, uint8_t name_len, uint32_t ino,
                       uint32_t type, void *context)
{
    (void)type;
    list_ctx_t *list = context;
    if (name_len == 2 && name[0] == '.' && name[1] == '.') return true;
    bfs_inode_t inode;
    require(bfs_inode_read(&list->fs->inode_tree, ino, &inode), "list-inode");
    if (bfs_be32(inode.flags) & BFS_INODE_FLAG_HAS_COMMENT) require(BFS_ERR_CORRUPT, "list-comment");
    memcpy(list->name, name, name_len);
    list->name_len = name_len;
    list->count++;
    return !list->stop_after_one;
}

/* ExNext: one entry per call, continuing after the name returned last. */
static void list_exnext(bfs_fs_t *fs, uint32_t dir)
{
    for (unsigned pass = 0; pass < 10; pass++) {
        list_ctx_t list = { .fs = fs, .stop_after_one = true };
        for (;;) {
            uint32_t before = list.count;
            if (before == 0)
                require(bfs_dir_scan(&fs->dir_tree, dir, list_entry, &list), "exnext-first");
            else
                require(bfs_dir_scan_after(&fs->dir_tree, dir, list.name, list.name_len,
                                           list_entry, &list), "exnext");
            if (list.count == before) break;
        }
        if (list.count != SMALL_COUNT) require(BFS_ERR_CORRUPT, "exnext-count");
    }
}

/* ExAll: one pass over the directory per listing. */
static void list_exall(bfs_fs_t *fs, uint32_t dir)
{
    for (unsigned pass = 0; pass < 10; pass++) {
        list_ctx_t list = { .fs = fs };
        require(bfs_dir_scan(&fs->dir_tree, dir, list_entry, &list), "exall");
        if (list.count != SMALL_COUNT) require(BFS_ERR_CORRUPT, "exall-count");
    }
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
    require(bfs_cache_init(&cache, device, CACHE_SLOTS), "cache");
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, CACHE_SLOTS / 2);
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
    list_exnext(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("LIST_EXNEXT_400");
    list_exall(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("LIST_EXALL_400");
    uint32_t big = write_large_file(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("SEQ_WRITE_8M");
    read_large_file(&fs, big);
    CALLGRIND_DUMP_STATS_AT("SEQ_READ_8M");
    delete_small_files(&fs, dir);
    CALLGRIND_DUMP_STATS_AT("SMALL_DELETE_40");

    require(bfs_fs_unmount(&fs), "unmount");
    bfs_cache_destroy(&cache);
    bfs_bio_close(device);
    (void)unlink(image);
    printf("COMPILER\t%s\n", __VERSION__);
    printf("PASS\t%s\n", group_commit ? "group" : "sync");
    return 0;
}
