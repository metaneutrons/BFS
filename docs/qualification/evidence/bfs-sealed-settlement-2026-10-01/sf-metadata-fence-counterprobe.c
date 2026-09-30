#define main sf_unused_suite_main
#include "../tests/test_txn_sealed_persistence.c"
#undef main

int main(void)
{
    sf_fixture_t successful;
    sf_operation_trace_t trace;
    uint64_t publication_sync = 0;
    if (!sf_fixture_init(&successful, 0, false)) return 2;
    if (bfs_fs_sync(&successful.fs) != BFS_OK) return 2;
    sf_capture_trace(&successful.device, &trace);
    if (!sf_find_sync_ordinal_after_sb_write(&trace, 0, &publication_sync))
        return 2;
    sf_fixture_destroy(&successful);

    sf_fixture_t failed;
    if (!sf_fixture_init(&failed, 0, false)) return 2;
    sf_arm_sync_failure(&failed.device, publication_sync,
                        SF_SYNC_ERROR_PERSIST_SB_ONLY);
    bfs_err_t error = bfs_fs_sync(&failed.fs);
    uint64_t durable_id = 0;
    bool new_sb_only = error == BFS_ERR_IO &&
        failed.device.injected_sync_index == publication_sync &&
        sf_storage_best_txn_id(&failed.device, failed.device.durable_bytes,
                               &durable_id) && durable_id == failed.commit_txn_id;
    bool complete_graph = new_sb_only && sf_powercut_and_check(&failed);
    printf("SB-only failed publication: sync=%llu new_sb=%d graph_data_fsck=%d\n",
           (unsigned long long)publication_sync, new_sb_only, complete_graph);
    if (failed.mounted) bfs_fs_abandon(&failed.fs);
    sf_bio_destroy(&failed.device);
    return complete_graph ? 0 : 1;
}
