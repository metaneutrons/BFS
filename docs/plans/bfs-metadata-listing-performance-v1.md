# BFS metadata and small-write performance, v1

Decision state: local staged implementation authorized by Fabian on
2026-10-08. GitHub tracking links are pending; creating issues, opening PRs,
starting remote CI, merging and publishing are not part of this authorization.
This plan owns acceptance requirements, not a live execution checklist.

## Outcome and boundaries

Reduce the remaining AmigaDOS listing and small-write overhead without changing
format v3, the current commit policy, data-integrity checks, snapshot isolation,
recovery or cross-handle visibility. Implement shared filesystem logic in the
common core; frontend packet layout and policy remain in the adapters.

The starting code is main `07216b7f17912c9f28b9b4a920caaa6828b00ccb`.
The retained [4 October comparison](../qualification/bfs-write-path-in-place-performance-2026-10-04.md)
has eight fresh runs per handler. Median per-run BFS/PFS3 factors are:

| Workload | Compare | Durable compare |
| --- | ---: | ---: |
| ExAll | 3.62 | 3.46 |
| ExNext | 2.69 | 2.72 |
| Append 256 KiB in 1 KiB steps | 2.57 | 2.65 |
| Append 1 MiB in 4 KiB steps | 2.15 | 2.36 |
| Delete 40 | 2.08 | 2.17 |
| Create 40 | 1.72 | 1.88 |
| Small read 40 | 1.45 | 1.38 |
| Write 8 MiB | 0.95 | 1.01 |

Listing "400" means ten passes over 40 files, not one directory of 400
files. Create includes writing 1 KiB and closing each file. Durable compare
adds an ACTION_FLUSH within each mutating phase's measured time. Its 1 KiB
append series includes a 5.05x outlier. These historical measurements are
not a fresh baseline for a changed handler or guest.

The existing <=5x goal remains open until every checked workload and run
satisfies it. A useful individual optimization may be retained without
claiming that the overall goal or universal PFS3 parity has been reached.
The host's approximately 44% inode-read fraction is a prioritization signal,
not a measured 68k speedup.

Non-goals: format changes, persistent duplicate metadata in directory records,
weaker CRC/validation, disabled snapshot support, changing delayed/SYNC
defaults, relaxed flush fences, hardware qualification, assembly tuning without
a relevant measured bottleneck, or restarting the stopped CI runner.

## Design and decisions

### Operation-local inode reuse

Reuse a freshly validated inode from the same locked write operation rather
than reading it again at finalization. No persistent inode image is added to
the file handle. Selected tree identity, root, generation, inode identity and
linked/unlinked state guard reuse; invalidation falls back to the current read.
Intermediate commits still publish current size and mapping before commit.
Recovery and timestamp-provider contracts remain unchanged. Unlocked entry
points without a seed retain the ordinary read path.

### Bounded common-core lookup hints

Cache inode-to-leaf/index hints, not copied inode values. A hit must still
obtain a currently validated resident node from the BIO, check leaf level,
index bounds and the current key, copy the current value immediately, and run
ordinary linked/unlinked inode validation. Tree identity/root/generation, a
BIO-wide mutation epoch and mount/recovery lifetime guard the hint. Every
potential cache mutation, including failed writes and explicit invalidation,
changes the epoch. Resident-node validation additionally rejects an evicted or
changed leaf. The existing last-leaf shortcut uses the same epoch guard: a raw
write to an internal node must not be hidden by its fallback either.
Missing hooks and any mismatch fall back to the existing search. The bounded
storage belongs to the mounted filesystem and is attached only to its inode
tree; standalone and immutable-view trees need no additional allocation.
Both Amiga and Linux callers use the same core logic. Cold-cache and larger-
directory evidence must accompany warm repeated listings.

Schema 4 adds real 40-, 400- and 1,000-entry directories with separate fixtures
for ExNext and ExAll. Each API measures its first pass after setup and ten
additional same-mount passes, all validating names, types, counts and uniqueness.
The six fixtures together contain 2,880 empty files; the common inode and
directory trees therefore reflect that complete volume, including the 40-entry
case. Setup and its explicit flush are outside the listing timers. First pass
is not a cold-cache claim: creation and preceding listings may warm metadata,
and neither the host file cache nor the device state is controlled. A separate
remount/setup boot path remains necessary before claiming handler-cache-cold
elapsed qualification. Host tests can explicitly invalidate the cache for
correctness, but do not substitute for that Amiga timing evidence.

A root/generation-only cache of copied inode values is rejected: it could
hide corruption or changed bytes after BIO invalidation, even though it caught
normal inode-tree writes. Leaf validation alone is also insufficient: a raw
internal-node write can change reachability without changing the leaf bytes or
the tree generation. A missing epoch hook therefore disables path-bypassing
hints; ordinary validated traversal remains available.

### Exclusive ExNext continuation

For a current cursor whose stopped entry was consumed, resume at the following
entry without reconstructing the old directory key. Keep a key-based fallback
after mutations or cursor loss. This is distinct from ExAll's inclusive stop:
its overflow entry has not been consumed and must be retried. Shared scan
logic must retain callback-mutation and leaf-transition guarantees.

### Partial-block and namespace investigation

Profile current 1 KiB writes on 4 KiB blocks and create/delete before broader
changes. The existing write-through cache already admits a block on its first
RMW read miss and updates resident bytes on subsequent writes; another tail
cache is not justified without evidence of admission/eviction overhead.
Dirty coalescing requires a separate reviewed design
for cross-handle visibility, error timing, flush/sync/unmount, checksum and
snapshot behavior. Do not silently defer a write failure or weaken an existing
guarantee to obtain a faster result. Rebalancing only merits implementation if
the measured workload actually spends material work there; node ownership
alone does not authorize publishing a partially failed operation.

If the larger-directory comparison exposes a material failure of the <=5x
target, profile that working set before further partial-write tuning. Separate
inode traversal, resident-node misses/validation, directory scan continuation
and adapter serialization. Any batching or inode-order optimization must keep
current validated results, bounded memory and ExAll overflow/filter semantics.

Deep schema12 adds the same append and listing workload code, with probe-only
inode-read/search/hint counts, logical cache-read hits/misses, and per-tree
node_view/resident-view counts. Production schema4 remains unchanged. These
diagnostic counts distinguish traversal and physical transfer but are not
elapsed-time forecasts; packet, BIO, CRC and CPU scopes overlap and must not
be added. Node views count the named traversal helper, not every internal
B-tree operation; successful search hints have separate counters. Resident
views still check their expected level and parent bounds. The profile ABI is
version13 for that retained measurement. The subsequent
[detailed listing profile](../qualification/bfs-listing-detail-profile-2026-10-08.md)
uses deep schema14 and probe ABI15, with inclusive sampled inode-search,
node-view, node-structure, binary-search, buffer-lease and ExAll-fill scopes.
The prime stride17 avoids fixed root/leaf sampling parity; samples are not
exclusive CPU totals or production-time estimates. The verifier retains older
schemas and checks the new deferred-write attribution without relaxing the
existing commit/free-tree invariant.

Both corrected detailed runs identify inode lookup and directory-node access
as the next bounded investigation. The benchmark requests ED_COMMENT, so an
ExAll optimization restricted to ED_SIZE/PROTECTION/DATE cannot satisfy its
measured workload. Before batching, compare metadata-cache capacity at the
same workload and policy. Any later bounded common-core batching must preserve
comment validation, original output/error order, filters and inclusive overflow
retries. No batching or cache-size change is accepted by this diagnostic step.

### Bounded resident leaf ranges

After the detailed listing profile, evaluate a bounded table of validated u32
leaf routes in addition to exact-key hints. A route records only a block number
and the leaf's first and last keys, never inode/value bytes or a borrowed BIO
pointer. A sorted table of at most 64 routes supports logarithmic lookup without
assuming contiguous inode allocation. The owning tree, BIO, layout operations,
root, generation and BIO mutation epoch must match the traversal that
established reachability. Identity changes also invalidate exact-key slots;
pointer identity does not replace the required reset at object lifetime changes.
The leaf must still be resident and currently validated, at leaf level, and
its current endpoints must match. Any mismatch or search miss falls back to
ordinary traversal; missing/saturated epochs disable and clear the routes.
Inode validation remains unchanged after copying the current value.

A dense key offset may be tried only as an index candidate: bounds and the
actual key must match. Sparse keys use the existing binary search, and missing
keys still use ordinary traversal. Route storage and replacement are bounded
and allocation-free; exact-key hints remain available for repeated single-key
lookups. This changes shared core lookup, not the on-disk format, ExAll layout,
cache capacity, commit policy or visible error ordering. ExAll inode batching
is deferred because it requires additional comment/filter/overflow/error-order
qualification beyond a location-hint change.

## Delivery and acceptance

### M1: Operation-local inode reuse

Execution: local implementation; issue link pending.
Dependencies: reproducible current-baseline handler and guest identities.

- M1-A1: The normal successful write uses the validated operation-local image;
  identity/generation changes cause a fresh read. A focused positive and
  counter-probe demonstrates the eliminated duplicate work where practical.
- M1-A2: Focused tests cover stale handles, fresh metadata, positive/zero/partial
  writes, unlinked files, intermediate commits and recovery. Existing timestamp,
  inode-publication fault, file and snapshot oracles remain unchanged and pass.
- M1-A3: Host and ASan/UBSan tests and m68k production/probe builds pass. Paired
  normal-handler compare and durable-compare runs establish benefit, neutrality
  or regression; source counts alone do not establish a performance win.

### M2: Bounded inode lookup hints

Execution: local implementation; issue link pending.
Dependencies: M1 qualification before combined performance attribution.

- M2-A1: Hinted hits preserve the full validated API result; mutations, tree
  changes, recovery, remount, deletion/reuse and linked/unlinked changes cannot
  expose stale values. Raw BIO writes and invalidation cannot be hidden by a
  copied value. Corrupt/missing first reads are not cached as success.
- M2-A2: Positive hits and invalidation/error counter-probes pass under ordinary
  and sanitizer tests, including multiple handles and immutable snapshot views.
- M2-A3: Record memory bounds and fresh performance comparisons for normal and
  durable workloads, including warm and cold listings and larger directories.
  Reject or revise the candidate if its additional overhead erases the benefit.

### M3: Consumed-entry ExNext continuation

Execution: local implementation; issue link pending.
Dependencies: M2 qualification before combined performance attribution; its
implementation may proceed independently in disjoint files.

- M3-A1: Unchanged ExNext cursors advance exclusively; ExAll overflow still
  retries the unconsumed entry. Mutation fallback, missing cursor, leaf boundary,
  filtered entries and callback changes return each expected entry exactly once.
- M3-A2: Existing and new cursor tests, sanitizers and handler builds pass;
  listing-focused and ordinary AmigaDOS comparisons show the effect, including
  a directory large enough to traverse multiple leaves.

### M4: Partial-block and create/delete decision

Execution: bounded investigation/design; issue link pending.
Dependencies: M1-M3 current profiles and qualification.

- M4-A1: Attribute partial-block reads/writes, inode searches, allocations,
  splits/rebalances and commits without adding overlapping scopes into a false
  residual. Separate instrumentation results from production elapsed time.
- M4-A2: Retain, reject or defer each candidate with evidence and an explicit
  reason. A dirty tail-cache implementation requires an accepted design with
  unchanged externally observable guarantees, not just a promising profile.
- M4-A3: Any selected implementation gets focused byte-visibility, flush,
  snapshot/checksum, injected-failure and recovery tests plus fresh paired
  production measurements before retention. A rejected candidate is not
  reported as an implemented speedup.

### M5: Bounded resident leaf ranges

Execution: local implementation authorized by Fabian; issue link pending.
Dependencies: the corrected detailed profile and M1-M3 correctness evidence.

- M5-A1: Distinct keys can reuse a positively validated resident leaf without
  copied inode values; exact-key and last-leaf hints cannot substitute for the
  positive test. Sparse keys, absent keys, endpoint mismatches and route-table
  capacity/replacement preserve ordinary results and error fallback.
- M5-A2: Epoch absence/saturation, raw leaf and internal-node writes, failed
  writes, tree changes, recovery, deletion and remount cannot hide stale or
  corrupt bytes. Snapshot views remain separate. Focused host/sanitizer tests,
  complete host tests, quality/static checks and relevant Amiga tests pass.
- M5-A3: Interleaved fresh-image normal and durable production comparisons
  use the same guest, formatter, volume and buffers for M3 and the candidate.
  Eight runs per handler/mode qualify a gain or regression. Independent
  30/64/128-slot experiments inform cache pressure but do not silently change
  the default or close the unchanged <=5x goal. Record measured memory bounds
  and retain failures alongside successful runs.

### Rejected exact-key fast path experiment

The first eight-run normal range series improves all large repeated ExAll
pairs but slows all actual-40 repeated ExAll pairs. This motivated testing
return from a fully guarded exact-key hit before preparing the range table.
Owner/BIO/layout and per-entry root/generation/epoch checks stayed mandatory;
range preparation and invalidation still ran on a miss or unavailable epoch.
The hypothesis was implemented and correctness-tested on 2026-10-09, but rejected
after the completed eight-run normal comparison per handler. Warm-40 ExAll
was essentially neutral against the previous range build; 400-entry ExAll
and 4 KiB append showed slower paired medians. The remaining durable and
diagnostic measurements were stopped. Only this candidate and its new oracle
were removed; the previous leaf-range source is restored byte-for-byte.
The [rejection report](../qualification/bfs-exact-key-fastpath-2026-10-09.md)
retains every completed and interrupted run. This does not relax M5-A3 or
qualify the incomplete normal/durable inventory for retention. M5 remains
provisional; investigate material directory/inode cache misses before another
dispatch-only micro-optimization.

## Evidence, migration and verification cost

Use the existing fresh-image interleaved harness, identical guest/formatter,
PFS3, ROM, volume geometry and mount policy for each comparison. Alternate
filesystem order and preserve all raw runs, failures and ratios above five.
Use pilot runs to reject broken candidates; qualification uses eight fresh
runs per handler/mode, with repeated matched evidence before attributing a win.
No absolute gain is promised in advance. Host scheduling is uncontrolled.

Preserve text evidence and input/binary digests under docs/qualification;
licensed assets, HDFs and binaries stay ignored. Link criterion IDs and exact
code/input identities in the result report. Expiring logs are not the only
evidence. No estimate of runner time or deadline is established.

Focused local tests run during iteration. Final retained changes require the
complete host/sanitizer, static/quality, conformance and relevant local Amiga
compatibility checks before a later push, following HANDOFF.md. Remote CI is
not requested. Linux FUSE and native AROS evidence are separate claims; do not
infer them from a host unit test or m68k emulator result.

There is no on-disk migration. Each candidate is isolated and can be removed
without changing volume format. Retain the baseline binary identity and paired
evidence so later steps cannot conceal a regression in an earlier step.

## Decision changes

- 2026-10-09: complete a balanced append/create confirmation using the same
  M5/sparse binaries: eight pairs per mode and two identical-M5 controls per
  mode, 40 fresh starts total. All strict data/identity checks pass, with
  unchanged 30-buffer capacity and source. The earlier 5–7% growth penalty
  does not repeat at that magnitude, but durable 4KiB growth remains 2.5%
  slower and durable grown-file read is 25% slower in 7/8 pairs. Strong A/A
  scatter and two controls per mode do not resolve causality or justify
  regression-free approval. Repeated large ExAll gains 11–16.5% again, but
  the <=5x goal remains open (large ExAll medians 14–16x PFS3). Retain only
  provisional local status; next separate read/flush timing and I/O/CRC work
  before another implementation/full series. The append-labeled fixture
  creates a new file, not an existing-file append workload. The
  [confirmation report](../qualification/bfs-sparse-crc-append-confirmation-2026-10-09.md)
  owns the complete raw cohort, mode-specific A/A controls and source/asset
  checks. No CI or publication is part of this step.
- 2026-10-09: provisionally retain exact sparse CRC only for used directory-leaf
  key prefixes, not generic file data. The 40-run comparison inventory and
  correctness/runtime checks complete with unchanged 30-buffer capacity,
  full-byte coverage, format and durability. Repeated large ExAll gains
  11–15%; ExNext gains 4–20% across normal/durable modes. The typed m68k
  microprobe confirms cheaper zero-heavy CRC; the initial incompatible-cast
  probe is explicitly nonqualifying, with its failed guard-build/stale-hash
  root error preserved. Append4KiB has 4.7%/7.0% slower paired medians and
  durable create is also slower; do not dismiss these possible regressions.
  Before broad acceptance/publication, confirm append/create with the same
  binaries and PFS3 calibrators. This bounded retention is not the <=5x goal:
  actual large ExAll remains about15–17x PFS3 and individual violations remain.
  The [complete report](../qualification/bfs-sparse-key-crc-performance-2026-10-09.md)
  owns all raw/control distributions, setup-error correction, sealed evidence
  and the reversible eight-file patch. Fresh leaf I/O remains a later bounded
  investigation; no adjacency/read-ahead assumption is authorized.
- 2026-10-09: evaluate scan-only two-touch admission at 30 buffers. A bounded
  block-ID history defers admission of a first absent directory leaf and lets
  a later qualifying miss admit it. Resident views keep ordinary MRU access;
  unchanged consumed cursor leaves visited solely for path reconstruction do
  not qualify as reuse. The history carries no bytes or validation proofs,
  resets on mutations, and degrades to normal admission if its optional
  allocation fails. CRC, structural, level and parent-bound checks remain
  unchanged. Protect warm 40-entry listings and require fewer total reads at
  both larger sizes before full normal/durable qualification. The separate
  [admission evidence](../qualification/evidence/bfs-scan-admission-performance-2026-10-09/)
  preserves the pilot, diagnostic comparison, patch and strict verifier.
  The four pilots and four diagnostic comparisons reject this candidate:
  repeated ExAll 400 improves, but warm ExAll 40 and both 1,000-entry APIs
  add reads/CRCs and show slower paired medians. Restore its eight source/test
  changes exactly and stop before full qualification. The
  [completed report](../qualification/bfs-scan-admission-performance-2026-10-09.md)
  owns the result. Prefer an isolated leaf-read cost reduction over another
  generic scan-cache policy; do not infer a safe batching/CRC speedup yet.
- 2026-10-09: reject replacement-only demotion of copied directory scan leaves
  at the unchanged 30-buffer capacity. Four production pilots and four separate
  ABI16/schema15 diagnostic comparisons pass strict verification. Repeated
  ExAll 400 saves reads/CRCs and improves, but warm ExAll 40 loses residency
  and ExNext 1,000 increases total reads/CRCs. Restore only the eight policy/
  test changes and do not start the remaining 32 qualification runs. Retain
  expected-level DIR/INODE leaf/internal diagnostic counters and verification,
  not the production policy. The
  [rejection report](../qualification/bfs-scan-cache-performance-2026-10-09.md)
  owns the complete pilot/deep evidence. Further admission experiments must
  distinguish warm reuse from streaming scans, protect the small working set,
  and reduce total reads at larger sizes; capacity and integrity stay fixed.
- 2026-10-09: evaluate guarded cursor parent-path retention before changing
  cache admission or capacity. Saved block locations and child indices avoid
  re-descending from the root after a stopped leaf is consumed. Reuse requires
  an unchanged available epoch and tree/backend/layout/geometry identity;
  parent and next-leaf access retain ordinary validation. Missing guards,
  callback mutation or errors preserve the existing descent fallback. The
  fixed path adds bounded per-cursor storage, not block buffers or persistent
  inode copies. Positive boundary and forced-fallback counter-oracles,
  corruption/mutation tests, matched diagnostics and paired production runs
  must establish benefit. Reject a neutral/regressing pilot before a full
  series; retaining the candidate still requires eight normal and eight
  durable runs per handler and the existing correctness requirements.
  The completed four-run pilot and four-run diagnostic series reject this
  standalone candidate: fewer traversal views accompany extra reads/CRCs
  and a small repeated ExAll regression signal. Restore only its source/test
  edits, retain the evidence, and do not start full qualification.
- 2026-10-09: reject exact-key early return before range preparation. Its
  completed normal series does not remove the warm-listing regression and
  shows additional regression signals. Restore the previous source, retain
  evidence and stop the unnecessary remaining measurements.
- 2026-10-08: choose current-leaf/index hints instead of copied inode values
  after a coherence review identified raw-BIO invalidation and recovery hazards.
  Existing node views and inode validation remain the authority for bytes.
- 2026-10-08: require a BIO mutation epoch after the prototype review identified
  unchanged-leaf hints bypassing a directly changed internal node. This also
  guards the pre-existing last-leaf fallback. Unrelated writes conservatively
  invalidate hints; pure read/listing reuse remains possible.
- 2026-10-08: extend the benchmark to truthful directory sizes. Its pilot
  exposes a much larger ExAll gap on real 400/1,000-entry directories already
  in the baseline; prioritize attribution of that gap before tail-cache work.
  Pilot ratios are not a qualified result and do not close any acceptance item.
