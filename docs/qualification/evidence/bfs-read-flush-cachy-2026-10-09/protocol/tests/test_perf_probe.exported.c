/* SPDX-License-Identifier: MPL-2.0 */
/* Host oracles for the disposable Amiga profiler and its real core hooks. */

#include "test_harness.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_inode.h"
#include "block_device_emu.h"
#include "perf_probe.h"

#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <stdio.h>
#include <unistd.h>

#define TEST_IMG "test_perf_probe.img"
#define BLOCK_SIZE 4096u
#define BLOCK_COUNT 2048u
#define CACHE_SLOTS 30u

static struct Device fake_device;
static struct timerequest fake_timer_request;
static uint64_t fake_clock_ticks;

struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size)
{
    (void)port;
    if (size != sizeof(fake_timer_request)) return NULL;
    memset(&fake_timer_request, 0, sizeof(fake_timer_request));
    return (struct IORequest *)&fake_timer_request;
}

void DeleteIORequest(struct IORequest *request)
{
    (void)request;
}

LONG OpenDevice(const char *name, ULONG unit, struct IORequest *request,
                ULONG flags)
{
    if (!name || unit != UNIT_ECLOCK || !request || flags != 0) return -1;
    request->io_Device = &fake_device;
    return 0;
}

void CloseDevice(struct IORequest *request)
{
    (void)request;
}

ULONG ReadEClock(struct EClockVal *value)
{
    uint64_t ticks = fake_clock_ticks;
    value->ev_hi = (ULONG)(ticks >> 32);
    value->ev_lo = (ULONG)ticks;
    fake_clock_ticks += 5;
    return 1000000u;
}

static void start_probe(void)
{
    static struct MsgPort port;
    fake_clock_ticks = 0;
    bfs_perf_probe_init(&port);
    bfs_perf_probe_reset();
}

static void stop_probe(void)
{
    bfs_perf_probe_close();
}

static void test_detail_sampling_disabled_tree_and_reset(void)
{
    start_probe();
    TEST_ASSERT_EQ(bfs_perf_probe_counters.clock_hz, 1000000u);

    bfs_perf_detail_sample_t sample;
    for (uint32_t i = 0; i < BFS_PERF_DETAIL_SAMPLE_STRIDE - 1u; i++) {
        sample = bfs_perf_probe_detail_begin(
            BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_READ);
        TEST_ASSERT(!sample.sampled);
        bfs_perf_probe_detail_end(&sample);
    }
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_calls,
                   BFS_PERF_DETAIL_SAMPLE_STRIDE - 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_samples, 0u);

    sample = bfs_perf_probe_detail_begin(BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_READ);
    TEST_ASSERT(sample.sampled);
    bfs_perf_probe_detail_end(&sample);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_calls,
                   BFS_PERF_DETAIL_SAMPLE_STRIDE);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_samples, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_sample_ticks, 5u);

    bfs_fs_t fs = {0};
    bfs_perf_probe_fs = &fs;
    sample = bfs_perf_probe_detail_tree_begin(
        NULL, BFS_PERF_DETAIL_SCOPE_DETAIL_INODE_NODE_VIEW,
        BFS_PERF_DETAIL_SCOPE_DETAIL_DIR_NODE_VIEW);
    TEST_ASSERT(!sample.sampled);
    TEST_ASSERT_EQ(sample.scope, BFS_PERF_DETAIL_SCOPE_DISABLED);
    bfs_perf_probe_detail_end(&sample);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_node_view_calls, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_dir_node_view_calls, 0u);

    bfs_perf_probe_data_depth = 7;
    bfs_perf_probe_free_tree_phase = BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN;
    bfs_perf_probe_counters.bio_read_calls = 12;
    ULONG hz = bfs_perf_probe_counters.clock_hz;
    bfs_perf_probe_reset();
    TEST_ASSERT_EQ(bfs_perf_probe_counters.clock_hz, hz);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.bio_read_calls, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_calls, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_data_depth, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_free_tree_phase,
                   BFS_PERF_FREE_TREE_PHASE_OTHER);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.clock_pair_ticks, 5u);
    stop_probe();
}

static void test_inode_read_wrappers_count_errors(void)
{
    start_probe();
    bfs_inode_t inode = {0};
    for (uint32_t i = 0; i < BFS_PERF_DETAIL_SAMPLE_STRIDE; i++)
        TEST_ASSERT_EQ(bfs_inode_read(NULL, 1, &inode), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_read_calls,
                   BFS_PERF_DETAIL_SAMPLE_STRIDE);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_calls,
                   BFS_PERF_DETAIL_SAMPLE_STRIDE);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_samples, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_sample_ticks, 5u);

    bfs_perf_probe_reset();
    /* A valid empty tree must reach NOTFOUND, not the argument guard. */
    bfs_bio_t bio = { .block_size = BLOCK_SIZE, .block_count = BLOCK_COUNT };
    const bfs_btree_ops_t ops = {0};
    bfs_btree_t empty_tree = {
        .bio = &bio, .ops = &ops, .root = BFS_BLK_NULL
    };
    for (uint32_t i = 0; i < BFS_PERF_DETAIL_SAMPLE_STRIDE; i++)
        TEST_ASSERT_EQ(bfs_inode_read_unlinked(&empty_tree, 2, &inode),
                       BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_read_calls,
                   BFS_PERF_DETAIL_SAMPLE_STRIDE);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_calls,
                   BFS_PERF_DETAIL_SAMPLE_STRIDE);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_samples, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.detail_inode_read_sample_ticks, 5u);
    stop_probe();
}

static void test_direct_and_deferred_tree_attribution(void)
{
    start_probe();
    static const bfs_btree_ops_t free_layout = {0};
    static const bfs_btree_ops_t dir_layout = {0};
    static const bfs_btree_ops_t inode_layout = {0};
    static const bfs_btree_ops_t refcount_layout = {0};
    static const bfs_btree_ops_t unknown_layout = {0};
    bfs_fs_t fs = {0};
    fs.freespace.tree.ops = &free_layout;
    fs.dir_tree.tree.ops = &dir_layout;
    fs.inode_tree.ops = &inode_layout;
    fs.refcount.tree.ops = &refcount_layout;
    bfs_perf_probe_fs = &fs;

    bfs_perf_probe_free_tree_phase = BFS_PERF_FREE_TREE_PHASE_ALLOCATION_BODY;
    bfs_perf_probe_node_write(&free_layout, FALSE);
    bfs_perf_probe_free_tree_phase = BFS_PERF_FREE_TREE_PHASE_RESERVE_REFILL;
    bfs_perf_probe_node_write(&free_layout, TRUE);
    bfs_perf_probe_free_tree_phase = BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN;
    bfs_perf_probe_node_write(&free_layout, FALSE);
    bfs_perf_probe_free_tree_phase =
        BFS_PERF_FREE_TREE_PHASE_POST_PUBLISH_PENDING_RECLAIM;
    bfs_perf_probe_node_write(&free_layout, TRUE);
    bfs_perf_probe_free_tree_phase = BFS_PERF_FREE_TREE_PHASE_OTHER;
    bfs_perf_probe_node_write(&free_layout, FALSE);
    bfs_perf_probe_node_write(&dir_layout, FALSE);
    bfs_perf_probe_node_write(&dir_layout, TRUE);
    bfs_perf_probe_node_write(&inode_layout, FALSE);
    bfs_perf_probe_node_write(&inode_layout, TRUE);
    bfs_perf_probe_node_write(&refcount_layout, FALSE);
    bfs_perf_probe_node_write(&refcount_layout, TRUE);
    bfs_perf_probe_node_write(&unknown_layout, FALSE);
    bfs_perf_probe_node_write(&unknown_layout, TRUE);

    TEST_ASSERT_EQ(bfs_perf_probe_counters.btree_node_writes, 13u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.deferred_node_writes, 6u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.free_tree_node_writes, 5u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_node_writes, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_node_writes, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.refcount_tree_node_writes, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.other_tree_node_writes, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.free_tree_allocation_body_node_writes, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.free_tree_reserve_refill_node_writes, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.free_tree_reserve_return_node_writes, 1u);
    TEST_ASSERT_EQ(
        bfs_perf_probe_counters.free_tree_post_publish_pending_reclaim_node_writes,
        1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.free_tree_other_node_writes, 1u);
    TEST_ASSERT_EQ(
        bfs_perf_probe_counters.free_tree_allocation_body_node_writes +
        bfs_perf_probe_counters.free_tree_reserve_refill_node_writes +
        bfs_perf_probe_counters.free_tree_reserve_return_node_writes +
        bfs_perf_probe_counters.free_tree_post_publish_pending_reclaim_node_writes +
        bfs_perf_probe_counters.free_tree_other_node_writes,
        bfs_perf_probe_counters.free_tree_node_writes);
    stop_probe();
}

static void test_expected_level_node_view_accounting(void)
{
    start_probe();
    static const bfs_btree_ops_t dir_layout = {0};
    static const bfs_btree_ops_t inode_layout = {0};
    static const bfs_btree_ops_t other_layout = {0};
    bfs_fs_t fs = {0};
    fs.dir_tree.tree.ops = &dir_layout;
    fs.inode_tree.ops = &inode_layout;
    bfs_perf_probe_fs = &fs;

    /* Level is the traversal's expectation at the call site: 0 is a leaf;
     * all positive levels are internal, independent of resident status. */
    bfs_perf_probe_node_view_at_level(&fs.dir_tree.tree, FALSE, 0);
    bfs_perf_probe_node_view_at_level(&fs.dir_tree.tree, TRUE, 0);
    bfs_perf_probe_node_view_at_level(&fs.dir_tree.tree, TRUE, 3);
    bfs_perf_probe_node_view_at_level(&fs.inode_tree, TRUE, 0);
    bfs_perf_probe_node_view_at_level(&fs.inode_tree, FALSE, 7);

    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_node_views, 3u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_resident_views, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_leaf_node_views, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_leaf_resident_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_internal_node_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_internal_resident_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_node_views, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_resident_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_leaf_node_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_leaf_resident_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_internal_node_views, 1u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_internal_resident_views, 0u);

    /* An unrelated tree and a view without an enabled filesystem retain the
     * existing OTHER category and do not enter DIR/INODE level partitions. */
    bfs_btree_t other_tree = { .ops = &other_layout };
    bfs_perf_probe_node_view_at_level(&other_tree, TRUE, 0);
    bfs_perf_probe_fs = NULL;
    bfs_perf_probe_node_view_at_level(&fs.dir_tree.tree, TRUE, 0);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.other_tree_node_views, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.other_tree_resident_views, 2u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_node_views, 3u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_leaf_node_views, 2u);

    ULONG clock_hz = bfs_perf_probe_counters.clock_hz;
    bfs_perf_probe_reset();
    TEST_ASSERT_EQ(bfs_perf_probe_counters.clock_hz, clock_hz);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_resident_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_leaf_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_leaf_resident_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_internal_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.dir_tree_internal_resident_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_resident_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_leaf_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_leaf_resident_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_internal_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.inode_tree_internal_resident_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.other_tree_node_views, 0u);
    TEST_ASSERT_EQ(bfs_perf_probe_counters.other_tree_resident_views, 0u);
    stop_probe();
}

static void test_filesystem_cache_finalizes_deferred_nodes(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *device = bio_emu_create(TEST_IMG, BLOCK_SIZE, BLOCK_COUNT);
    TEST_ASSERT(device != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(device, "PerfProbe", 0), BFS_OK);

    bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, device, CACHE_SLOTS), BFS_OK);
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, CACHE_SLOTS / 2u);
    bfs_fs_t fs;
    bfs_err_t err = bfs_fs_mount(&fs, &cache.bio);
    if (err != BFS_OK) {
        bfs_cache_destroy(&cache);
        bfs_bio_close(device);
        unlink(TEST_IMG);
        TEST_ASSERT_EQ(err, BFS_OK);
        return;
    }

    start_probe();
    bfs_perf_probe_fs = &fs;
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "probe", 5, &ino), BFS_OK);
    TEST_ASSERT(ino != 0);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    uint8_t payload[BLOCK_SIZE + 137u];
    memset(payload, 0x6D, sizeof(payload));
    TEST_ASSERT_EQ(bfs_file_write(&file, payload, sizeof(payload)),
                   (int32_t)sizeof(payload));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    bfs_perf_probe_snapshot_t *counters = &bfs_perf_probe_counters;
    TEST_ASSERT(counters->deferred_node_writes > 0);
    TEST_ASSERT_EQ(counters->free_tree_node_writes + counters->dir_tree_node_writes +
                   counters->inode_tree_node_writes +
                   counters->refcount_tree_node_writes +
                   counters->other_tree_node_writes,
                   counters->btree_node_writes);
    TEST_ASSERT_EQ(
        counters->free_tree_allocation_body_node_writes +
        counters->free_tree_reserve_refill_node_writes +
        counters->free_tree_reserve_return_node_writes +
        counters->free_tree_post_publish_pending_reclaim_node_writes +
        counters->free_tree_other_node_writes,
        counters->free_tree_node_writes);
    TEST_ASSERT_EQ(counters->dir_tree_leaf_node_views +
                   counters->dir_tree_internal_node_views,
                   counters->dir_tree_node_views);
    TEST_ASSERT_EQ(counters->dir_tree_leaf_resident_views +
                   counters->dir_tree_internal_resident_views,
                   counters->dir_tree_resident_views);
    TEST_ASSERT_EQ(counters->inode_tree_leaf_node_views +
                   counters->inode_tree_internal_node_views,
                   counters->inode_tree_node_views);
    TEST_ASSERT_EQ(counters->inode_tree_leaf_resident_views +
                   counters->inode_tree_internal_resident_views,
                   counters->inode_tree_resident_views);
    if (counters->sealed_commits > 0)
        TEST_ASSERT(counters->free_tree_node_writes >= counters->sealed_commits);

    bfs_perf_probe_fs = NULL;
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    stop_probe();
    bfs_cache_destroy(&cache);
    bfs_bio_close(device);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Performance probe host oracles")
    setbuf(stdout, NULL);
    TEST_RUN(test_detail_sampling_disabled_tree_and_reset);
    TEST_RUN(test_inode_read_wrappers_count_errors);
    TEST_RUN(test_direct_and_deferred_tree_attribution);
    TEST_RUN(test_expected_level_node_view_accounting);
    TEST_RUN(test_filesystem_cache_finalizes_deferred_nodes);
TEST_SUITE_END()
