# BFS bounded metadata withdrawal batch performance

The first prototype is rejected and its production changes were removed
from the worktree at this qualification checkpoint. It passes the local safety
gates but makes all four matched create, write and delete observations slower.
The timestamp checkpoint remains
the production baseline. The five-times PFS3 objective is not achieved.

## Normal comparison

Four fresh-image normal runs per revision ran serially, balanced by filesystem
order. Means below are elapsed microseconds. Every observation is retained.

| Workload | Baseline BFS | Candidate BFS | Change | Candidate PFS3 | Candidate ratio of means |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 196686.25 | 297854.5 | +51.436% | 16350.75 | 18.216565 |
| Lookup 400 | 107868.25 | 123230.5 | +14.242% | 65791.25 | 1.873053 |
| Small read 40 | 47008.25 | 49280.25 | +4.833% | 14495.75 | 3.399634 |
| Write 8 MiB | 377952.25 | 476317.5 | +26.026% | 34607.75 | 13.763319 |
| Read 8 MiB | 498909.75 | 491933.75 | −1.398% | 261214.25 | 1.883258 |
| Delete 40 | 177422.25 | 246942.75 | +39.184% | 7920 | 31.179640 |

Eighteen of 24 order/repeat-matched BFS changes are adverse. Create, write and
delete are adverse in every matched pair; all twelve candidate observations of
those phases exceed five times same-run PFS3. The other phases stay below five
in these observations. Independent extraction agrees with all sixteen normal
TSVs, means, matched deltas and ratios. The
[complete measurements](evidence/bfs-metadata-batch-2026-10-01/measurement-summary.md)
preserve every raw value, adverse sample and threshold failure.

Four observations do not establish statistical or causal precision, and the
non-mutating lookup change is not attributed to allocator code. Physical-host
scheduling and PFS3 variation are uncontrolled. Nonetheless, the consistent
large regressions in every mutating pair reject this prototype; fewer node
writes alone cannot justify retention.

## Mechanism and diagnostic counts

The prototype bound a first-request, height-one mounted-owner metadata batch
to the existing normal reserve floor plus at most eight ordinary blocks. It
shared refill, owner, geometry, leaf preflight and validated surplus-pop logic;
standalone, snapshot, deeper-tree, public data policy, COW/CRC, allocation
freeze, publication fences and the settlement bound were not broadened.
Exact sink binding, pre-write root-tag checks and conservative pending-free
headroom guarded the optional start. Failures after start were not converted
to ordinary allocation retries; uncertain working state latched recovery.

Both fresh candidate probes agree on these counts. Earlier timestamp probes
are context only, not a contemporaneous paired diagnostic baseline.

| Phase | Total node writes, earlier → candidate | Free-Tree body | Free-Tree refill | Free-Tree other | B-tree malloc calls | Commits / BIO updates |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 548 → 479 | 233 → 40 | 40 → 204 | 80 → 40 | 80 → 313 | 40 / 120 |
| Write 8 MiB | 401 → 393 | 137 → 128 | 1 → 5 | 4 → 1 | 639 → 645 | 1 / 3 |
| Delete 40 | 313 → 285 | 116 → 0 | 40 → 129 | 41 → 40 | 161 → 317 | 40 / 120 |

The existing highest-tail refill repeatedly consumes short high-key extents;
requesting more stock does not imply one COW mutation. The reduced allocation
body count is partly replaced by refills, and heap work grows. This is a
source-supported mechanism consistent with the regression, not a complete
causal decomposition of elapsed time. Allocator, heap and device intervals
overlap and cannot be summed or subtracted. Node CRC timing samples every 64th
call. Probe elapsed time is not used for acceptance.

## Safety qualification and limits

Independent production and test review found no remaining blocker at the final
source identities. Sixteen functional cases cover all seven allocator block
sizes, ownership/accounting, epoch reset, exclusion/fallback paths, real current
and committed root/backup aliases, corruption guards, low space/capacity,
unsynced directory roots, churn and actual deeper-tree commit fallback.
Seven fault cases include successful controls, full/prefix first-write failures,
fragmented multiple-tail controls and full/prefix later-write failures after a
successful COW/root swap. Published bytes, volatile pool inventory, no retry,
sticky error and committed checker/remount graph/data oracles are asserted.

Two isolated variants removing only one new guard each fail exactly their
corresponding probe (15 pass, one fails). In particular, one pending slot lets
ordinary allocation succeed while batching declines; a fully exhausted queue
alone does not distinguish the batch headroom check from ordinary preflight.
The old sealed-preflight fixture retains every original oracle; its new helper
constructs actual emergency-origin COW provenance and exact logical leaf content
instead of relying on allocator placement. Whole-node bytes legitimately change.

A later physical write failure is not a post-swap retirement-callback failure.
The latter was not injected against the real mounted sink. Independent source
review gives a conditional valid-context argument: exclusive operation, exact
owner-bound sink and successful headroom leave no expected callback error
without owner corruption or forbidden reentry. Existing lower-level retirement
failure tests pass but use a substituted sink excluded from batch eligibility.
No stronger batch-specific injection or hardware guarantee is claimed. Some
test assertion-failure paths bypass cleanup/unlock; passing paths clean up.

The frozen candidate passes 50 host suites / 483 tests in both normal and actual
ASan/UBSan builds, 70 quality tests plus shell/static checks, 20 Linux conformance
tests plus actual FUSE qualification, and normal/probe m68k builds. Two separate
m68k builds reproduce both handler hashes. Leak detection is disabled in the
sanitizer run as recorded. Initial failed fixture/compile/invocation attempts
are retained; they are not counted as gates.

## Reproduction and disposition

Baseline is the timestamp production checkpoint `dfe4f53`, with normal handler
SHA-256 `572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`.
Rejected candidate normal is
`73e9198e6ef2619248b21913b948654199c5b5202102d32c63a23c3d656aec96`;
probe is `17d27e22b6dbd2d1fc11fefb60b033e2d12d61e53c812452286b7d6ed8ea4721`.
The production patch replays against `cb925e4`; SHA-256 is
`73df41e2edc778bd9a0a193335c26635cd71c8a7b171be98f5f0c4a098701c68`.
Separate fixture and guard-removal patches and all final source/test hashes are
recorded. Rejected tests are recoverable in evidence and an ignored local archive,
not active production tests with missing prototype fields.

All ten images were prepared before timing using the unchanged guest, fixed
formatter, PFS3, ROM/Workbench, options=0, 4 KiB BFS geometry, 255.5 MiB partitions,
30 requested cache slots and Cachy FS-UAE 3.2.35 A1200/68040 maximum speed. No data
checksums or snapshots are enabled in this performance workload. Preflight checks
all inputs and finds no other emulator/compiler or active Coffin instance.

[Raw evidence](evidence/bfs-metadata-batch-2026-10-01/) preserves all twenty TSVs,
completion/phase files, configurations/logs, build/test gates, unsuccessful
attempts, scripts, source/fixture/guard patches and summaries. TSVs are unchanged;
copied log trailing horizontal whitespace is normalized. No HDF, binary or
licensed asset is committed. No CI, push, PR, merge, release or real-device
power/controller/media qualification ran. A follow-on must avoid repeated
small-tail refills and be independently qualified; it cannot inherit acceptance
from this rejected experiment.

After timing, all ten completed remote images were moved to the recoverable
archive `/home/fabian/.cache/bfs-performance/metadata-batch-2026-10-01/`.
All twenty timing TSV hashes remain identical before/after the move; local
original images also remain. No material evidence was deleted.
