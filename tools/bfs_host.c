/* SPDX-License-Identifier: MPL-2.0 */
/* Canonical POSIX administration command for BFS images. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bfs_fs.h"
#include "bfs_host_common.h"
#include "bfs_snapshot.h"

#ifdef BFS_FUSE_ENABLED
int bfs_fuse_mount_main(int argc, char **argv);
#endif

static void usage(void)
{
    fprintf(stderr,
            "Usage:\n"
            "  bfs format IMAGE --label LABEL [--block-size BYTES]\n"
            "  bfs check IMAGE [--repair]\n"
            "  bfs snapshot create IMAGE NAME\n"
            "  bfs snapshot delete IMAGE NAME\n"
            "  bfs snapshot list IMAGE\n"
            "  bfs info IMAGE\n"
            "  bfs mount IMAGE MOUNTPOINT [--read-write] [--offset BYTES] "
            "[--length BYTES] [--snapshot NAME | --snapshot-id ID]\n");
}

static void report_open_error(const char *image, bfs_err_t error, const char *diagnostic)
{
    if (error == BFS_ERR_UNSUPPORTED && diagnostic[0])
        fprintf(stderr, "%s\n", diagnostic);
    else
        fprintf(stderr, "Cannot open %s (BFS error %d)\n", image, error);
}

static int format_command(int argc, char **argv)
{
    const char *image;
    const char *label = NULL;
    const char *block_size = "4096";
    bool block_size_seen = false;
    uint32_t block_size_value;

    if (argc < 5) {
        usage();
        return 2;
    }
    image = argv[2];
    for (int index = 3; index < argc; index += 2) {
        if (index + 1 >= argc ||
            (strcmp(argv[index], "--label") != 0 &&
             strcmp(argv[index], "--block-size") != 0)) {
            usage();
            return 2;
        }
        if (strcmp(argv[index], "--label") == 0) {
            if (label) {
                usage();
                return 2;
            }
            label = argv[index + 1];
        } else {
            if (block_size_seen) {
                usage();
                return 2;
            }
            block_size = argv[index + 1];
            block_size_seen = true;
        }
    }
    if (!label || !*label) {
        usage();
        return 2;
    }
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(block_size, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX) {
        fprintf(stderr, "Invalid block size: %s\n", block_size);
        return 2;
    }
    block_size_value = (uint32_t)parsed;
    return bfs_host_format_image(image, block_size_value, label);
}

static int check_command(int argc, char **argv)
{
    if (argc == 3) {
        return bfs_host_check_image(argv[2], false);
    }
    if (argc == 4 && strcmp(argv[3], "--repair") == 0) {
        return bfs_host_check_image(argv[2], true);
    }
    usage();
    return 2;
}

typedef struct {
    uint32_t count;
} snapshot_list_t;

static bool print_snapshot(uint32_t id, const bfs_snapshot_record_t *record, void *opaque)
{
    snapshot_list_t *list = (snapshot_list_t *)opaque;
    printf("%u %.*s\n", id, BFS_SNAPSHOT_NAME_MAX, (const char *)record->name);
    list->count++;
    return true;
}

static int snapshot_command(int argc, char **argv)
{
    const char *operation;
    const char *image;
    const char *name = NULL;
    bool writable;
    bfs_err_t open_error, error;
    char diagnostic[BFS_FORMAT_ERROR_MAX] = {0};
    bfs_bio_t *bio;
    bfs_fs_t fs;

    if (argc < 4) {
        usage();
        return 2;
    }
    operation = argv[2];
    image = argv[3];
    if (strcmp(operation, "list") == 0) {
        if (argc != 4) {
            usage();
            return 2;
        }
        writable = false;
    } else if (strcmp(operation, "create") == 0 || strcmp(operation, "delete") == 0) {
        if (argc != 5 || !argv[4][0]) {
            usage();
            return 2;
        }
        name = argv[4];
        writable = true;
    } else {
        usage();
        return 2;
    }

    bio = bfs_host_open_bfs_image(image, writable, &open_error, diagnostic);
    if (!bio) {
        report_open_error(image, open_error, diagnostic);
        return 1;
    }
    error = writable ? bfs_fs_mount(&fs, bio) : bfs_fs_mount_readonly(&fs, bio);
    if (error != BFS_OK) {
        fprintf(stderr, "Cannot mount %s (BFS error %d)\n", image, error);
        bfs_bio_close(bio);
        return 1;
    }

    if (strcmp(operation, "create") == 0)
        error = bfs_snapshot_create(&fs, name);
    else if (strcmp(operation, "delete") == 0) {
        uint32_t id;
        error = bfs_snapshot_find_by_name(&fs, name, &id, NULL);
        if (error == BFS_OK) error = bfs_snapshot_delete(&fs, id);
    }
    else {
        snapshot_list_t list = {0};
        error = bfs_snapshot_list(&fs, print_snapshot, &list);
        if (error == BFS_OK && list.count == 0) printf("No snapshots.\n");
    }
    if (error != BFS_OK) {
        fprintf(stderr, "Snapshot %s failed (BFS error %d)\n", operation, error);
        bfs_fs_abandon(&fs);
        bfs_bio_close(bio);
        return 1;
    }
    error = bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    if (error != BFS_OK) {
        fprintf(stderr, "Cannot close %s cleanly (BFS error %d)\n", image, error);
        return 1;
    }
    return 0;
}

static int info_command(int argc, char **argv)
{
    bfs_err_t open_error, error;
    char diagnostic[BFS_FORMAT_ERROR_MAX] = {0};
    bfs_bio_t *bio;
    bfs_fs_t fs;

    if (argc != 3) {
        usage();
        return 2;
    }
    bio = bfs_host_open_bfs_image(argv[2], false, &open_error, diagnostic);
    if (!bio) {
        report_open_error(argv[2], open_error, diagnostic);
        return 1;
    }
    error = bfs_fs_mount_readonly(&fs, bio);
    if (error == BFS_OK) {
        printf("Volume: %s\nBlocks: %u\nBlock size: %u\nFree blocks: %u\n",
               fs.txn.sb.volname, bio->block_count, bio->block_size, fs.freespace.total_free);
        error = bfs_fs_unmount(&fs);
    }
    bfs_bio_close(bio);
    if (error != BFS_OK) {
        fprintf(stderr, "Cannot inspect %s (BFS error %d)\n", argv[2], error);
        return 1;
    }
    return 0;
}

static int mount_command(int argc, char **argv)
{
#ifdef BFS_FUSE_ENABLED
    return bfs_fuse_mount_main(argc, argv);
#else
    (void)argv;
    if (argc < 4) {
        usage();
        return 2;
    }
    fprintf(stderr, "bfs mount requires a Linux build with libfuse3 development files\n");
    return 1;
#endif
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    if (strcmp(argv[1], "format") == 0) return format_command(argc, argv);
    if (strcmp(argv[1], "check") == 0) return check_command(argc, argv);
    if (strcmp(argv[1], "snapshot") == 0) return snapshot_command(argc, argv);
    if (strcmp(argv[1], "info") == 0) return info_command(argc, argv);
    if (strcmp(argv[1], "mount") == 0) return mount_command(argc, argv);
    usage();
    return 2;
}
