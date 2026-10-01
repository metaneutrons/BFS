# Single-block Free-Tree absence shortcut

The narrow shortcut is retained: it removes demonstrably redundant traversal
and heap work, with a modest observed normal-write benefit and no material
mean regression in this comparison. The five-times PFS3 goal is still not met.
Create, write and delete exceed five times PFS3 in all four candidate runs:
12 of 24 same-run workload ratios fail the threshold.

## Normal-handler comparison

Eight fresh systems run four normal measurements per revision, balanced by
BFS-first/PFS3-first order and repeat. The table reports arithmetic means in
microseconds. Probe times are not pooled into these observations.

| Workload | Baseline BFS µs | Candidate BFS µs | Observed change | Candidate PFS3 µs | BFS/PFS3 mean ratio | Adverse matched pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 193726.50 | 189951.25 | −1.9488% | 19312.00 | 9.835918 | 1/4 |
| Lookup 400 | 112476.00 | 112767.00 | +0.2587% | 73535.75 | 1.533499 | 1/4 |
| Small read 40 | 54697.50 | 47231.75 | −13.6492% | 20108.25 | 2.348874 | 2/4 |
| Write 8 MiB | 387904.75 | 363693.25 | −6.2416% | 46639.50 | 7.797966 | 1/4 |
| Read 8 MiB | 600399.25 | 525774.75 | −12.4291% | 289521.00 | 1.816016 | 0/4 |
| Delete 40 | 186478.50 | 172990.75 | −7.2329% | 8032.00 | 21.537693 | 2/4 |

Seven of 24 matched BFS pairs are adverse, including the first BFS-first write
pair (+36031 µs). This is not a clean universal speedup. The write median moves
from 370494.5 to 361085.5 µs, a smaller change than the mean. The read-only
improvements have no changed structural counters and must not be attributed
to the absence shortcut. PFS3 means also drift: create +10.94%, write +32.08%,
small read +40.04%. In particular, the lowest candidate write ratio, 5.019950,
coincides with PFS3 taking 67519 µs rather than a stable faster reference. It
does not establish proximity to the target. Four samples per revision with
uncontrolled scheduling do not support causal or statistical precision.

A separate Luna worker extracts all sixteen normal TSVs before the primary
JSON exists. Raw arrays, means, matched deltas, all 24 same-run ratios and
threshold/adverse counts agree with the primary extraction. The chronological
sequence is baseline BFS-first, candidate PFS3-first, baseline PFS3-first,
candidate BFS-first, repeat candidate BFS-first, repeat baseline PFS3-first,
repeat candidate PFS3-first, repeat baseline BFS-first. Every normal and probe
output passes the existing strict verifier, including data/EOF/completion
checks: ten systems and twenty BFS/PFS3 TSVs.

## Proof and measured mechanism

The old helper first validates a floor-search predecessor, then scans from
the same start. In a validated height-one leaf with strictly ordered integer
keys, a one-block interval [start,start+1) cannot contain an additional starting
key: an exact or covering predecessor has already been rejected, and every
successor is at or beyond the exclusive end. The six-line core change returns
success only for count=1, height=1 and a nonempty root after the existing
floor result and selected-extent validation. It caches no proof.

Deeper trees retain the scan because it supplies additional ancestor-bound
checks; larger intervals and empty roots retain their existing paths. CRC,
node shape, expected level, key order, selected-extent geometry and the first
read error are checked before the shortcut. Removing the second allocation/
read also removes failures that could arise only from that redundant operation.
This is not a claim that obsolete injected read ordinals remain unchanged.
The existing serialized BIO/cache contract remains the same.

The corrected focused old-source counter-probe reports 22 passes and two
failures: a successful below-first stash requires two root reads and a pop
requires three, rather than the asserted one. The new source passes all 24
focused tests. Positive tests use genuinely caller-owned blocks below, between
and after two free extents and check exact root/free/reserve accounting. CRC,
CRC-correct invalid key order, a malformed selected extent and first-read I/O
errors reject without mutation. Existing stock/root/emergency, deeper-tree,
snapshot and rollback coverage is retained. The first test draft and failed
run are preserved; the fixture CRC update and pop setup are corrected before
the final old/new comparison. A distinct Luna worker independently reviews
the actual implementation and corrected tests without editing or building.

Two fresh candidate probes in opposite orders show the same count changes
against retained, noncontemporaneous CPU-scope controls:

| Workload | B-Tree heap alloc calls, old → new | Heap free calls, old → new |
| --- | ---: | ---: |
| Create 40 | 80 → 40 | 80 → 40 |
| Write 8 MiB | 639 → 134 | 639 → 134 |
| Delete 40 | 161 → 160 | 201 → 200 |

All other non-time counters agree in both comparisons; only those calls and
their corresponding sample counts change. Physical BIO reads do not change:
the mounted cache already absorbs the repeated read, but the helper still
allocated and freed scan workspace. Node/data writes, CRC calls,
filesystem-block allocations,
superblock publications, commit/fence counts and workload packet counts are
unchanged. Inclusive interface-free probe intervals fall from approximately
61 ms to 44–47 ms for large writes; these overlapping wall intervals are not
exclusive CPU attribution or contemporaneous normal acceptance evidence.

The diagnostic extractor's first executed draft uses the wrong scope name
`TXN_SEAL` and fails closed. Its exact source and reproduced failure log are
kept; the successful extractor uses the actual `SEAL_COMMIT` schema field.
No production source, raw result or verifier is altered to accommodate it.

## Guarantees, platform gates and identities

No placement/refill policy, reserve ownership ledger, LIVE-tag interpretation,
in-place COW, delayed writes, publication, fence or feature policy changes.
The common-core implementation is shared by Linux and Amiga. Local normal and
actual ASan/UBSan runs each pass 48 suites/465 cases; leak detection is disabled
and no leak-detector result is claimed. All 78 quality tests, ShellCheck,
actionlint and clang static analysis pass. Linux passes 20 conformance tests
and both actual FUSE runs: default options and options 7 with disk-pressure
and interrupted-daemon checks. Two independent Amiga build directories yield
byte-identical normal/probe handlers and probe guest.

The emulator is FS-UAE 3.2.35 on Cachy Linux 7.1.3-2-cachyos, A1200/68040 max,
8 MiB fast plus 2 MiB chip, Xvfb/null audio. Each comparison starts with fresh
255.5 MiB BFS/PFS3 partitions; BFS uses 4 KiB blocks, options 0 and 30 buffers.
The normal guest is unchanged. Idle preflight verifies all ten input systems
and the ROM, no concurrent emulator/compiler and inactive `coffin.service`.
That service is not disabled. No real CPU/media/power/controller qualification
or full FUSE geometry matrix is claimed.

SHA-256 identities:

- Old core: `38c3bb00326279b079586d7ca5cbee3da4904e63492050260e218712b80579f4`.
- Retained core: `b628d72bcc5ba0a8280c801c7ef2c769db9cb0d24523d0ff1eb81055cc06c193`.
- Focused tests: `ddf6565742f57ba325488832b467b046eade9c2a5eb1fdd8767d7083b65d3a54`.
- Old normal handler: `572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`.
- Retained normal handler: `7ec433160c6aa37c3ebbb5345097ef549638bde634dd8a3aa34e1b898a4a7d19`.
- Retained probe handler: `8bed35de45ebfe81fbac52cec55b99992ce0fa5f3e1ea9c9a89ae68e94b1a2ba`.
- Normal guest: `1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80`.
- Probe guest: `5126e8640256e19bc2f5661d8b735f7961c4882edf3d8046b26bd2badfc86277`.

## Evidence and next decision

The [versioned plan](../plans/bfs-single-block-absence-v1.md) separates proof
requirements from these observations. The [evidence directory](evidence/bfs-single-block-absence-2026-10-01/)
retains exact raw TSVs, failed/final focused sources and logs, the production
patch/baseline, successful and failed extractors, gates, image/run configurations,
input identities and recoverable remote archive hashes. The ten completed
remote systems are archived under
`/home/fabian/.cache/bfs-performance/single-block-absence-2026-10-01/` with
all twenty TSV hashes unchanged; no useless emulator or soak remains running.

Only new copied formatter text converts the C1 CSI byte to ASCII ESC[; its
original/output hashes are recorded. Raw result TSVs are unchanged. The scoped
new-evidence asset check is recorded separately; the known older committed
C1-encoded formatter records still prevent a globally green asset audit. No
global checker, older evidence or hook is weakened.

This pilot is a bounded reduction in allocator overhead, not the large
create/delete improvement still required. The next safe candidate is
[forward-only reserve-pair validation](../plans/bfs-reserve-pair-validation-v1.md) that preserves the earliest failing
index and error precedence. It must have a separate identity, tests and normal
comparison; it is not included in this report. No CI, push, PR, merge, release,
external publication or hardware qualification is performed.
