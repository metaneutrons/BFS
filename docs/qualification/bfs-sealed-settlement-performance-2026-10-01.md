# Sealed pre-publication settlement — 2026-10-01

## Result and acceptance target

The target remains **≤5× PFS3 in every checked AmigaDOS workload**, with
unchanged durability, integrity, snapshot and recovery guarantees. It is
**not achieved**. This is a bounded, locally tested structural checkpoint,
not final performance acceptance.

Eight normal-handler runs use four fresh images per version, interleaved in
both filesystem orders. Every sample, including adverse results, is retained.
Times are microseconds; the ratio of means is not a per-run acceptance gate.

| Workload | Baseline BFS mean | Candidate BFS mean | Change | Candidate PFS3 mean | Ratio of means | Candidate range | Runs >5× |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 435408.75 | 335166 | -23.0% | 17727.75 | 18.91× | 17.69–20.52× | 4/4 |
| Lookup 400 | 136366.5 | 136027.75 | -0.2% | 71984.25 | 1.89× | 1.71–2.29× | 0/4 |
| Read 40 | 63904 | 64591.5 | +1.1% | 18383.75 | 3.51× | 2.09–6.37× | 1/4 |
| Write 8 MiB | 507005.25 | 540029 | +6.5% | 50863.5 | 10.62× | 6.88–16.30× | 4/4 |
| Read 8 MiB | 658092 | 644700.75 | -2.0% | 307630.25 | 2.10× | 1.86–2.31× | 0/4 |
| Delete 40 | 395726.5 | 235678.25 | -40.4% | 8223.75 | 28.66× | 25.32–32.25× | 4/4 |

Create and delete are faster in all four order-matched pairs. Large write is
slower in three of four pairs and by 6.5% in the means; this adverse result is
not removed or explained away. The small-read 6.37× sample also fails the goal.
PFS3 large-write samples of 55872 and 74624 µs lower the apparent candidate
ratio and remain included. Four samples do not establish a confidence interval,
precise universal gains or absence of a material regression. The checkpoint
is retained for its small-file benefit and bounded ownership proof; the
large-write cost remains unresolved and requires further optimization.

The [measurement summary](evidence/bfs-sealed-settlement-2026-10-01/measurement-summary.md)
records every raw tuple, paired delta and diagnostic count. The baseline is
the [immutable nibble CRC checkpoint](bfs-crc-nibble-performance-2026-09-30.md),
commit `f02a8b9`. Cachy, FS-UAE 3.2.35, A1200/68040, Kickstart 47.102,
Workbench 47.2 and equal fresh 256 MiB images are unchanged. No other guest
emulator or compiler ran during the measured guest runs. The physical KVM
host's scheduler is not controlled; this is not a hardware qualification.

The fixed formatter produces options **0**, as in the preceding comparisons.
These guest timings are not a data-checksum-enabled benchmark. The candidate
does not disable a feature: separate checksummed, ordered-data fixtures cover
its eligibility, full contents, remount, accounting and persistence behavior.

## Ownership change and durability boundary

At the serialized full-filesystem commit boundary, an eligible mounted,
writable, no-snapshot, height-one Free-Tree now prepares one private replacement
leaf containing the existing free extents, pending retirements, ordinary reserve
stock and the old unpublished current root. Historical emergency-origin
blocks return only to their pool. A distinct, validated pool block supplies
COW scratch. Existing codecs and the owned-root replacement API remain the
single implementation of leaf layout and root transfer.

The full candidate set, all 32 pool slots, reserve, working roots, old roots
and both superblock placements are checked before staging. Duplicate ownership
is corruption, not deduplication. Unsupported shape, snapshot/refcount or
legacy extent contexts and absent scratch decline before writes. Read/I/O
errors do not become permission to fall back after an attempted operation.

Once the root swaps, allocation is frozen and pending ownership stays recorded.
A successful metadata fence makes the complete COW graph durable **before**
the matching superblock can be written. The unchanged superblock write-and-sync
then publishes that graph. Only its success clears pending ownership and
unfreezes allocation. Existing ordered-data preflush and final sync remain.
The eligible path has no post-publication reclaim tail; other contexts retain
the established commit loop. An error after sealing latches recovery and
prevents later allocation or mutation.

A fresh writable mount also syncs its selected readable state before allocation
or mount-time recovery. This handles a valid newer superblock left only in
volatile storage by a failed publication flush. Failure leaves the filesystem
unmounted; read-only inspection does not sync or assert durability.

The metadata fence is necessary, not an optional latency tradeoff. The retained
[counterprobe](evidence/bfs-sealed-settlement-2026-10-01/sf-fence-counterprobe.log)
removes only that fence in an isolated negative build. An SB-only failed
publication flush then selects a new superblock whose graph/data oracle fails.
The production build passes the same power-cut and strict-fsck oracle.

The two qualified probe runs confirm the structural change. Counts are
baseline → candidate; they are not normal-handler latency estimates.

| Phase | Metadata node writes | Free-Tree writes | Physical BIO writes | SB publications | Reclaim passes | Candidate sealed commits / fences |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 873 → 695 | 632 → 451 | 1033 → 775 | 120 → 40 | 80 → 0 | 40 / 40 |
| Write 8 MiB | 431 → 426 | 146 → 141 | 2482 → 2475 | 3 → 1 | 2 → 0 | 1 / 1 |
| Delete 40 | 605 → 313 | 489 → 197 | 725 → 353 | 120 → 40 | 80 → 0 | 40 / 40 |

The write phase still issues **2048 single-block payload writes** and 128
extent-run mappings. Its five-node reduction is small. Create/delete still
have 282/116 allocation-body Free-Tree writes; the next bounded allocator
experiment must preserve the full recursive reserve floor and disable its
new policy throughout legacy settlement. These counts identify remaining
work, not a forecast that either change will achieve 5×.

## Tests and scope

Three new host fixtures cover preflight/ownership, buffered persistence and
natural convergence. The persistence fixture cuts every actual read, write and
sync in the eligible commit, includes partial leaf and both SB-slot writes,
and separates volatile acknowledgement from durable media. Failed flushes
can drop writes, persist all, persist only SB blocks or persist one non-SB block.
It checks old-or-new complete file contents, strict fsck with zero errors,
warnings and leaks, sticky recovery, exact rollback and no subsequent mutation.
Same-BIO remounts exercise successful/failed writable mount barriers and
non-mutating read-only inspection.

The convergence fixture witnesses **72/72 sealed commits** across repeated
create/write, overwrite and delete cycles at both 1 KiB and 4 KiB, with
checksums and ordered data. Each explicit commit has one frozen metadata fence
and one SB write-and-sync, retains pending ownership while frozen, and performs
no non-SB write in that interval. Each stage remounts read-only and passes full
data and strict fsck checks; deletion returns to exact initial free accounting.

Full normal and ASan/UBSan runs pass **425 tests in 43 suites**. Local repository,
quality, static-analysis and secret gates pass. Normal/probe handlers and both
probe consumers cross-build with warnings treated as errors. Linux actual
FUSE mount tests, 20 conformance tests and FUSE static analysis pass. The Linux
source copy precedes the final diagnostics-only counters; the rebuilt normal
Amiga handler remains byte-identical. LeakSanitizer is unsupported on this
macOS runtime and is not claimed. Commands, findings and test identities are
in the [review notes](evidence/bfs-sealed-settlement-2026-10-01/review-notes.md).

Selective-persistence qualification here covers the eligible sealed path,
not arbitrary selective persistence of the legacy fallback. Snapshots and
unsupported shapes still run the existing tested path. Real power cuts,
controller cache/flush lies and individual media require separate,
device-bound qualification. No format migration or CLI change is involved.
No CI, push, PR, merge or other GitHub mutation was performed.

## Diagnostics and retained inputs

The old schema-8 candidate diagnostic completed its guest data checks but
failed the strict verifier: that schema requires ordinary reserve-return calls
for every commit. It is retained as a **rejected diagnostic attempt**, not
qualified evidence. Its raw output and the old schema rules were not changed.

Probe ABI 10 appends sealed-commit and successful metadata-fence counters;
deep schema 9 emits them. The normal workload and schema 1 are unchanged.
The schema-9 verifier retains the earlier bucket, run, batch, skip and
publication relations, accounts exactly for ordinary returns plus sealed
commits against transactions plus legacy reclaim passes,
requires matching fences and sufficient BIO updates, and bounds reclaim
passes by unsealed commits only. Negative tests cover malformed/missing/
duplicate counters and inconsistent accounting. A fresh ABI-10/schema-9 image
qualifies the candidate; the baseline keeps its correctly paired ABI-9/schema-8
tool and passes its original strict checks. Common counters remain comparable.

Normal baseline handler SHA-256 is
`031b76eacf998a52e249cbfcea15ae20253c74e0243174de8951fc879a22ced9`;
normal candidate is
`f3b593adb3f377d4ce755f4d08c6e378fb7bb4d0d41fdad2d86a900280ab783c`.
Both final and pre-counter normal builds match that candidate. It is 132252
bytes versus 128232 (+4020). Probe, guest-tool, formatter, PFS3, source and test
identities accompany the raw text/configuration and logs. Licensed ROMs,
Workbench binaries and disk images are not committed. The evidence directory's
relative SHA-256 manifest is checked after the final copy.
