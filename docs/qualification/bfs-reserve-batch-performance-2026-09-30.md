# BFS height-one reserve-return batch — 2026-09-30

## Result

A bounded reserve-return batch reduced the uninstrumented FS-UAE time for
creating 40 small files by 26.9% and deleting them by 27.6% against the
pre-change handler. Both workload orders passed the strict, data-checked
verifier. These are two samples per handler, not a statistical confidence
interval or a hardware qualification.

| Handler and order | Create 40 (µs) | Write 8 MiB (µs) | Delete 40 (µs) |
| --- | ---: | ---: | ---: |
| Baseline `ed6daf1`, PFS3 first | 966,138 | 488,060 | 860,155 |
| Baseline `ed6daf1`, BFS first | 944,571 | 494,717 | 853,243 |
| Final candidate, BFS first | 694,332 | 460,477 | 611,133 |
| Final candidate, PFS3 first | 702,459 | 488,766 | 629,500 |
| Baseline mean | 955,355 | 491,389 | 856,699 |
| Final mean | 698,396 | 474,622 | 620,317 |
| Change in mean | −26.9% | −3.4% | −27.6% |

The sequential-write difference is too small and variable to attribute to
this change. The two earlier prototype runs (722,364/640,317 µs and
704,764/635,262 µs for create/delete) are retained as separate corroborating
evidence; they preceded a source-level helper extraction and are not mixed
into the final means. The baseline runs were interleaved between these early
prototype runs; the final-code validation followed afterward. Temporal drift
is therefore not completely controlled, although both orders were measured
for each handler.

PFS3 averaged 5,728 µs for creation and 2,944 µs for deletion in the two
final runs. BFS remains roughly 122× and 211× slower on these small-file
workloads. This change is material but does not approach parity.

## Mechanism and safety boundary

For a height-one Free-Tree with at least three reserve blocks, the allocator
sorts all but one reserve block and attempts to return them through one private
root-leaf replacement. One block remains outside the batch as COW scratch.
The existing sorted-block merger rejects an oversized or unsupported leaf
before a root swap; in that case the original reserve is restored and the
ordinary return loop runs. A failed read, allocation or node write before the
swap restores the original reserve and returns the error. An error after a
swap is propagated, not hidden by fallback. Deeper trees use the unchanged
ordinary path. The global reserve floor is temporarily lifted as before, so
the batch does not recursively refill its own scratch pool.

The independent deep-compare probe showed the expected reduction in Free-Tree
node writes:

| BFS phase | Prior total / reserve-return writes | Candidate total / reserve-return writes | Successful batches / blocks / batch node writes |
| --- | ---: | ---: | ---: |
| Create 40 | 3,029 / 2,281 | 2,230 / 1,570 | 23 / 437 / 23 |
| Write 8 MiB | 1,089 / 53 | 1,084 / 47 | 1 / 19 / 1 |
| Delete 40 | 2,660 / 2,223 | 1,987 / 1,552 | 23 / 437 / 23 |

The prior counters come from the two checked v6 runs in
[`bfs-reserve-commit-profile-2026-09-30.md`](bfs-reserve-commit-profile-2026-09-30.md).
The candidate's v7 run records ordinary-run and batch-node writes separately
and verifies that their sum equals reserve-return writes in every phase. The
23 batches in each small-file phase used one Free-Tree node write each.
Instrumented elapsed times are retained for diagnostics only and must not be
compared with the uninstrumented table above.

## Verification and provenance

`make check` passed, including the allocator, crash-injection, pending-free,
and hardware-failure suites. The full host-test suite also passed under
AddressSanitizer and UndefinedBehaviorSanitizer in a separate build directory.
New deterministic hardware-failure tests use a fragmented height-one fixture
to check successful batch accounting, first-write rollback and retry, and a
second-write failure after the batch replacement. The latter retains the new
private root and exact block ownership before a later successful retry. The
fixture's complete return takes four BIO writes because the retired root requires
further settlement; the single-write figure above counts only the batch
replacement itself. The focused deep-compare verifier suite passed, including
v7 mutation tests and v4–v6 compatibility. The normal and probe m68k handlers
and the guest benchmark tool built with `-Werror`. All six uninstrumented
fresh-image FS-UAE runs and the candidate deep-compare run passed the strict
workload verifier. Both volumes mounted after format, and both guest workloads
reported `PASS`.

The emulator was FS-UAE 3.2.35, A1200/68040 at maximum emulated speed,
Kickstart 47.102 and Workbench 47.2. The pre-format inventory reports DH2 as
not a DOS disk; the post-format inventory confirms PFS3 mounted read/write.
The normal baseline handler SHA-256 was
`d7568afad5654f3260c89f16c22194bd5699dc25a8b2dce1d6c1a4e762e1fccf`;
the final candidate was
`3256c449edb62ad72eef8885f86d8ed1970ff5a6fa6f019ac5ca1422e06d7ee7`.
The candidate probe handler was
`2aebd6d13baeebac27aba1612bc2429d93331c961d7bd757c44a38a9e50e90a6`,
the guest compare tool was
`30cd8520d2b6d025b5de12a167c3d76175f89b83ed12d9ea10f2e62955184619`,
the baseline guest tool was
`5ff3caa207b995a4c3983ebeda255d9da8e67aafa844ddac5078bd2ada9e3756`,
and PFS3 was
`bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

The [raw evidence](evidence/bfs-reserve-batch-2026-09-30/) preserves the two
baseline runs, two preliminary prototype runs, two final uninstrumented runs,
and one candidate deep-compare run. Each includes guest output, completion
marker, before/after machine information, emulator configuration and log.
`SHA256SUMS` covers every retained file. Log copies differ from their run
directories only by removal of trailing whitespace. No HDF, ROM or licensed
binary is committed. No remote CI, real hardware, physical power interruption,
or dishonest controller-flush test was run here.
