# B-tree local scratch lifetime v1

State: completed pilot, rejected after normal elapsed comparison. The overall
goal remains elapsed BFS <=5 times same-run PFS3 in every checked AmigaDOS
workload, preserving durability, integrity, snapshots and crash recovery.
The qualified production checkpoint is `1e52c04`; its B-tree source is
`5095d6dd4e9e2984eb88a68779d1f5ea3fbb7e39c3c10a22f32de0479d2cb7da`
and normal handler `7ec433160c6aa37c3ebbb5345097ef549638bde634dd8a3aa34e1b898a4a7d19`.
All rejected allocator, bulk-slot, CRC and memcpy experiments stay rejected.

## Mechanism and proof requirements

Current O2/m68020 emitted code clears the entire 2184-byte mutation journal
and 1032-byte bounds scratch. Journal arrays are written before their two
counts advance; all reads are bounded by those counts. Bounds keys are copied
before their validity flags become true; key-array reads are flag-gated.
Use one private initializer per structure to set only counts or flags at all
existing initialization/reset sites. Do not use partial aggregate initializers:
C would still zero unspecified members. Preserve every count/flag reset.

No public structure/API, format, allocation policy, tree geometry, validation,
copy algorithm, on-disk initialization, padding/CRC, COW, retirement, rollback,
publication, fence, flush or packet behavior changes. Private arrays may remain
indeterminate only until their owning slot/key is fully written. No new reads
of those bytes are permitted. An independent reviewer must inspect every
array/count/flag use, not infer safety merely from passing ASan. ASan/UBSan is
not MemorySanitizer or direct qualification of every indeterminate byte.

Static byte/store savings are not dynamic call counts or elapsed improvement.
This removes unnecessary writes rather than substituting a faster memcpy.
The short-copy kernel remains exactly retained. No dominance or five-times
speedup forecast is made.

## Qualification and decision

A distinct Luna test author supplies bounded public-API equivalence cases:
multilevel/full-width keys, repeated searches/scans across inherited lower/
upper bounds and branch transitions, CRC-correct ancestor-bound corruption,
and failing multi-level mutations with exact mapping/root/ownership rollback.
Run them against frozen old and new source, retaining all failures and gaps.
Existing mutation/snapshot/crash/corruption suites remain mandatory. Root owns
production edits; a third worker independently reviews source and tests.

Require complete normal and actual sanitizer host suites, existing quality/
static gates, Linux conformance plus actual default/options7 FUSE runs, and
two byte-identical Amiga normal/probe builds. Preserve exact old/new emitted
assembly and input identities; confirm large scratch clears are removed and
on-disk node initialization remains. Freeze source before timing.

Only after equivalence/review/gates pass, use eight fresh normal systems,
four per revision, balanced by order/repeat, with unchanged normal guest and
inputs. Two separately instrumented probes use the existing ABI/schema; all
non-time counters must agree. Apply the strict data/completion verifier.
Retain every adverse pair and individual-run five-times failure. Probe or
static savings cannot substitute for normal-handler elapsed acceptance.
Reject material regressions or an unhelpful result, restore exact source and
handler, and preserve equivalent tests and textual evidence. Recoverably
archive only these completed systems and stop only owned emulators.

No CI, push, PR, merge, release, external publication, new user chat or soak;
no physical CPU/power/controller/media qualification. The five-times goal
remains unachieved until all six workloads meet it in the checked normal runs.

## Outcome

Source/dataflow review, emitted-code work-removal review and bounded old/new
equivalence pass. Full host and actual ASan/UBSan pass 50 suites/475 cases;
focused all-core Linux MemorySanitizer passes 3 cases, existing quality/static
gates and Linux conformance/FUSE pass, and both Amiga handlers reproduce.
Eight normal systems and two separate probes all pass strict verification.
All non-time diagnostic counters agree with retained controls. Independent
raw extraction precedes primary actual JSON. Create/lookup improve all four
pairs, but sequential write/read means regress 5.13%/8.06%; the combined
candidate is rejected. Exact 5095d6dd… source and rebuilt 7ec43316… normal
are restored. New equivalent tests and rejected textual evidence remain.
Twelve of 24 candidate ratios still exceed five times PFS3; the overall goal
is not achieved. See the qualification report for all adverse observations
and scope limits. Ten completed systems are recoverably archived, not deleted.
