/* SPDX-License-Identifier: MPL-2.0 */

#include "test_harness.h"
#include "bfs_posix_bio.h"
#include "posix_bio_faults.h"

#include <fcntl.h>
#include <unistd.h>

#define BLOCK_SIZE 4096u
#define IMAGE_SIZE (2u * BLOCK_SIZE)

static void make_image(char path[])
{
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT_EQ(ftruncate(fd, IMAGE_SIZE), 0);
    uint8_t block[BLOCK_SIZE];
    memset(block, 0x5a, sizeof(block));
    TEST_ASSERT_EQ(pwrite(fd, block, sizeof(block), 0), (ssize_t)sizeof(block));
    TEST_ASSERT_EQ(close(fd), 0);
}

static bfs_bio_t *open_image(const char *path, bool writable)
{
    bfs_posix_bio_options_t options = {
        .block_size = BLOCK_SIZE,
        .writable = writable,
        .lock = true,
    };
    return bfs_posix_bio_open(path, &options);
}

static void test_retries_eintr_and_partial_transfers(void)
{
    char path[] = "/tmp/bfs-posix-faults.XXXXXX";
    make_image(path);
    bfs_posix_faults_reset();
    bfs_bio_t *bio = open_image(path, true);
    TEST_ASSERT(bio != NULL);

    uint8_t block[BLOCK_SIZE];
    bfs_posix_faults.pread_eintr = 1;
    bfs_posix_faults.pread_partial = true;
    TEST_ASSERT_EQ(bfs_bio_read(bio, 0, block), BFS_OK);
    TEST_ASSERT_EQ(bfs_posix_faults.pread_calls, 3);

    bfs_posix_faults.pwrite_eintr = 1;
    bfs_posix_faults.pwrite_partial = true;
    TEST_ASSERT_EQ(bfs_bio_write(bio, 1, block), BFS_OK);
    TEST_ASSERT_EQ(bfs_posix_faults.pwrite_calls, 3);

    bfs_posix_faults.fsync_eintr = 1;
    TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_posix_faults.fsync_calls, 2);
    bfs_bio_close(bio);
    TEST_ASSERT_EQ(bfs_posix_faults.close_calls, 1);
    unlink(path);
}

static void test_propagates_io_errors(void)
{
    char path[] = "/tmp/bfs-posix-faults.XXXXXX";
    make_image(path);
    uint8_t block[BLOCK_SIZE];

    bfs_posix_faults_reset();
    bfs_bio_t *bio = open_image(path, false);
    TEST_ASSERT(bio != NULL);
    bfs_posix_faults.pread_error = true;
    TEST_ASSERT_EQ(bfs_bio_read(bio, 0, block), BFS_ERR_IO);
    bfs_bio_close(bio);

    bfs_posix_faults_reset();
    bio = open_image(path, true);
    TEST_ASSERT(bio != NULL);
    bfs_posix_faults.pwrite_error = true;
    TEST_ASSERT_EQ(bfs_bio_write(bio, 0, block), BFS_ERR_IO);
    bfs_posix_faults.fsync_error = true;
    TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_ERR_IO);
    bfs_bio_close(bio);
    unlink(path);
}

static void test_rejects_geometry_and_allocation_failures(void)
{
    char path[] = "/tmp/bfs-posix-faults.XXXXXX";
    make_image(path);

    bfs_posix_faults_reset();
    bfs_posix_faults.fstat_error = true;
    TEST_ASSERT(open_image(path, false) == NULL);
    TEST_ASSERT_EQ(bfs_posix_faults.close_calls, 1);

    bfs_posix_faults_reset();
    bfs_posix_faults.calloc_error = true;
    TEST_ASSERT(open_image(path, false) == NULL);
    TEST_ASSERT_EQ(bfs_posix_faults.close_calls, 1);

    bfs_posix_faults_reset();
    bfs_posix_bio_options_t options = {
        .byte_length = BLOCK_SIZE - 1,
        .block_size = BLOCK_SIZE,
    };
    TEST_ASSERT(bfs_posix_bio_open(path, &options) == NULL);
    unlink(path);
}

TEST_SUITE_BEGIN("POSIX Transport Fault Probes")
    TEST_RUN(test_retries_eintr_and_partial_transfers);
    TEST_RUN(test_propagates_io_errors);
    TEST_RUN(test_rejects_geometry_and_allocation_failures);
TEST_SUITE_END()
