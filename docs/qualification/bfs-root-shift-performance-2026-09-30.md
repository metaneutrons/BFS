# Atomic Free-Tree run carving — 2026-09-30

## Result and acceptance target

The target remains ≤5× PFS3 in **every** checked AmigaDOS workload, without
weaker durability, integrity, snapshot or recovery guarantees. It is **not
achieved**. This increment replaces a partial first-fit run's separate
delete/reinsert operations with one atomic COW root-leaf replacement.
The measured 8 MiB write drops exactly 128 Free-Tree writes in both diagnostic
candidate runs. It does not solve small-operation settlement.

Sixteen normal-handler runs use eight fresh images per version, interleaved
in four blocks with both filesystem orders. All samples, including slow ones,
are retained. Times below are microseconds; ratios compare BFS and PFS3 in
candidate runs. Ratios of means do not replace the per-run acceptance gate.

| Workload | Baseline BFS mean | Candidate BFS mean | Change | Candidate PFS3 mean | Ratio of means | Candidate range | Runs >5× |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 489501 | 457469.375 | -6.5% | 16736.625 | 27.33× | 21.48–32.20× | 8/8 |
| Lookup 400 | 131919 | 135535.125 | +2.7% | 77823.625 | 1.74× | 1.09–2.54× | 0/8 |
| Read 40 | 63551.375 | 61775.625 | -2.8% | 15008.375 | 4.12× | 2.58–5.99× | 2/8 |
| Write 8 MiB | 562937.375 | 509101.375 | -9.6% | 40007.5 | 12.73× | 9.09–14.89× | 8/8 |
| Read 8 MiB | 656956.5 | 616724.25 | -6.1% | 278174 | 2.22× | 1.99–2.42× | 0/8 |
| Delete 40 | 428365.625 | 442063.125 | +3.2% | 8568.125 | 51.59× | 36.46–66.70× | 8/8 |

Seven of eight order-matched write pairs are faster, but the first eight-run
batch's write mean changes only -4.3%. The combined -9.6% is descriptive, not
a confidence interval or a guaranteed gain. PFS3 write means also differ
between baseline and candidate runs (47623.625 versus 40007.5 µs), showing
that scheduling/runtime variation is material. Delete has two substantial
candidate outliers (494526 and 529406 µs) and a +3.2% combined mean;
this is not a proven absence of regressions. Small-read samples 68543/13696
and 81216/13568 µs are 5.005× and 5.986×, respectively, and fail the gate.

The change is retained as a narrow, tested reduction of allocation COW work,
not as small-operation parity or a universally qualified latency result.
The [measurement summary](evidence/bfs-root-shift-2026-09-30/measurement-summary.md)
contains every normal BFS/PFS3 row, batch means, paired deltas and per-run
failures. The A1200/68040, FS-UAE 3.2.35, Kickstart 47.102 and Workbench 47.2
setup is unchanged from the
[reserve-reuse comparison](bfs-metadata-reuse-performance-2026-09-30.md).
Cachy is a KVM guest; physical-host scheduling is not controlled. There were
no competing guest emulator or compiler runs. The same fixed formatter and
equal fresh 256 MiB partitions are used throughout.

## Mechanism and ownership

The existing first-fit scan and roving placement choose the run. For requests
larger than one block, a height-one Free-Tree and a selected extent strictly
longer than the request, collect the complete validated leaf and shift only
that entry's start and length. Bounds, adjacent non-overlap and the exact
selected entry are checked before mutation. Entry count, unrelated entries
and placement are preserved. One shared encoder serves both root-leaf paths.
Exact fits, deeper trees and single-block highest-tail placement are unchanged.

The existing **normal** B-tree replacement API preflights retirement headroom,
allocates a different scratch root and preserves the old root through a
failed/partial write. Current-root retirement retains allocator ownership;
older-root retirement retains the deferred-free queue. This is not an
owned-root transfer or an in-place overwrite. Errors never fall through to a
second delete/reinsert attempt, including `BFS_ERR_UNSUPPORTED`.

Accounting follows the actual root identity: after a swap the interval is
removed and roving advances exactly once, even if retirement subsequently
fails. Mounted read-only and recovery-state pointers reject allocation before
refill, including recursive allocator-interface calls. An ownership error
after swap or abort cleanup latches the mounted owner's recovery state;
another B-tree's local diagnostic reset cannot make that state publishable.
Format/standalone owners keep NULL pointers and the documented requirement
to abandon/recover uncertain ownership. Flush order, commit settlement,
snapshots, synchronous error reporting, format and CLIs are unchanged.

## Diagnostic attribution

Instrumented times are not normal-handler performance evidence. The baseline
probe and both candidate probes retain the same workload/data assertions.

| Write 8 MiB counter | Baseline probe | Candidate BFS-first | Candidate PFS3-first |
| --- | ---: | ---: | ---: |
| Free-Tree nodes | 274 | 146 | 146 |
| Allocation-body Free-Tree nodes | 264 | 136 | 136 |
| Refill / return / post-publish / other FT nodes | 1 / 2 / 4 / 3 | 1 / 2 / 4 / 3 | 1 / 2 / 4 / 3 |
| All metadata nodes | 567 | 432 | 432 |
| Inode / extent nodes | 163 / 128 | 156 / 128 | 156 / 128 |
| Data writes | 2048 | 2048 | 2048 |
| BIO writes | 2618 | 2483 | 2483 |
| Superblock publications | 3 | 3 | 3 |

The exact structural effect is 128 fewer Free-Tree writes, one per partial
data run. Seven further inode-node differences are timestamp/history effects,
not attributed to this change. Create/delete Free-Tree counts are
632/490 in the baseline and 624/489 and 637/490 in the candidates. Each small
phase still publishes 120 superblocks for 40 commits, with 80 post-publication
reclaim passes. That larger settlement cycle remains a separate design task.

## Verification and provenance

Fifteen focused tests pass in normal and ASan/UBSan builds. They cover one-write
allocation, first-fit/roving/wrap, unrelated entries and exact accounting,
exact-fit/single-block/deeper fallbacks, scratch exhaustion, every pre-swap
read cut, partial-write abort/retry, retirement headroom, current/older-root
ownership, CRC-valid overlapping extents and actual read-only mounts.
Before-swap failure oracles include old-root bytes, total/roving and the full
reserve array; the partial-write test requires exactly one failed write.

Mounted post-swap tests inject IO and UNSUPPORTED retirement errors after
**synthetically** advancing an uncommitted generation to make an older root.
This is an error-boundary test, not a normally reachable healthy writable
allocation. They require one root write, exact accounting, a sticky recovery
latch, no further sync/public/recursive allocation writes, unchanged working
stock and both full on-disk superblock copies, then abandon and read-only
remount with zero fsck errors, warnings and leaked blocks. An actual mounted
8 MiB append/sync/remount checks every byte and strict fsck. Healthy serialized
writable replacement consumes scratch before current-root retirement and
therefore has stock room; older retirement is preflighted.

Complete normal tests, ASan/UBSan tests, local `make check`, static analysis,
affected allocator/extent/pending/hardware-fault/reuse tests and independent
Luna source/test review pass. The strengthened test source was included in
the final full suites. All 19 fresh guest runs pass the strict verifier.
Final rebuilt normal/probe handlers are byte-identical to the measured ones.
ASan/UBSan is not a LeakSanitizer result on this macOS host.

SHA-256 identities (all measured run handlers are listed in the raw evidence):

- Baseline source: `60946e3`.
- Baseline normal: `9b43efc76d61471f89b4b9bae66e0a6fa1f9dc35d873398b7b3000a69f3afc39`.
- Baseline probe: `b28cfcfd59a29f622d4aa55df9e67756c21920cd0e8e39341398016a0983ddf9`.
- Candidate normal: `197c4faff73e2f7c3e7af60659a7654992417bdc1ace3c5ac4ae53a18a430556`.
- Candidate probe: `f5b907b2de11db8e1d7b045e79837d36a4d224f07e0798a7f10e428154b7bedb`.
- Candidate `alloc.c`: `4c31929f12f88cf080faf742ccc574040bcaf35671fe37bedc9bbc83ed1bafc1`.
- Candidate focused tests: `4db7d6369030b759482e089e422dd936fe68ccb2485322c2e3a078fbed1afff7`.
- Fixed formatter: `0f87a0638791a4a2371083339ed014d4d6659dc85a2270bd993f9df1ddf13cc6`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

[Raw text evidence](evidence/bfs-root-shift-2026-09-30/) preserves all normal
and diagnostic runs, qualification logs, input identities and SHA256SUMS.
Copied trailing whitespace is normalized. No ROM/HDF/licensed binary is
committed. No CI, push, GitHub mutation or physical hardware qualification ran.
`coffin.service` remains stopped for further measurements, not disabled/deleted.
