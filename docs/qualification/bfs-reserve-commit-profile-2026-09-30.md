# BFS reserve-return and commit-round profile — 2026-09-30

## Result

Two fresh-image FS-UAE deep-compare runs, first BFS then PFS3 and in the
reverse order, passed the strict verifier. All non-timing BFS counters were
identical between orders. The additional probe shows that the small-file
reserve-return cost is predominantly repeated single-block Free-Tree edits
within a multi-publication commit, not merely a few large return ranges.

| BFS workload | Commits | Reserve-return calls | Returned runs / blocks | One-block runs | Return Free-Tree node writes | Post-publication reclaim passes | Superblock publications |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 × 1 KiB files | 40 | 159 | 2,116 / 2,461 | 2,061 (97.4%) | 2,281 | 119 | 159 |
| Write one 8 MiB file | 1 | 4 | 49 / 62 | 47 (95.9%) | 53 | 3 | 4 |
| Delete 40 small files | 40 | 160 | 2,045 / 2,480 | 1,972 (96.4%) | 2,223 | 120 | 160 |

In the create phase, the remaining run lengths were 40 runs of four to seven
blocks and 15 runs of eight blocks. In the delete phase they were 58 and 15;
the 8 MiB write had one of each. No two-to-three-block run occurred. The
maximum run length was eight blocks and the maximum Free-Tree node writes in
one run was two. The mean was 1.08 writes per returned run for create and
1.09 for delete. Runs count successful return operations, not
distinct physical blocks; the same block can cycle through the reserve more
than once.

The create phase averaged 2.975 post-publication reclaim passes per commit;
delete used exactly three per commit (120 passes across 40 commits, with a
measured maximum of three). Each pass is followed by another reserve return
and superblock publication in the current commit loop. The measured
`BIO_UPDATES` counts were 199 for create, 5 for the 8 MiB write and 200 for
delete: in these successful runs, exactly one more update per commit than the
number of superblock publications. This is a handler/device-request count,
not a claim about a controller's physical flush behavior.

## Interpretation

The next optimization experiment should address two interacting mechanisms:
the thousands of singleton returns and the three-pass post-publication
settlement. A bounded batch of return ranges could reduce repeated Free-Tree
COW mutations, but it must preserve scratch-block ownership, defer old-root
reclamation and prove that the publish/reclaim loop still terminates. Simply
retaining the reserve across commits is not qualified: a prior prototype hit
the 256-pass settlement limit on repeated overwrites (see the
[multi-edit plan](../plans/bfs-free-tree-multiedit-v1.md)). The present counters
identify operation frequency, not a safe algorithm or a latency reduction.

These measurements do not attribute returned runs between the initial
pre-publication call and the calls after each reclaim pass. That split is a
useful next probe if a batch design depends on the boundary. The sequential
write's 644 allocation-body and 384 `other` Free-Tree node writes remain
separate work; reserve-return batching alone cannot remove them.

## Instrumentation, verification and limits

The probe-only snapshot ABI is v7 and the deep-compare TSV schema is v6.
It records return calls, successful runs/blocks, four run-length bins, maximum
run length, maximum Free-Tree writes per run, total and maximum reclaim passes
per commit, and successful superblock publications. Existing v4/v5 TSVs
remain accepted by the verifier; v6 adds run-bin, block-bound and publication
invariants. The normal handler has no new instrumentation.

Both fresh 255.5 MiB partitions ran under FS-UAE 3.2.35, A1200/68040 at
maximum emulated speed, Kickstart 47.102 and Workbench 47.2. The post-format
inventory identifies both volumes as read/write, both checked guest workloads
ended with `PASS`, and both strict result verifications passed. The initial
inventory precedes PFS3 formatting and must not be read as a mount failure.
The focused verifier suite (8 tests), probe and normal Amiga handler builds,
guest tool build, and full `make check` passed locally. No remote CI was run.

Probe handler SHA-256:
`b138a5baab210cd023168dacfcc57d78f1b6ece4d7d35e85650e1f2f600aa20f`.
Guest tool SHA-256:
`f21edbb4703a392f9e7c69e096afddc68c39d9354b472fcbc539230c4d5ffc51`.
PFS3 handler SHA-256:
`bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

The [raw evidence](evidence/bfs-reserve-commit-probe-2026-09-30/) contains
both guest TSVs, completion marker, before/after machine information,
emulator configuration and log for each order. `SHA256SUMS` covers every
retained file; the log copies differ from the emulator originals only by
removed trailing spaces. HDFs, ROM and licensed binaries are not committed.
Probe timings are preserved but are not an uninstrumented speed comparison.
FS-UAE does not qualify real media, power cuts or dishonest controller flushes.
