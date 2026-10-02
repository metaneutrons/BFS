# BFS mixed reserve-return batch — 2026-09-30

## Result and acceptance target

The candidate reduces the checked small-create mean by 17.3% against
`7d743c8`. Small-delete time is lower in both orders; the mean reduction is
33.1%, but a high baseline delete sample makes that percentage uncertain.
The 8 MiB write is effectively unchanged. The agreed target is **at most 5×
PFS3 in every checked AmigaDOS phase, without weaker durability, integrity,
snapshot or recovery guarantees**. It is not achieved.

All times below are microseconds from normal, uninstrumented handlers. Each
row used fresh images; runs were sequential, interleaved baseline/candidate,
with no BFS builds or host tests running concurrently.

| Handler / filesystem order | Create 40 | Lookup 400 | Read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline, BFS first | 694,205 | 49,537 | 23,807 | 430,782 | 261,952 | 622,780 |
| Candidate, PFS3 first | 576,317 | 49,152 | 23,743 | 433,469 | 264,782 | 484,862 |
| Baseline, PFS3 first | 718,459 | 50,177 | 24,000 | 443,006 | 271,679 | 859,195 |
| Candidate, BFS first | 591,613 | 50,560 | 24,320 | 438,654 | 267,967 | 506,557 |
| Baseline mean | 706,332 | 49,857 | 23,903.5 | 436,894 | 266,815.5 | 740,987.5 |
| Candidate mean | 583,965 | 49,856 | 24,031.5 | 436,061.5 | 266,374.5 | 495,709.5 |
| PFS3 mean in candidate runs | 5,952 | 24,447 | 4,895.5 | 14,271 | 107,455 | 3,072 |
| Candidate / PFS3 | 98.1× | 2.04× | 4.91× | 30.6× | 2.48× | 161.4× |
| 5× acceptance ceiling | 29,760 | 122,235 | 24,477.5 | 71,355 | 537,275 | 15,360 |

There are two samples per handler, not a confidence interval. The two baseline
delete samples differ substantially; do not treat the mean reduction as a
precise forecast. Lookup and read differences are small and not attributed to
this optimization. Small reads currently have little margin under the target.
The three mutating phases still need approximately 19.6×, 6.1× and 32.3× further
reductions respectively. No parity or universal 5× claim follows from this run.

## Mechanism and fixed-point boundary

The earlier root-leaf batch rejected a complete reserve whenever any block
matched any of the 32 emergency-pool slots. In the new baseline diagnostic,
that excluded 71 create-phase returns and 70 delete-phase returns. There were
no tree-shape or leaf-capacity fallbacks in these phases.

Before the first superblock publication, the candidate partitions the reserve.
Ordinary blocks are sorted and returned through one atomic private root-leaf
replacement. Emergency-slot blocks remain outside that batch, including blocks
matching inactive slots. One ordinary block is retained last as the batch's
LIFO scratch allocation. Subsequent ordinary cleanup may itself use an
emergency block as a COW node; a retained slot is therefore not necessarily
reactivated by the end of the complete return. The tests check the actual
remaining root owner, not only the aggregate free count.

Pre-swap errors restore the exact original reserve only after checking that
the root and retained stack are unchanged. Errors after a swap propagate;
already applied candidates are not restored or freed a second time. The
global reserve floor is restored on failure. Existing deeper-tree and
capacity fallbacks remain in place. The on-disk format and synchronous
write-through behavior do not change.

Enabling mixed batches in every post-publication pass was rejected: existing
mkdir/refill fault tests reached `BFS_ERR_AGAIN` at the 256-pass settlement
limit, and the first guest experiment did not complete deletion. The run was
stopped and is retained as **rejected, incomplete evidence**, not a benchmark.
The final `bfs_freespace_settle_reserve` entry point uses the same implementation
with mixed batching disabled, preserving the previously qualified tail policy.
It does not skip reclamation, increase the iteration limit, or suppress errors.

## Counter evidence

The v8 deep-compare verifier checks that successful batches plus all classified
fallbacks equal reserve-return calls, as well as the existing phase and
ordinary-run/batch write sums. ABI version 9 adds diagnostic fields only.

| BFS phase | Baseline Free-Tree writes / reserve-return writes | Candidate Free-Tree writes / reserve-return writes | Baseline / candidate batches | Baseline / candidate publications |
| --- | ---: | ---: | ---: | ---: |
| Create 40 | 2,230 / 1,570 | 1,790 / 1,127 | 23 / 49 | 134 / 135 |
| Write 8 MiB | 1,084 / 47 | 1,084 / 47 | 1 / 1 | 4 / 4 |
| Delete 40 | 1,987 / 1,552 | 1,502 / 1,063 | 23 / 53 | 133 / 135 |

Candidate batches returned 904 blocks during creation and 978 during deletion
using 49 and 53 Free-Tree node writes. The remaining ordinary paths still issued
883 and 823 return runs. Maximum post-publication passes remained three.
Instrumented elapsed times are not used for the performance comparison.

## Verification and provenance

`make check` passed. The full host suite passed with AddressSanitizer and
UndefinedBehaviorSanitizer in `build/mixed-reserve-sanitize`; the final stronger
root-owner assertions were then rebuilt and passed in both normal and sanitized
hardware-failure suites (39/39). Three added tests cover mixed success with two
inactive emergency slots, exact first-write rollback including the pending
block and retry, and second-write failure after the batch swap with exact
remaining ownership and retry. Existing mkdir/refill fault cases, pending-free
storms, snapshot tests, crash injection and fsck tests passed unchanged. An
independent Luna review reran the hardware-failure binary in an isolated
temporary directory and approved the final scoped change.

Both diagnostic comparisons and all four idle normal comparisons passed the
strict guest-data verifier, including completion and mounted volumes after
format. The excluded contended normal run also passed functionally, but ran
during host compilation and is not included in any performance mean.

The emulator and licensed assets match the
[previous qualification](bfs-reserve-batch-performance-2026-09-30.md): FS-UAE
3.2.35, A1200/68040 at maximum emulated speed, Kickstart 47.102, Workbench 47.2,
and equal fresh 256 MiB HDFs. The pre-format DH2 not-DOS message is expected;
the retained post-format inventory confirms the PFS3 volume is mounted.

SHA-256 identities:

- Baseline normal handler: `3256c449edb62ad72eef8885f86d8ed1970ff5a6fa6f019ac5ca1422e06d7ee7`.
- Candidate normal handler: `3578657dd113747b954e2b2ae5d8a2e04de8bb7eb7809144acb3b78c24684379`.
- Baseline diagnostic handler: `2208730bf589c512a8c74396cddf7437e0fed1e0508a4ff535762dbee04b80d3`.
- Candidate diagnostic handler: `9bc8059c805de66a86055da92d9182b3d16e95170db8b37b855c540a5ed1c01f`.
- Guest compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3 handler: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

The [raw evidence](evidence/bfs-mixed-reserve-2026-09-30/) contains six qualified
runs, one excluded contended run, and four files from the rejected incomplete
prototype. `SHA256SUMS` covers the 53 retained files. Log copies have trailing
whitespace removed; other copies are unchanged. No ROM, HDF or licensed binary
is committed. No remote CI, physical hardware, power-cut or controller-lie
qualification was run.
