# Rejected forward-only reserve validation

The forward-triangle candidate is rejected after normal workload comparison.
It is functionally equivalent in the reviewed paths and cuts healthy reserve
pair checks, but delete regresses 16.50% in the mean and in all four matched
pairs. A 15.27% mean write improvement does not justify that regression. The
qualified single-block-absence core and byte-identical handler are restored.
The additional equivalence tests remain. The five-times PFS3 goal is unachieved.

## Normal elapsed decision

Eight fresh normal systems provide four runs per revision, balanced by order
and repeat. Baseline is the retained single-block-absence handler, not its
older control. All sixteen TSVs pass the unchanged strict schema-1 verifier.
Times are arithmetic means in microseconds; probes are separate evidence.

| Workload | Baseline BFS µs | Candidate BFS µs | Change | Candidate PFS3 µs | BFS/PFS3 mean ratio | Adverse matched pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 215118.75 | 210111.25 | −2.3278% | 21871.00 | 9.606842 | 3/4 |
| Lookup 400 | 112047.25 | 111647.75 | −0.3565% | 69918.75 | 1.596821 | 1/4 |
| Small read 40 | 49823.50 | 46001.00 | −7.6721% | 14531.50 | 3.165606 | 0/4 |
| Write 8 MiB | 385566.25 | 326686.25 | −15.2710% | 35887.25 | 9.103129 | 1/4 |
| Read 8 MiB | 526924.75 | 525133.00 | −0.3400% | 260542.75 | 2.015535 | 2/4 |
| Delete 40 | 167935.25 | 195638.00 | +16.4961% | 7887.75 | 24.802764 | 4/4 |

Eleven of 24 matched BFS pairs are adverse. In matched initial BFS-first,
initial PFS3-first, repeat BFS-first, repeat PFS3-first order, delete increases
by 47040, 7296, 14337 and 42138 µs. The create mean conceals three adverse
pairs, offset by one −66879 µs pair. Write deltas are −93698, −155521, +40063
and −26364 µs. Every sample is retained; no adverse run is discarded.

Create, write and delete each exceed five times PFS3 in all four candidate
runs: twelve of 24 ratios fail. Candidate same-run ranges are 5.740199–15.058605
for create, 8.557643–9.763250 for write, and 22.007875–27.598233 for delete.
Ratios of means cannot replace that gate. PFS3 mean create drifts +30.56%,
write +3.04%, delete +0.20%. Four runs per revision with uncontrolled scheduling
are descriptive observations, not causal or statistical precision. In
particular, the data do not establish why a correct validation-only change
slows delete; compiler/code-layout and scheduling contributions are not
isolated. The measured regression is sufficient to reject this candidate.

Independent Luna extraction reads all sixteen raw normal TSVs before the
primary JSON exists. Means, matched deltas, all 24 ratios and adverse/threshold
counts agree. The chronological sequence is baseline BFS-first, candidate
PFS3-first, baseline PFS3-first, candidate BFS-first, repeat candidate BFS-first,
repeat baseline PFS3-first, repeat candidate PFS3-first, repeat baseline
BFS-first. Two subsequent candidate probes also pass: ten systems/twenty TSVs.
Every non-time probe counter agrees with retained absence-shortcut controls
in both orders. The controls are not contemporaneous normal measurements.
Inclusive wall-scope intervals overlap and are not exclusive CPU attribution.

## Equivalent proof, not performance acceptance

The private range helper changes from an ignored reserve index to a first
reserve index. Stock validation passes i+1, while candidate stash and ordinary
free pass zero and still validate the entire reserve. At index i, every prior
entry has already checked every later entry, so a duplicate with a prior entry
cannot survive. Checking only future entries retains the earliest failure:
protection first, duplicates second, active emergency membership third. A
backward-only triangle would not preserve that ordering.

The global capacity/emergency guards, unsigned bounds, per-entry protection,
full active pool and all historical emergency-slot behavior remain unchanged.
No proof is cached, no state is changed during validation and no new memory
allocation is added. For a healthy n-entry full scan, reserve-pair checks fall
from n(n−1) to n(n−1)/2: 16256 to 8128 at n=128. This does not halve emergency
checks or total validation work and makes no fixed reduction claim for errors.

A separate Luna worker checks the actual three call sites and proof. Its exact
abstract model, preserved with explicit root-added scope comments, compares
23296 return-category cases and is rerun successfully. It models arrays of
length0–5 over three values and all protected/emergency subsets, not real
count guards, filesystem geometry, arithmetic or concurrent mutation. The
actual source is separately reviewed under the existing stable-state contract.

The distinct test author adds three registered C tests without changing
existing oracles. The exact same final source passes 27/27 tests with both
the old core and candidate. It covers synthetic active prefixes0/1, reversed
stock, full128 stock obtained through ordinary owned allocations and popped
at entry127, duplicate positions, active pool alias and both mixed-fault
precedence cases. Failures preserve the complete reserve, root bytes/ID,
free/count, working superblock/pool and write count. Prefix0/1 states are
explicitly not publishable accounting witnesses. The reviewer finds no
blocker, but notes no new direct over-capacity injection through this stock
path; those unchanged guards are inspected, not claimed as newly tested.

## Qualification and restoration

Candidate normal and actual ASan/UBSan each pass 48 suites/468 cases. Leak
detection is disabled and not qualified. All78 quality tests, ShellCheck,
actionlint and clang static analysis pass. Linux passes20 conformance tests
and two actual FUSE runs: default and options7 pressure/interrupted-daemon.
Two separate Amiga build directories reproduce normal/probe/guest binaries.
None of these correct-function gates overrides adverse normal performance.

The environment and input geometry are unchanged from the
[retained absence-shortcut comparison](bfs-single-block-absence-performance-2026-10-01.md):
Cachy FS-UAE3.2.35, A1200/68040 max, 8 MiB fast/2 MiB chip, fresh255.5 MiB
partitions, BFS4 KiB/options0/30 buffers, unchanged normal/probe guests,
formatter, PFS3 and ROM. Idle preflight verifies every input and no remote
compiler/emulator. `coffin.service` is inactive, not disabled. Physical CPUs,
power/controller/media and a complete FUSE geometry matrix are not qualified.

Restoration removes only the candidate helper/call-site changes. Core bytes
match the saved baseline exactly. The rebuilt normal handler is byte-identical
to the retained measured handler. New functional tests remain because their
statuses agree on both cores; final restored normal and ASan/UBSan suites are
also recorded separately.

SHA-256 identities:

- Baseline/restored core: `b628d72bcc5ba0a8280c801c7ef2c769db9cb0d24523d0ff1eb81055cc06c193`.
- Rejected core: `6161a109a0a928b8a9158ef4c4160a9ee7faebb6236a8ea21bf9093e448a4185`.
- Retained final tests: `cfd43be9f7161563a70d465c1feab0ea9ce1dc8d7f7e7cbf4a16b227e212f079`.
- Baseline/restored normal: `7ec433160c6aa37c3ebbb5345097ef549638bde634dd8a3aa34e1b898a4a7d19`.
- Rejected normal: `fa264b32bb3b62e7549b6884c8fb0b7aea83993da323cd2ac6690289044cc1b2`.
- Rejected probe: `e4da08b9d09a82cdce076855c5836d13cc88e28102e28ef26518647b4e85fa77`.
- Normal/probe guest: unchanged `1fccf207…` / `5126e864…`, full hashes retained.

## Evidence and boundaries

The [versioned plan](../plans/bfs-reserve-pair-validation-v1.md) owns requirements;
this report owns the rejected outcome. The [evidence directory](evidence/bfs-reserve-pair-validation-2026-10-01/)
contains the rejected patch and exact baseline, model, old/new tests, platform
logs, raw records, extractors, configurations and identities. All ten completed
remote systems are recoverably archived at
`/home/fabian/.cache/bfs-performance/reserve-pair-validation-2026-10-01/` with
twenty before/after TSV hashes identical. Raw copied TSVs remain byte-identical.
Only new copied formatter C1 CSI text is ASCII-CSI normalized, with both hashes
recorded. The new scoped asset check is separate from the known global audit
failure on older unchanged C1 formatter records; no checker or old evidence
is weakened.

The earlier rejected expanded-stock/hash-set policy is not restored. No
placement, stock size, warmup8, ownership, format/API, in-place COW, writeback,
publication/fence, snapshot, rollback or workload change is introduced. No CI,
push, PR, merge, release, external publication, new soak or hardware qualification.
Further work needs a larger safe within-operation improvement; this rejected
loop does not close the create/delete gap.
