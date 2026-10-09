# Independent implementation review

Luna's read-only comparison found exactly one insertion in btree.c: the
resident-root helper. Shared node_view and ordinary bfs_btree_search_floor
source are byte-exact against the preimage. alloc.c changes only the internal
include and alloc_data_at_goal lookup with UNSUPPORTED fallback. The internal
header adds the helper declaration, not a public API or disk-layout change.

The helper uses the normal engine's validation context, current root, key
search and layout accessors. It requires height one and cache-key-order,
checks leaf level, copies outputs only on success, stores no cross-call
pointer/state and performs no BIO call between its peek and result copy.
Goal bounds, extent validation/coverage, reserve/owner guards and range
mutation/COW remain unchanged. No correctness issue was found.

A cold single-root miss performs a preliminary eligibility peek and another
peek in the ordinary fallback. Both are actual cache requests; instrumentation
must retain that cost. Successful direct views are recorded once. A separate
test-author review identified the NULL-root/nonzero-height shape mismatch;
the helper now validates shape before empty-tree NOTFOUND. This correction and
the final handler pinning precede every emulator measurement. The ordinary
floor implementation's original behavior is not changed.
