/* SPDX-License-Identifier: MPL-2.0 */
/* Canonical POSIX administration command for BFS images. */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bfs_fs.h"
#include "bfs_host_commands.h"
#include "bfs_host_common.h"
#include "bfs_snapshot.h"

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
    char *legacy_argv[] = { "mkbfs", (char *)image, (char *)block_size,
                            (char *)label, NULL };
    return bfs_host_mkbfs_main(4, legacy_argv);
}

static int check_command(int argc, char **argv)
{
    if (argc == 3) {
        char *legacy_argv[] = { "bfsfsck", argv[2], NULL };
        return bfs_host_fsck_main(2, legacy_argv);
    }
    if (argc == 4 && strcmp(argv[3], "--repair") == 0) {
        char *legacy_argv[] = { "bfsfsck", argv[2], "--fix", NULL };
        return bfs_host_fsck_main(3, legacy_argv);
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
    if (argc < 4) {
        usage();
        return 2;
    }
    char sibling[PATH_MAX];
    char *fuse_argv[argc + 2];
    int out = 0;
    const char *fuse_program = "bfs-fuse";
    const char *slash = strrchr(argv[0], '/');
    if (slash) {
        size_t directory_length = (size_t)(slash - argv[0]) + 1u;
        if (directory_length + sizeof("bfs-fuse") <= sizeof(sibling)) {
            memcpy(sibling, argv[0], directory_length);
            memcpy(sibling + directory_length, "bfs-fuse", sizeof("bfs-fuse"));
            fuse_program = sibling;
        }
    }
    fuse_argv[out++] = (char *)fuse_program;
    fuse_argv[out++] = "--image";
    fuse_argv[out++] = argv[2];
    for (int index = 4; index < argc; index++) fuse_argv[out++] = argv[index];
    fuse_argv[out++] = argv[3];
    fuse_argv[out] = NULL;
    if (fuse_program == sibling) execv(fuse_program, fuse_argv);
    else execvp(fuse_program, fuse_argv);
    perror("bfs mount: cannot start bfs-fuse");
    return 127;
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
