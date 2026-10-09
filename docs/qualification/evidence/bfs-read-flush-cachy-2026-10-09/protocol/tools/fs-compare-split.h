/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_FS_COMPARE_SPLIT_H
#define BFS_FS_COMPARE_SPLIT_H

/* Diagnostic schema only. The existing production guest stays pinned.
 * These tables are also read by the strict host verifier. */
#define BFS_SPLIT_OPS(X) \
    X(OPEN, open) \
    X(READ, read) \
    X(WRITE, write) \
    X(HANDLE_FLUSH, handle_flush) \
    X(CLOSE, close) \
    X(VERIFY, verify)

#define BFS_SPLIT_PHASES(X) \
    X(SMALL_CREATE_40) \
    X(LOOKUP_400) \
    X(SMALL_READ_40) \
    X(LIST_EXNEXT_400) \
    X(LIST_EXALL_400) \
    X(SEQ_WRITE_8M) \
    X(SEQ_READ_8M) \
    X(SMALL_DELETE_40) \
    X(APPEND_4K_1M) \
    X(APPEND_1K_256K) \
    X(APPEND_READ_1280K) \
    X(LIST_EXNEXT_40_ENTRIES_FIRST_PASS) \
    X(LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL) \
    X(LIST_EXALL_40_ENTRIES_FIRST_PASS) \
    X(LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL) \
    X(LIST_EXNEXT_400_ENTRIES_FIRST_PASS) \
    X(LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL) \
    X(LIST_EXALL_400_ENTRIES_FIRST_PASS) \
    X(LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL) \
    X(LIST_EXNEXT_1000_ENTRIES_FIRST_PASS) \
    X(LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL) \
    X(LIST_EXALL_1000_ENTRIES_FIRST_PASS) \
    X(LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL)

/* Timings are inclusive; DATA_READ_TICKS is a subset of READ_TICKS.
 * Packet scopes, device intervals and CRC intervals must not be summed. */
#define BFS_SPLIT_COUNTERS(X) \
    X(BIO_READS, bio_read_calls) \
    X(BIO_WRITES, bio_write_calls) \
    X(BIO_UPDATES, bio_update_calls) \
    X(DATA_READS, data_read_calls) \
    X(DATA_WRITES, data_write_calls) \
    X(NODE_WRITES, btree_node_writes) \
    X(TXN_COMMITS, txn_commit_calls) \
    X(EXTENT_MAPS, extent_map_calls) \
    X(READ_TICKS, bio_read_ticks) \
    X(WRITE_TICKS, bio_write_ticks) \
    X(UPDATE_TICKS, bio_update_ticks) \
    X(DATA_READ_TICKS, data_read_ticks) \
    X(DATA_WRITE_TICKS, data_write_ticks) \
    X(NODE_CRC_READ_CALLS, node_crc_read_calls) \
    X(NODE_CRC_READ_SAMPLES, node_crc_read_samples) \
    X(NODE_CRC_READ_SAMPLE_TICKS, node_crc_read_sample_ticks) \
    X(NODE_CRC_WRITE_CALLS, node_crc_write_calls) \
    X(NODE_CRC_WRITE_SAMPLES, node_crc_write_samples) \
    X(NODE_CRC_WRITE_SAMPLE_TICKS, node_crc_write_sample_ticks) \
    X(CLOCK_PAIR_TICKS, clock_pair_ticks)

#endif
