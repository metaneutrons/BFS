# BFS unpublished Free-Tree root folding — 2026-09-30

## Result and acceptance target

Against `686c4e4`, the candidate cuts the checked small-create mean by 49.8%
and small-delete mean by 53.5%. The agreed target remains **at most 5× PFS3
in every checked AmigaDOS phase, without weaker durability, integrity,
snapshot or recovery guarantees**. It is not achieved. Reads and lookups
remain below 5× in these samples; all three mutating phases remain far above.

Normal handlers, fresh equal-size images, sequential interleaved runs in both
orders, with no concurrent BFS tests, compiler or emulator jobs:

| Handler / order | Create 40 | Lookup 400 | Read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline, BFS first | 578,045 | 50,111 | 23,935 | 432,700 | 269,183 | 499,645 |
| Candidate, PFS3 first | 287,870 | 52,033 | 23,745 | 480,061 | 273,279 | 232,766 |
| Baseline, PFS3 first | 578,685 | 49,728 | 23,617 | 428,030 | 264,702 | 501,245 |
| Candidate, BFS first | 292,926 | 53,248 | 24,705 | 431,165 | 278,591 | 233,024 |
| Baseline mean | 578,365 | 49,919.5 | 23,776 | 430,365 | 266,942.5 | 500,445 |
| Candidate mean | 290,398 | 52,640.5 | 24,225 | 455,613 | 275,935 | 232,895 |
| PFS3 mean in candidate runs | 5,855.5 | 24,608 | 5,024 | 14,464 | 106,592 | 3,008 |
| Candidate / PFS3 | 49.6× | 2.14× | 4.82× | 31.5× | 2.59× | 77.4× |
| 5× ceiling | 29,277.5 | 123,040 | 25,120 | 72,320 | 532,960 | 15,040 |

Times are microseconds. Two samples are not a confidence interval. The two
candidate write samples differ by 11.3%; their mean is 5.9% above baseline.
There is no demonstrated write improvement. Lookup and sequential-read means
are also somewhat higher, and small reads have little margin under the goal.
Do not extrapolate these emulator results to physical hardware or declare
parity. Creation, writing and deletion need approximately 9.9×, 6.3× and 15.5×
further reductions respectively.

## Ownership mechanism

The height-one Free-Tree path requires a CRC/structure-validated node tagged
with exactly the live transaction ID, different from the last published
Free-Tree root. Future tags are corruption, not eligibility. Snapshots do not
share the Free-Tree. The ordinary/deeper-tree fallback remains available.

An owned-root B-tree primitive writes a different replacement block, swaps the
root, then transfers the old unpublished root's ownership to its caller. It
does not append that block to the ordinary reserve or pending queue. The normal
replacement primitive retains its original deferred-retirement behavior.

The allocator prefers an inactive emergency-origin scratch already in reserve,
otherwise stages the last active emergency block. Ordinary reserve blocks and
an ordinary old root are merged into one leaf. An emergency-origin old root is
reactivated after the swap. The candidate eliminates the chain of ordinary
root COWs needed to return the reserve, without an in-place write, dirty cache,
format change, skipped reclamation or relaxed flush policy.

Before-swap failure restores the exact reserve and emergency state only after
checking root, scratch-return order/count and pool bytes. After-swap owners
are never restored as unapplied candidates. The transferred step leaves the
pending queue unchanged. The same proof permits post-publication use on a
new, unpublished root; the 256-pass limit and recovery-required errors remain.

An initial full host run exposed a valid fallback case: range reclamation may
place an *inactive* historical emergency-origin block in an ordinary extent.
The final preflight returns unsupported for unrelated stale-slot aliases,
restores staging, and uses the legacy path. Current-root, staged-reserve and
active-pool aliases remain corruption. The complete scan precedes fallback so
an inactive alias cannot conceal a later live-owner alias. The failed
fill-and-recover assertion is retained separately as rejected evidence.

## Required Amiga memory fix

The first guest prototype could not mount even though its extracted partition
passed the host checker. The old assembly memset stored 12 registers (48
bytes) while advancing 44. The stores overlap; overflow is confined to the
final chunk when fewer than four remainder bytes follow, not accumulated
kilobytes. With the exact handler flags, the new `bfs_fs_t` is 66,880 bytes,
an exact multiple of 44. Its final four-byte overwrite clears the adjacent
`cache.bio.ops` pointer during mount.

The corrected memset stores eleven registers. Both bulk loops count remaining
bytes directly, avoiding the old 16-bit DIVU quotient overflow on large valid
buffers. Memcpy now reloads its original destination for the return value;
its old `a2` destination save was overwritten by MOVEM copy data. All saved
registers remain restored. The unreadable-disk run is retained as rejected,
incomplete BFS evidence, regardless of its completion marker or PFS3 result.

The standalone Amiga probe executes the actual assembly through volatile
function pointers with builtins disabled. It passed 8,512 small memcpy,
4,256 small memset, thirteen large memcpy and thirteen large memset cases:
all lengths 0–132, source/destination offsets 0–7, four fill bytes, canaries,
source immutability, destination return values, 4 KiB, 64 KiB, 66,880 bytes
and DIVU boundary cases around `44 * 65535` / `44 * 65536`. Each large-operation
family checked 28,972,230 payload bytes. This is emulator, not hardware evidence.

## Counters and remaining work

The final diagnostic run passed the strict v8 verifier. Its elapsed times ran
during host qualification and are not used as normal performance samples.
Baseline counters are from the [preceding qualified increment](bfs-mixed-reserve-performance-2026-09-30.md).

| Phase | Baseline / candidate Free-Tree writes | Baseline / candidate reserve-return writes | Baseline / candidate publications |
| --- | ---: | ---: | ---: |
| Create 40 | 1,790 / 692 | 1,127 / 80 | 135 / 120 |
| Write 8 MiB | 1,084 / 1,035 | 47 / 2 | 4 / 3 |
| Delete 40 | 1,502 / 516 | 1,063 / 80 | 135 / 120 |

Creation and deletion each use 80 one-write batches and zero ordinary reserve
return runs. Maximum post-publication passes are two. Writing still emits
1,423 B-tree writes: 1,035 Free-Tree, 258 inode and 128 extent-tree writes.
The Free-Tree split is 644 allocation-body, one refill, two return, four
post-publication and 384 other writes. The next experiment targets the
remaining full-block CRC cost with exact zero-run state advancement over
canonically zeroed unused write bytes. Read CRC coverage must stay full-block;
namespace/allocation multi-edit reductions remain necessary candidates.

## Verification and provenance

Final `make -j4 check` and the complete ASan/UBSan host suite passed. Focused
B-tree tests are 27/27 and hardware-failure tests 47/47. Added tests exercise
old-root ownership transfer, exact/future tags, expected-root mismatch,
full pending headroom, write abort, ordinary/emergency accounting, retry,
committed identity, missing scratch, inactive-alias fallback and active
selected-scratch corruption. Existing snapshots, crash cuts, low-space,
pending storms, remount, checker and handler fill/recovery cases passed.
Independent Luna review covered the engine, allocator and assembly; test
authors did not act as independent reviewers of their code.

All four normal runs and the final diagnostic run passed mounted-volume,
completion, contents and counter checks. All images were freshly formatted
with the same current host formatter for both normal handler versions. The
environment matches the earlier reports: FS-UAE 3.2.35, A1200/68040 maximum
speed, Kickstart 47.102, Workbench 47.2, equal 256 MiB HDFs and unchanged PFS3.
No remote CI, physical media, power-cut or controller-lie qualification ran.

SHA-256 identities:

- Baseline normal: `3578657dd113747b954e2b2ae5d8a2e04de8bb7eb7809144acb3b78c24684379`.
- Candidate normal: `5426e843bae1b46b969c9b72d686b52d21fdd13247f1e3b8e976d3c404843891`.
- Candidate probe: `25b9a9e0b7d36b5c3f3f708cb33107a6cb7c4833a208c5274853a2b3336e846e`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- Memory probe: `d99bdde5f6f014211eac1620a8af454b82f6b45b3b3461047cdc21ae1ebac5b0`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

The [raw evidence](evidence/bfs-root-fold-2026-09-30/) contains 49 files covered
by SHA256SUMS, including the rejected mount/assertion cases and final passing
host logs. Log copies have trailing whitespace removed; other files
are unchanged. No licensed binary, ROM or HDF is committed.
