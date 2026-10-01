# BFS FreeTree allocation cost investigation

The next useful measurement is the work inside ordinary FreeTree allocation,
not another split or reserve-return experiment. Three bounded Luna audits map
the existing counters, allocator callers and B-tree primitives at checkpoint
`ec6bcb6`. The primary agent checks the source findings and raw extraction.
This is a read-only investigation using retained diagnostic runs, not a new
Amiga benchmark, an implemented optimization or performance acceptance.
The goal remains <=5 times same-run PFS3 in each checked workload, with all
durability, integrity, snapshot and recovery guarantees unchanged.

## Observed FreeTree work

The two opposite-order schema-12 runs agree on every non-time field. Counts
below are per run. They count node-write call sites, not elapsed cost or
physical device writes.

| Workload | FreeTree writes | Allocation body | Reserve refill | Other | Freespace allocation calls | Metadata interface allocation calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 353 | 233 | 40 | 80 | 233 | 193 |
| Write 8 MiB | 142 | 137 | 1 | 4 | 137 | 259 |
| Delete 40 | 197 | 116 | 40 | 41 | 116 | 116 |

Lookup 400, small read 40 and sequential read 8 MiB have zero FreeTree writes.
Reserve-return and post-publication pending-reclaim writes are zero in all six
phases; their run/batch/pass counters are also zero. Across the three mutating
phases, `692 = 486 allocation-body + 81 refill + 125 other` exactly. Commit,
sealed-commit, metadata-fence and superblock-publication counts each total 81.
The checked workloads already use the sealed settlement path, not the older
reserve-return/reclaim loop that dominated
[previous phase measurements](bfs-free-tree-phase-profile-2026-09-30.md).

Allocation-body writes numerically equal freespace allocation calls in each
mutating phase. This aggregate equality does not prove one write in every
individual request, identify request origin or establish each guard's hit
count. Interface requests can fall through to the public allocator or use
metadata spares, so these call counts overlap and must not be added.
Every FreeTree split and two-edit coalescence cell is zero. Those paths are
not a priority for another measurement in this workload.

## Why current counters miss relevant costs

Single-block allocation already selects the highest tail and makes one update
or an exact-fit delete (`src/core/alloc.c:455-488,542-563`). For a partial
multi-block allocation on a root leaf, the current implementation avoids
delete plus insert and makes one replacement write. It still selects via
first-fit scan, collects and validates the whole leaf, builds key/length arrays,
and invokes the replacement API, which reads the old root again
(`src/core/alloc.c:565-625,924-978`; `src/core/btree.c:571-634`). Exact fits are
delete-only; partial fits in deeper trees keep the two-edit fallback. No
duplicate unchanged-node write exists in `cow_node`.

There are two important diagnostic gaps. First, `ALLOCATION_BODY` is a
node-write provenance bucket, not a complete CPU scope. On the first-fit path
its entry at `alloc.c:614` follows selection scans at lines 575-600. Reusing
that marker for read or traversal attribution would incorrectly leave
selection outside the allocation body.

Second, direct heap probes are confined to `btree.c`. They exclude scratch
allocations in `alloc.c:933,1115,1469` and reclaim scratch in `txn.c:193`.
The prior observation of small B-tree heap intervals therefore does not
establish that allocator scratch is cheap. At 4 KiB geometry, the partial-root
helper requests 4,064 bytes for its capacity-sized key/length arrays. This
is a source-derived allocation size, not a measured frequency, live-memory
peak, initialization volume or latency.

`OTHER` also combines direct free operations, metadata-free fallback and sealed
root replacement. Sealing already writes a single different-block root leaf;
allocation remains frozen through the graph fence and successful superblock
publication (`alloc.c:1505-1543`; `txn.c:219-260`). A new producer counter
should make that provenance explicit instead of assuming all 125 writes
are avoidable retirement work.

## Recommended next diagnostic

Measure request provenance and work before adding finer timers. Keep the
existing phase buckets and introduce an orthogonal operation scope covering
selection, validation, mutation and cleanup. Nested reserve refill must retain
its own provenance, and every error/early return must restore the enclosing
scope. Zero-write operations, especially successful metadata-spare take/stash,
must remain visible.

Record public data/extent requests separately from metadata-interface fallback;
classify tail update/delete, root-leaf partial replacement, exact-fit delete
and deeper partial delete/insert. For each class count attempts/outcomes,
requested/returned blocks, observed shape, node-read/write calls, search/scan
invocations and visited entries. Record allocator scratch requests and bytes
separately from B-tree heap calls. Subdivide `OTHER` by sealed replacement,
direct free, metadata-free fallback, format/add and unresolved cleanup.

Use counters rather than a clock read for every key or entry. If a frequently
hit stage merits timing, measure bounded inclusive helper scopes in a separate
private probe and report probe disturbance explicitly. Cached node reads are
not physical BIO reads, and overlapping allocator/CRC/device intervals must
not be added or subtracted to construct an elapsed saving.

If repeated selection/validation/copying dominates, a B-tree-owned leaf view
confined to one serialized operation is a candidate for a separate design.
It is not authorization for cross-operation ownership caching, stale root
proofs, in-place writes or removing validation. If the measured paths already
make one necessary final COW write, fewer writes require a different ownership
or allocation design; aggregate counts are not permission to omit that write.
The rejected repeated-tail and single-extent metadata-batch policies remain
rejected. Fewer writes previously coincided with substantial normal regressions.

## Verification and evidence

The [counter extraction](evidence/bfs-free-tree-allocation-cost-audit-2026-10-01/free-tree-non-split-counter-review.md),
[caller map](evidence/bfs-free-tree-allocation-cost-audit-2026-10-01/free-tree-non-split-sites-review.md)
and [primitive and test inventory](evidence/bfs-free-tree-allocation-cost-audit-2026-10-01/free-tree-non-split-btree-cost-review.md)
retain the supporting Luna findings. The raw inputs remain in the
[previous measurement archive](evidence/bfs-split-coalesce-attribution-2026-10-01/),
with pinned SHA-256 identities; no raw input is regenerated or discarded.
An [independent focused Luna review](evidence/bfs-free-tree-allocation-cost-audit-2026-10-01/independent-review.md)
passes after correcting evidence links. It checks the primary report's claims
and limitations, not another runtime qualification.

The extraction verifies all four raw identities and equality of 1,326 BFS
non-time fields and three PFS3 fields across orders. These field counts include
schema/drive/PASS and other non-phase metadata, not only operational counters.
The primary replay reproduces the extraction JSON byte for byte, including
execution outside the repository and from an isolated copy of the four raw
inputs. A one-byte mutation in that owned copy rejects before JSON emission
with expected exit 1. From the repository root, run
`node docs/qualification/evidence/bfs-free-tree-allocation-cost-audit-2026-10-01/verify-extraction.mjs`
to repeat these checks. `SHA256SUMS` covers the new text evidence; source
identities refer to the audited checkpoint, not a new implementation.
The extraction is not a replacement for the prior strict schema/data verifier.
The new evidence passes the scoped text-asset check. This is not a fresh
whole-repository audit or a claim that the known older formatter-record
asset findings have been repaired.

Existing root-shift tests cover placement, wrap, exact/deeper fallback,
pre-swap read cuts, write rollback, headroom, old/current ownership, scratch
exhaustion, overlapping extents, sticky recovery and remount/checker oracles.
Metadata-reuse and sealed tests cover stock, snapshots, convergence and
persistence. Previous private counter fixtures also cover coalescence and
singleton first/second-edit rollback. These are existing source/qualification
references; no logical suite is rerun or newly qualified in this audit.
Any added instrumentation still needs probe-enabled site/restore fixtures,
strict schema rejection tests, normal-handler identity and actual checked
opposite-order guest runs before its result is usable.

No production source, allocation policy, CLI, format, publication or flush
behavior changes. No new emulator, soak, CI, push, PR, merge, release or
physical power/controller/media qualification is performed. The overall
performance goal is still open.
