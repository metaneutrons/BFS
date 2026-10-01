# BFS single-extent metadata withdrawal performance

The one-fitting-extent follow-on is rejected for elapsed performance. Every
matched create, write and delete observation is slower than the timestamp
baseline, despite substantially fewer Free-Tree writes. The five-times PFS3
objective remains unachieved. Its production and sealed-fixture changes were
removed exactly at this qualification checkpoint. The timestamp baseline is
retained; subsequent diagnostic-only instrumentation is a separate increment.

## Normal comparison

Four fresh-image normal runs per revision ran serially, balanced by filesystem
order, with the same handler-independent inputs and no remote compiler or
concurrent emulator. Means below are elapsed microseconds. All observations,
including adverse samples and same-run threshold failures, are retained.

| Workload | Baseline BFS | Candidate BFS | Change | Candidate PFS3 | Candidate ratio of means |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 199118.5 | 280557.25 | +40.900% | 17285 | 16.231255 |
| Lookup 400 | 106703.25 | 105359.5 | −1.259% | 67117 | 1.569789 |
| Small read 40 | 50720 | 46015.25 | −9.276% | 15548.25 | 2.959513 |
| Write 8 MiB | 383006.5 | 497709.5 | +29.948% | 43500 | 11.441598 |
| Read 8 MiB | 515625 | 507677.5 | −1.541% | 261454.5 | 1.941743 |
| Delete 40 | 179951.5 | 233246.25 | +29.616% | 7855.75 | 29.691150 |

Seventeen of 24 order/repeat-matched BFS changes are adverse. All twelve
mutating matched pairs are adverse and all twelve candidate create/write/delete
observations exceed five times same-run PFS3. Independent extraction agrees with
all sixteen strict TSVs, four-run means, twenty-four named matched deltas and
same-run ratios. An initially unlabeled reviewer vector used the opposite order
for the two repeat pairs; explicit pair names resolved that ordering difference.
The [complete measurement summary](evidence/bfs-metadata-single-extent-2026-10-01/measurement-summary.md)
retains every raw tuple and threshold failure. The other phases remain below five
in these observations. Four observations do not establish statistical or
causal precision. Scheduling and PFS3 variation are uncontrolled; no read-only
improvement is attributed to the allocator. These consistently adverse mutating
observations reject the experiment without relying on a node-count prediction.

## Mechanism and qualification boundaries

The follow-on retains the first prototype's explicit mounted-owner binding,
full sink tuple, pre-write future/equal-live published-root checks, complete
leaf/stock/geometry preflight, conservative pending-free headroom, protected
surplus pop, exact live-epoch permission and post-start uncertainty latch.
It replaces repeated short highest-tail withdrawals only in the eligible
optional batch: reverse-select one highest fitting extent and use the shared
ordinary tail/append implementation once. No fit or sole-entry exact deletion
declines before mutation. Public data allocation and ordinary short-tail refill
policy are unchanged.

The single draw accounts for old/current root age and reserve/emergency scratch
ownership. Postflight requires exact reserve/free/emergency/pending deltas and
a different live, unpublished root before granting permission. On-disk format,
snapshots, deeper trees, synchronous COW/CRC errors, allocation freeze,
publication fences, flushes and the settlement bound are unchanged.

Pre-start memory exhaustion is an explicit availability limitation: the extra
preflight scratch can fail where ordinary allocation might succeed, and NOMEM
is propagated rather than treated as a decline. This is not a claim of
unchanged low-memory availability; no corruption/IO error is discarded and no
failure after optional mutation retries ordinary allocation.

Both candidate diagnostic orders agree on these counts; earlier timestamp
probes are historical context, not a contemporaneous paired CPU baseline.

| Phase | BIO writes | Node writes | Free-Tree body / refill / other | Direct B-tree malloc calls | Commits / updates |
| --- | ---: | ---: | --- | ---: | --- |
| Create 40 | 395 | 315 | 40 / 40 / 40 | 85 | 40 / 120 |
| Write 8 MiB | 2438 | 389 | 128 / 1 / 1 | 131 | 1 / 3 |
| Delete 40 | 236 | 196 | 0 / 40 / 40 | 215 | 40 / 120 |

Allocator, heap and device timings overlap; they cannot be summed or subtracted
to invent an elapsed residual. Direct heap counters exclude other translation
units and BIO buffer hooks. CRC timing samples every 64th call. The primary
agent initially misnamed create's 395 BIO writes as node writes in commentary
and corrected it explicitly; the raw output and table above distinguish them.

Read-only source review identifies seven to eight root node_read invocations
in one successful height-one batch, including repeated tag/full-scan and
postflight absence reads. These are not physical reads or repeated CRC work:
context-valid cached structure proof can avoid both CRC and structural checks.
No speedup follows from static call counts. A B-tree-owned within-call leaf
view would require explicit error-ordering and mutation-provenance design;
the current APIs expose tags or transient scan callbacks, not such a view.
Earlier stock, geometry and exact-set experiments also failed elapsed acceptance.
Another allocation policy is not selected from this mapping alone.

The expanded fault fixture separates first single-COW full/prefix write cuts
from second-write cuts on a genuine no-fit ordinary multi-tail fallback. The
latter preserves the ordinary path's valid first progress and retry semantics,
not the batch-only sticky latch. A separate deliberately artificial test changes
the real mounted owner's pending count after a successful scratch BIO; its
unchanged retirement callback then fails after root swap. This is volatile
owner-state corruption, not a naturally reachable valid-context failure or a
controller/power-loss/hardware-media qualification.

The artificial owner fault also proves later allocator and sync calls reject
with AGAIN and no new writes. Sync checks the sticky error before entering
commit; the deliberately contradicted queue is neither inspected nor published.

An intermediate failed fault run was overwritten by its worker and is
unavailable. It used an incorrect successful-allocation oracle: the allocator
interface's error callback returns NOSPC when last_error is clear. The corrected
oracle checks the actual returned block and last_error. This missing historical
log is disclosed; it is not reconstructed or counted as a gate.

## Safety evidence and limits

The source has independent ownership/accounting review and passes the preserved
16-case V1 functional oracle. Fresh V2 variants removing only the published-root
or pending-headroom guard each fail exactly the corresponding preserved oracle
(15 pass, one expected failure); the old V1 mutant sources are not reused.
Seven existing focused suites / 107 cases pass. Eighteen final functional cases
and nine fault cases pass separately. Complete frozen normal and actual
ASan/UBSan runs each pass 50 suites / 487 cases. Leak detection is explicitly
disabled; this is not LeakSanitizer qualification. Seventy quality tests plus
shell/static checks, twenty Linux conformance tests plus actual FUSE and two
reproducible normal/probe m68k builds pass. The two moving test files were
excluded from prebuild and compiled only after final freeze. No production
source changed during qualification.

Functional cases cover exact first-draw stock/free/emergency/pending deltas
across seven block geometries, zero-stock older/current roots, real held-block
ownership and return before commit, high crumbs surviving lower fitting-extent
selection, exact full-leaf capacity, genuine no-fit ordinary success, churn,
fsck/remount and actual deeper-tree sealed-commit fallback. This file does not
independently cover standalone warmup, an empty mounted Free-Tree or an older
root with nonzero starting reserve. The fault fixture likewise does not record
nonzero initial stock, so its published-root retirement witness is not proof
of that last combination. Existing generic regression cases remain intact.
Some assertion-failure paths can bypass cleanup/unlock; passing paths clean up.

Independent final functional review found no blocker. Its no-fit call does not
directly assert pending headroom; the fixture retains a 16384-slot default cap
and leaf-capacity-scale pending setup. That source-bound argument is reported
instead of an additional direct callback-headroom witness.

Unique failed functional fixture/compile logs are retained and are not gates;
the separately disclosed overwritten intermediate fault log is unavailable.

All ten completed Cachy emulator image directories were moved recoverably to
`/home/fabian/.cache/bfs-performance/metadata-single-extent-2026-10-01/`.
All twenty before/after timing TSV hashes agree; original local images remain.
No CI, push, PR, merge, release, external publication or hardware qualification
was performed.

## Reproduction and source disposition

The [raw evidence](evidence/bfs-metadata-single-extent-2026-10-01/) contains all
twenty unchanged timing TSVs, ten emulator configurations, guest completion and
machine outputs, builds/gates, failed functional attempts, scripts, exact guard
mutants as patches, source/test identities and a SHA256SUMS manifest. Only copied
log trailing horizontal whitespace/final blank lines are normalized. Unified
diff context spaces and raw TSVs are preserved. No binary, HDF or licensed asset
is committed.

The [production patch](evidence/bfs-metadata-single-extent-2026-10-01/metadata-single-extent-candidate.patch)
replays the five changed production files against `61ee01b`; the separate
sealed-fixture patch and two complete test sources reproduce the qualification.
Their reverse applicability was checked against the final live source before
withdrawal. Production-patch SHA256 is
`e4b224566e1fe863faae5dcd1954589f3a7a7aaaa086dac226a5522b794ba737`.
Candidate alloc.c is
`ed19a77f73da71782717ab5b80748a513b985dbc4c3987ee8e8fb4dd893f91f7`;
the identity log records every other source and test hash.

Normal baseline handler is
`572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`;
candidate normal is
`2db1e1b7c0fae544d6eda24fa55342d528c6404e6b9aa6ddbf29283cf5ea5646`;
candidate probe is
`eec8251eae5bcebfdb4cce2adceb9046123ea0b846c193f26bd44b50f53545b3`.
Guest, PFS3, formatter, ROM, Workbench, A1200/68040, 4 KiB BFS blocks,
options=0, 30 buffers and 255.5 MiB partition geometry match the retained inputs;
per-image identities and configurations record them exactly. Both normal/probe
handlers reproduce in separate build directories.

Withdrawal used apply_patch on the exact validated six-file reverse diff;
git diff then confirmed all five production files and the sealed fixture equal
HEAD. The two new test files were compared byte-for-byte with evidence, then
moved recoverably to `build/metadata-single-extent-rejected-source/`. No user
changes were discarded and no destructive checkout/reset was used. Retained
baseline qualification belongs to the preceding timestamp report; this report
does not claim an unnecessary repeat qualification after an exact restoration.

The next step is
[bounded handler/core CPU attribution](../plans/bfs-handler-cpu-attribution-v1.md)
on the retained baseline, not another unmeasured stock policy.
