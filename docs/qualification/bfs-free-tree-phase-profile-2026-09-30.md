# BFS free-tree phase attribution — 2026-09-30

## Result

Two fresh-image, order-reversed FS-UAE deep-compare runs passed the strict
verifier. Every non-timing BFS counter, including all five new buckets, was
identical in both orders. The buckets sum exactly to the free-tree node-write
total in each of the six workload phases. The table reports writes of B-tree
nodes, not physical-media writes or elapsed time.

| Workload phase | Total | Allocation body | Reserve refill | Reserve return | Post-publication pending reclaim | Other |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 × 1 KiB files | 3,029 | 313 | 79 | 2,281 | 236 | 120 |
| Write one 8 MiB file | 1,089 | 644 | 2 | 53 | 6 | 384 |
| Delete 40 small files | 2,660 | 116 | 80 | 2,223 | 240 | 1 |

Reserve return accounts for 75.3% of the create-phase and 83.6% of the
delete-phase free-tree writes. Post-publication pending reclaim accounts for
7.8% and 9.0%, respectively. In the sequential-write phase, allocation-body
writes account for 59.1% and the currently unclassified `other` bucket for
35.3%. The three lookup/read phases have zero free-tree writes.

This changes the immediate optimization priority: a reclaim-only multi-edit
cannot address most small-file free-tree traffic. Investigate why reserve
return performs so many separate COW tree edits, then design a bounded,
transaction-safe way to combine them. For sequential writing, subdivide the
`other` bucket before proposing a change; its 384 writes are not yet
attributed to a specific call path. Allocation-side batching remains a
separate target. No reduction in node writes or latency is claimed here:
this change only measures the existing behavior.

## Method and checks

The probe-only handler now tags a free-tree node write by the active
allocation, reserve-refill, reserve-return or post-publication pending-reclaim
scope. All remaining free-tree node writes fall into `other`. The scope is
restored on every instrumented return path; refill nested within reserve
return or pending reclaim retains the outer phase. The ordinary handler is
unaffected because the instrumentation is conditional on `BFS_PERF_PROBE`.
The guest tool emits deep-compare schema v5; the strict verifier accepts both
v4 and v5 and checks every v5 bucket sum. The probe ABI version is 6.

Both runs used fresh 255.5 MiB BFS and PFS3 partitions, FS-UAE 3.2.35,
A1200/68040 at maximum emulated speed, Kickstart 47.102 and Workbench 47.2.
The second run reversed the filesystem order. Both mounted volumes were
read/write after PFS3 formatting, both guest workloads completed and checked
their data, and both output files ended with `PASS`. The pre-format `info.txt`
lists DH2 as not yet formatted; `info-after-format.txt` and the checked PFS3
workload establish the actual measurement state. A preliminary run omitted
the longest counter name because of a guest-tool output-buffer limit. It
failed the strict verifier and is excluded; the buffer was enlarged before
these two fresh runs.

`make amiga-perf-probe-handler`, `make amiga-fs-compare-bench`, the focused
verifier tests and full `make check` passed. The two strict guest-result
verifications passed. Probe handler SHA-256:
`b2e46d3d906937de96851cfdd404f2045c1839dc40c55f833b63c42b2f5861dd`.
Guest tool SHA-256:
`5ff3caa207b995a4c3983ebeda255d9da8e67aafa844ddac5078bd2ada9e3756`.
The unchanged PFS3 handler SHA-256 is
`bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

The [raw evidence](evidence/bfs-free-tree-phases-2026-09-30/) contains both
TSVs, completion marker, before/after inventory, emulator configuration and
log for each order. `SHA256SUMS` covers every retained file; log copies differ
from the emulator originals only by removed trailing spaces. HDFs, ROM and
licensed binaries are not committed. Probe timing values are retained in the
TSVs but are not an uninstrumented speed comparison or evidence of parity
with PFS3. These emulator counters do not qualify physical hardware,
controller flush behavior or power-loss durability.
