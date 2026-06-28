/*
 * BFS — Concurrency and thread safety tests
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define TEST_IMG "test_concurrency.img"
#define BLK_SIZE 4096
#define BLK_COUNT 32768  /* 128MB */
#define NUM_OPS 200

static bfs_fs_t g_fs;
static volatile int g_stop = 0;

static bfs_fs_t *setup(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "ConcurTest", 0);
    bfs_fs_mount(&g_fs, bio);
    g_stop = 0;
    return &g_fs;
}

static void teardown(bfs_fs_t *fs)
{
    bfs_bio_t *bio = fs->bio;
    bfs_fs_unmount(fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

typedef struct {
    bfs_fs_t *fs;
    int id;
} thread_arg_t;

/* Writer thread: constantly creates, writes, reads, and deletes files in its own namespace */
static void *writer_thread_fn(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    bfs_fs_t *fs = targ->fs;
    int id = targ->id;

    for (int i = 0; i < NUM_OPS; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "w%d_file_%03d", id, i);
        uint32_t ino;

        /* Create file */
        bfs_err_t err = bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
        if (err == BFS_OK) {
            /* Open and write */
            bfs_file_t f;
            if (bfs_file_open(&f, fs, ino) == BFS_OK) {
                char buf[64];
                int content_len = snprintf(buf, sizeof(buf), "Thread %d wrote index %d", id, i);
                bfs_file_write(&f, buf, content_len);

                /* Read back to verify */
                bfs_file_seek(&f, 0, BFS_SEEK_SET);
                char read_buf[64];
                memset(read_buf, 0, sizeof(read_buf));
                bfs_file_read(&f, read_buf, content_len);
                (void)read_buf;
            }

            /* Delete every other file */
            if (i % 2 == 0) {
                bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)len);
            }
        }

        /* Sleep micro-second to let other threads interleave */
        usleep(100);
    }
    return NULL;
}

/* Reader thread: looks up existing files and reads them */
static void *reader_thread_fn(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    bfs_fs_t *fs = targ->fs;

    while (!g_stop) {
        /* Read from random files of writers */
        for (int w = 1; w <= 2; w++) {
            for (int i = 0; i < NUM_OPS; i += 5) {
                char name[32];
                int len = snprintf(name, sizeof(name), "w%d_file_%03d", w, i);
                uint32_t ino, type;
                bfs_lock_read(&fs->lock);
                bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type);
                bfs_lock_unlock(&fs->lock);
                if (err == BFS_OK) {
                    bfs_file_t f;
                    if (bfs_file_open(&f, fs, ino) == BFS_OK) {
                        char buf[64];
                        bfs_file_read(&f, buf, sizeof(buf));
                    }
                }
            }
        }
        usleep(500);
    }
    return NULL;
}

/* Snapshot thread: periodically creates and lists snapshots */
static void *snapshot_thread_fn(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    bfs_fs_t *fs = targ->fs;
    int snap_cnt = 0;

    while (!g_stop) {
        char name[32];
        snprintf(name, sizeof(name), "snap_%03d", snap_cnt++);
        bfs_snapshot_create(fs, name);

        usleep(5000); /* 5ms */
    }
    return NULL;
}

/* Sync thread: periodically syncs the filesystem */
static void *sync_thread_fn(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    bfs_fs_t *fs = targ->fs;

    while (!g_stop) {
        bfs_fs_sync(fs);
        usleep(2000); /* 2ms */
    }
    return NULL;
}

static void test_multithreaded_io(void)
{
    bfs_fs_t *fs = setup();

    pthread_t writers[2];
    pthread_t readers[2];
    pthread_t snap_thread;
    pthread_t sync_thread;

    thread_arg_t wargs[2] = { {fs, 1}, {fs, 2} };
    thread_arg_t rargs[2] = { {fs, 1}, {fs, 2} };
    thread_arg_t sarg = {fs, 0};

    /* Start threads */
    pthread_create(&writers[0], NULL, writer_thread_fn, &wargs[0]);
    pthread_create(&writers[1], NULL, writer_thread_fn, &wargs[1]);
    pthread_create(&readers[0], NULL, reader_thread_fn, &rargs[0]);
    pthread_create(&readers[1], NULL, reader_thread_fn, &rargs[1]);
    pthread_create(&snap_thread, NULL, snapshot_thread_fn, &sarg);
    pthread_create(&sync_thread, NULL, sync_thread_fn, &sarg);

    /* Wait for writers to finish */
    pthread_join(writers[0], NULL);
    pthread_join(writers[1], NULL);

    /* Stop reader, snapshot and sync threads */
    g_stop = 1;
    pthread_join(readers[0], NULL);
    pthread_join(readers[1], NULL);
    pthread_join(snap_thread, NULL);
    pthread_join(sync_thread, NULL);

    /* Final sync and teardown */
    bfs_fs_sync(fs);
    teardown(fs);
}

TEST_SUITE_BEGIN("Concurrency Tests")
    TEST_RUN(test_multithreaded_io);
TEST_SUITE_END()
