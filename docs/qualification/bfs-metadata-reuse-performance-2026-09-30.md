# Unpublished metadata reserve reuse — 2026-09-30

## Result and acceptance target

The target is ≤5× PFS3 in **every** checked AmigaDOS workload without weakening
durability, integrity, snapshots or recovery. It is **not achieved**.
The retained warmup revision reduces the 8 MiB write mean by 16.0% relative
to `cc6e08f` in this comparison. Create/delete means change by −6.1%/−2.7%;
lookup/small-read/sequential-read means change by +3.5%/+5.8%/+1.1%.
Four samples per version and uncontrolled host scheduling do not establish
precise gains or a general absence of regressions.

Times below are microseconds. A and B are serial interleaved batches, with
each version measured in both filesystem orders on fresh equal 256 MiB images.

| Normal run | Create 40 | Lookup 400 | Read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A: baseline-bfs-first | 447614 | 130431 | 63360 | 584125 | 585084 | 396990 |
| A: candidate-pfs3-first | 467735 | 126143 | 62400 | 585469 | 590460 | 397181 |
| A: baseline-pfs3-first | 576191 | 138560 | 60800 | 763196 | 720444 | 456510 |
| A: candidate-bfs-first | 531708 | 127231 | 83328 | 522814 | 670716 | 389502 |
| B: candidate-bfs-first | 549500 | 159039 | 61376 | 526802 | 649085 | 390974 |
| B: baseline-pfs3-first | 535548 | 124480 | 58688 | 580220 | 641917 | 385021 |
| B: candidate-pfs3-first | 450046 | 125248 | 56257 | 500413 | 629884 | 401918 |
| B: baseline-bfs-first | 570300 | 126079 | 66174 | 616062 | 565117 | 384892 |
| Baseline BFS mean | 532413.25 | 129887.5 | 62255.5 | 635900.75 | 628140.5 | 405853.25 |
| Candidate BFS mean | 499747.25 | 134415.25 | 65840.25 | 533874.5 | 635036.25 | 394893.75 |
| Candidate PFS3 mean | 19920.25 | 70063.5 | 16080 | 46271.75 | 263934.75 | 7871.5 |
| Candidate / PFS3 means | 25.09× | 1.92× | 4.09× | 11.54× | 2.41× | 50.17× |
| Candidate vs baseline | -6.1% | +3.5% | +5.8% | -16.0% | +1.1% | -2.7% |

Create, write and delete fail 5× in every candidate run. Their per-run
ranges are 18.13–33.67×, 8.58–15.83× and 49.48–51.48×. Lookup and sequential
read pass in every candidate run. Small read averages 4.09×, but the
83,328 µs candidate sample is **5.47×**, so this phase is not universally
qualified either. The 763,196 µs baseline write and all PFS3 samples remain
in the evidence. Ratios of means are not a substitute for the per-run gate.

Cachy remains the same KVM guest and FS-UAE configuration as the
[resident-validation comparison](bfs-validation-cache-performance-2026-09-30.md).
Guest measurements are serial; no competing emulator or guest compiler ran.
Physical-host scheduling and other VM workloads are not controlled.
These are software/emulator observations, not hardware parity or a confidence
interval. The formatter is fixed across every baseline and candidate image.

## Mechanism and proof boundary

Only the top-level B-tree single-block allocator interface participates.
Caller-owned retired unpublished metadata and aborted scratch may enter the
existing volatile reserve without a Free-Tree write. Later metadata COW may
consume an ordinary suffix spare strictly above the entire height-dependent
refill target. The protected recursion prefix is not spent. All 32 historical
emergency slots, including inactive slots, are excluded from ordinary reuse.

Eligibility requires a mounted filesystem, working and committed superblocks,
a height-one Free-Tree, exact equality of its live ID with the newer working
superblock ID, and both snapshot controls clear. Explicit owner-state pointers
exclude format/standalone use. The runtime `has_snapshots` guard also excludes
snapshot creation on an optionless volume; testing found that checking only
the option bit was insufficient. Older/shared metadata remains deferred.
Data/multi-block allocation and Free-Tree recursion are unchanged.

Reuse begins after eight eligible top-level metadata allocation **requests**
in one live transaction. The volatile counter saturates and resets on a new
live ID. This experimentally selected amortization policy is not an ownership
proof or an optimality claim. Before warmup, the interface uses the existing
path. Earlier warmed transactions can still change later free-space
fragmentation and settlement history.

Shared validation helpers check range, stock duplication, reserve/active-pool
separation and existing free extents. Actual stash/pop also reject the live
Free-Tree root and committed roots. Ownership of other immediately reclaimable
storage is supplied by the existing B-tree deallocation contract, not inferred
from block bytes: abort scratch can be unwritten or partially written.
This change does not re-prove or alter the existing B-tree tag-retirement rule.
Unsupported contexts, no ordinary suffix spare and full stock retain the
legacy fallback. Validation and I/O errors are propagated, not concealed.

Stash changes reserve stock, not Free-Tree accounting; pop consumes one entry.
Existing reserve return, root fold, flush ordering and publish/reclaim
fixed-point settlement remain. There is no on-disk format, CLI, cache
write-back, durability or snapshot-policy change.

## Rejected prototypes and attribution

All prototype evidence is retained, not just the selected revision.
The initial unbounded version checked stock even when no suffix spare existed.
Its four normal runs and diagnostic are stored under `initial-*`.
The next version first fell back when stock was at/below the floor, but still
reused immediately. Eight normal runs and a diagnostic are under
`unbounded-*`. Its baseline/candidate means for create/write/delete were
482,221.25/522,701.5, 609,272.25/533,245 and 404,093.75/431,915 µs:
write improved 12.5%, while create/delete worsened 8.4%/6.9%.
That revision was **rejected**. The final nine runs are under `warmup-*`.

The retained diagnostic passed the strict v8 verifier. Create/write/delete
write 884/563/632 metadata nodes, including 638/274/516 Free-Tree nodes.
The large write has 159 inode nodes, 128 extent nodes, 2,048 data blocks and
2,614 BIO writes. Its Free-Tree attribution is 264 allocation-body,
one refill, two reserve-return, four post-publication and three other writes.
Against the prior diagnostic, Free-Tree writes decrease 839→274 (67.3%),
all metadata 1,129→563 (50.1%), and BIO writes 3,180→2,614 (17.8%).
These are structural counts; instrumented elapsed times are not normal-handler
performance evidence.

Create Free-Tree attribution is 284 allocation, 40 refill, 80 reserve-return,
144 post-publication and 90 other writes. Delete is 116/40/80/279/1.
Small-phase Free-Tree counts remain close to or above the previous 633/486;
earlier allocation history and timestamp-dependent metadata can change them.
Superblock publications remain **120/3/120**. The remaining allocation-side
delete/reinsert pair and small-operation settlement cycles are separate
optimization candidates; neither is solved by reserve reuse.

## Verification and provenance

Nineteen focused tests cover write-free stash/pop, the whole floor, all-slot
emergency exclusion, duplicate/overlap/protected roots, read faults,
ineligible/low-stock/full/deeper contexts, warmup saturation/reset, actual
partial-write abort scratch recovery, repeated append/unaligned overwrite/
commit/remount and both optionless and snapshot-enabled volumes.
The partial-write B-tree test preserves old-root bytes and exact reserve,
Free-Tree accounting and superblock state, then retries using the same owned
scratch.

The warmed mounted test naturally reaches eight requests and an ordinary
suffix spare with four unsynced 64 KiB appends. It cuts writes 1–64 in the next
append, verifies the exact injected write index (including partial writes),
unchanged published roots and both complete on-disk superblock copies, then
abandons without syncing failed working state. Read-only remount has an empty
committed file and strict fsck with zero errors, warnings and leaked blocks.
The range contains injected failures and successful cuts beyond the operation.
The 512 KiB snapshot test checks both live replacement and historical data
after read-only remount in both format modes.

Five namespace write-cut tests also run with both zero options and the
snapshot option, retaining their original recovery oracles. Their 256 combined
cuts each finish with strict read-only remount/fsck; these short transactions
usually remain on the legacy path under warmup and are **not** the warmed
fast-path witness. Complete normal and ASan/UBSan suites, local `make check`,
independent Luna review, and normal/probe m68k builds pass.

All 23 fresh guest runs pass strict mount, completion and data-verification
checks. Final rebuilt production binaries match the measured identities.

SHA-256 identities:

- Baseline normal: `ea683c9362a0bcc159124bbd40517563ddaa9b061cb2e35843562541abc1c207`.
- Retained candidate normal: `9b43efc76d61471f89b4b9bae66e0a6fa1f9dc35d873398b7b3000a69f3afc39`.
- Retained candidate probe: `b28cfcfd59a29f622d4aa55df9e67756c21920cd0e8e39341398016a0983ddf9`.
- Unbounded candidate normal: `8be39abc5ade675aab511b815ec8f37a3ddc8c18d8b53938e7c6953f394e2240`.
- Unbounded candidate probe: `1bc5805bc1a4f89f1912738cb29487d7b91f25fd4e212b749ecc8229c1447158`.
- Initial candidate normal: `109b8422b49a65f26e3c5957a92ac3516bd022d04d4f4d8be13549f491c5a407`.
- Initial candidate probe: `565dfe7c7a09774cdd2e65f6ecfbe8f1344f938c36216f38bad8de5ea4114ab0`.
- Fixed formatter: `0f87a0638791a4a2371083339ed014d4d6659dc85a2270bd993f9df1ddf13cc6`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

[Raw text evidence](evidence/bfs-metadata-reuse-2026-09-30/) includes all
prototype/final runs, local qualification logs and SHA256SUMS. Copied log
trailing whitespace is normalized. No ROM, HDF or licensed binary is committed.
No CI, GitHub mutation or physical power-cut/controller/media qualification
ran. `coffin.service` remains stopped for further measurements; it was not
disabled or deleted.
