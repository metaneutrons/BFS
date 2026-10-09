# Independent final test review

Luna independently reviewed all eight finalized oracles. No assertion or
correctness defect was found. The standalone validated hit proves one peek,
zero scratch leases/releases and unchanged device I/O. Allocator tests call
bfs_freespace_alloc_data with real goals; failing allocation before the first
peek would reject the ordinary lookup's pre-peek scratch lease. Setting
global_reserve to UINT32_MAX isolates lookup by suppressing reserve refill,
not by bypassing goal selection or ownership/accounting.

Successful allocation checks the exact goal, free count, data roving, reserve
stock and split extents. Goal miss checks first-fit allocation. Read-only and
invalid reserve stock are rejected before a peek/write. The cold adversary
swaps keys, recomputes a valid CRC and invalidates cache; the fast path declines,
then ordinary structural validation rejects the node with no writes/accounting
change. A separate cold bad-CRC fixture is not among these eight new tests;
the existing generic engine corruption tests remain in the full suite.

The first parallel ASAN build captured intermediate test-author drafts, with
four failed assertions whose line numbers/assertions differ from the finalized
file. Its log is retained. The finalized file passes a fresh full 586-test
host suite and the same 586 tests under ASAN/UBSAN before emulator execution.
No candidate runtime result or failed benchmark is replaced or excluded.
