# Bounded free-tree leaf batching — 2026-09-29

## Result

An atomic, height-one free-tree batch reduces the checked 40-file delete
workload, but does not close the BFS/PFS3 gap. The implementation merges the
sorted post-publication pending blocks with the existing root-leaf extents and
writes one replacement leaf. Deeper trees, full leaves and emergency-pool
blocks use the existing range-free path. Only the initial reclaim pass may use
the batch; the established loop settles the root it retires. The on-disk
format is unchanged.

The two uninstrumented, fresh-image, order-reversed FS-UAE runs gave these
mean times. The baseline is the previously qualified reserve-batching version
in `bfs-free-tree-quickwins-2026-09-29.md`. The candidate runs preceded a
mechanical B-tree helper extraction in the final source; they are performance
evidence for the same algorithm, not final-binary qualification.

| Phase | Baseline BFS (µs) | Candidate BFS (µs) | Change | Candidate PFS3 (µs) |
| --- | ---: | ---: | ---: | ---: |
| 40 × 1 KiB create | 2,957,648 | 2,917,680.5 | −1.4% | 5,504.5 |
| 8 MiB sequential write | 1,225,561 | 1,236,888.5 | +0.9% | 13,919.5 |
| 40 small-file deletes | 2,926,319.5 | 2,584,305.5 | −11.7% | 2,879.5 |

The delete-phase BFS/PFS3 ratio remains about 897× for this workload. Two
runs per version are not a confidence interval. The create and 8 MiB timing
differences are small relative to observed emulator variation; neither is a
credible improvement claim.

## Mechanism and exploratory variants

The debug-only deep-compare run with batching enabled from two pending blocks
measured 2,660 free-tree node writes for 40 deletes versus 2,839 at baseline
(−6.3%). Create writes rose from 3,002 to 3,029, and the 8 MiB phase from
1,086 to 1,089. A threshold of four produced 3,053 / 1,089 / 2,660 writes;
at eight the three counts were exactly the baseline 3,002 / 1,086 / 2,839.
The last run shows that the batch was not exercised in this workload at that
threshold. The implementation retains the natural minimum of two blocks.

The input is validated for order, uniqueness, bounds and pool overlap. A
bounded in-memory merge rejects overlap with existing free extents and joins
adjacent ranges. The B-tree engine then writes a private replacement node and
publishes its root only after successful I/O. An allocation or write error
leaves the old root readable; failures after the first superblock publication
require remount/recovery, as in the existing transaction protocol. Replacing
the root can itself add an older root to pending frees, so subsequent passes
retain the existing non-batched convergence path.

## Correctness and limits

The new tests cover canonical extent merging, exact accounting, duplicate and
overlap rejection, full-leaf fallback, unsupported deeper trees, a failed
replacement write with unchanged root and accounting, remount/fsck, and every
simulated write cut in a six-file delete transaction. The crash test requires
an entirely old or entirely deleted namespace and repairable-only leaks. Existing
pending-free storms, snapshot tests and hardware-failure suites also pass.
The fault tests do not exhaust every boundary around reserve refill and
deferred old-root enqueue, nor a free-sink error after root publication;
these remain explicit recovery-test targets for the multi-level increment.
`make check` passed on the final core code. The focused new hardware-fault
test passed after it was added; the subsequent full AddressSanitizer and
UndefinedBehaviorSanitizer suite passed. The full `make ci-test` run passed
46/46 Amiga integration checks using the pre-Codacy normal handler, SHA-256
`6d90adf951d29917da38816478c0ee93cce206462284e9a0d35d416a2cde539a`.
`make coverage` passed with 89.4% core line coverage against the 85% gate.
The final probe handler, SHA-256
`487ae8e74d6a633dafe4bce6a926fb78d3f9ac30a7a47c3d50169abe9bcb765c`,
passed a fresh PFS3-first deep-compare run and exactly reproduced the
3,029 / 1,089 / 2,660 free-tree write counts. A separate fresh BFS-first
compare run with that pre-fix normal handler passed the verifier and measured
2,972,015 µs for create, 1,242,810 µs for the 8 MiB write, and 2,613,171 µs
for delete. This one pre-fix validation is excluded from the paired means
above. The guest comparison tool has SHA-256
`e4702d5e306a1598bc0a0757938903edd19e35faa0bd8ea4a9a0917282ca3366`.

The subsequent portability and static-analysis fix changed the handler binary
without changing the batch algorithm. The corrected normal handler, SHA-256
`ea13c3304c50b5440cb7add300188fdd3bce393900ec5fc379842a30cab5c37b`,
passed another full 46/46 Amiga integration run. A fresh checked BFS-first
comparison measured 2,796,721 µs for create, 1,180,856 µs for the 8 MiB
write, and 2,503,858 µs for delete. The corrected probe handler, SHA-256
`7384baf794379ea3c01995a229576c970d60922f8f3622d5195c4d59e0dc1757`,
passed a fresh PFS3-first deep comparison and reproduced 3,029 / 1,089 /
2,660 free-tree node writes. These single post-fix runs validate the final
binary but are not added to the paired timing means.

The [raw evidence](evidence/bfs-free-tree-leaf-batch-2026-09-29/) contains
the three exploratory deep-compare runs, two normal candidate runs, the
pre-fix normal/probe validations and both post-fix runs. Each retains both
checked guest TSVs, completion
marker, post-format machine
information, FS-UAE configuration and log. `SHA256SUMS` verifies all 54 copied
files. The HDF images, ROM and licensed PFS3 handler are not committed. These
are A1200/68040 FS-UAE 3.2.35 results on fresh 255.5 MiB BFS/PFS3
partitions, not physical-device throughput or power-loss qualification.

The next structural step is a bounded multi-level B-tree multi-edit operation,
described in `docs/plans/bfs-free-tree-multiedit-v1.md`. This leaf-only slice
cannot reduce the allocation-side COW work or the substantial remaining
small-file latency.
