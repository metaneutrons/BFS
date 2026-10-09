# FreeTree allocation goal pilot scope

Fixed before execution: two diagnostics, two baseline/candidate production
pairs per normal/durable mode with reversed filesystem and handler order,
and one identical-baseline control pair per mode. Exactly 14 fresh Cachy
starts, all 23 workloads and both filesystems. No retries, exclusions, automatic
extensions or pooling with preceding evidence. Production and diagnostic guests
are pinned separately; the harness, format v3 and actual RDB buffers 30 remain
unchanged. Diagnostics use the existing write sidecar with all-call CRC and
detail stride 17; production remains uninstrumented.

Only alloc_data_at_goal tries a scratch-free floor query of the current,
fully validated resident FreeTree root leaf. Nonresident, taller or noncacheable
trees use the existing search. This stores no cross-call hint or pointer. The
shared node-view and generic floor-search bodies must remain byte-exact in
source. Node layout, validation context and binary search are reused from the
ordinary engine; reserve/extent/owner checks and all mutation/COW/durability
paths remain unchanged. Cold misses can perform an extra eligibility peek.
No new public API or on-disk change is introduced.

Local retention requires all correctness and identity checks, fewer diagnostic
4KiB growth buffer requests, lower paired 4KiB growth medians in both production
modes, and at least three of four target pairs not slower. Any phase at least
10% slower in both pairs of either mode prevents retention. This is a bounded
pilot rule, not statistical equivalence or production clearance. Controls and
PFS3 calibrators are descriptive, not noise bounds or reasons to discard results.
Probe times are inclusive/nested and sampled ticks are not extrapolated.

If retention fails, restore only this experiment's three code/header changes
and its new active test while preserving the candidate, tests and full results
in an immutable evidence snapshot. No second attempt in this step. The previous
sparse/read-regression concern and <=5x PFS3 objective remain open. No CI,
commit, push, merge, installation or issue mutation is included.
