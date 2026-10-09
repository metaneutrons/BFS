# Resident root floor pilot scope

Fixed before execution: two diagnostic starts, two baseline/candidate pairs
per normal/durable mode with reversed filesystem and handler order, one
identical-baseline control pair per mode. Exactly 14 fresh starts; no retries,
outlier filters, automatic extensions or pooling with preceding cohorts.
Each start retains all 23 workloads and both filesystems. Diagnostic and
production guest/build identities are separate.

The implementation removes a scratch lease only for a current, fully validated
resident root leaf. It uses the shared node-view validation and floor-result
logic; cold misses lease scratch normally. It stores no cross-call pointer,
key, value or allocation hint and changes no integrity/durability guarantee.
Cold node-view diagnostic time now includes lazy scratch allocation; nested
timers remain inclusive and are not added/subtracted into exclusive budgets.

Local retention requires all correctness/identity checks passing and fewer
diagnostic buffer requests, plus a lower 4KiB growth paired median in both
normal and durable modes with at least three of the four target pairs not
slower. A phase slower by at least 10% in both pairs of either production mode
prevents retention without further explanation; no automatic extra series
will be launched to seek a more favorable result. These are conservative
pilot decisions, not statistical equivalence or production-release criteria.
The controls are descriptive, not a bound on noise or an excuse to remove
a result. Previous sparse/read-regression concerns remain open.

If the pilot does not support retention, restore only this task's B-tree
delta and keep its candidate source, tests, binaries' identity receipts and
raw results as rejected-experiment evidence. Do not alter preceding evidence
or unrelated dirty-worktree changes. No CI, Git commit, push, merge or issue
mutation is included.
