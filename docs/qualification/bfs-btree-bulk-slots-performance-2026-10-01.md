# Rejected bulk B-tree slot movement

The standard `memmove` candidate is rejected. Correctness and platform gates
pass, and the emitted helpers eliminate per-slot shift backedges. Normal
8 MiB write nevertheless slows in all four matched pairs, by 6.00% in the
mean. Create, lookup and small read also have adverse means. A 4.09% delete
mean improvement does not justify the write regression. The exact retained
production source and normal handler are restored; equivalent additional
tests and rejected evidence remain. The five-times PFS3 goal is unachieved.

## Normal elapsed decision

Eight fresh normal systems provide four runs per revision, balanced by
filesystem order and repeat. Baseline is the qualified single-block-absence
handler at local checkpoint `449d66a`, not an older control. All sixteen raw
schema-1 TSVs pass the unchanged strict verifier. No sample is trimmed.
Arithmetic means below are microseconds; diagnostic probes are separate.

| Workload | Baseline BFS µs | Candidate BFS µs | Change | Candidate PFS3 µs | BFS/PFS3 mean ratio | Adverse matched pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 203792.50 | 208766.50 | +2.4407% | 21232.00 | 9.832635 | 2/4 |
| Lookup 400 | 108734.75 | 110623.00 | +1.7366% | 73323.75 | 1.508693 | 3/4 |
| Small read 40 | 48112.00 | 48592.00 | +0.9977% | 13788.00 | 3.524224 | 1/4 |
| Write 8 MiB | 334519.50 | 354590.25 | +5.9999% | 34940.25 | 10.148475 | 4/4 |
| Read 8 MiB | 538317.25 | 522240.00 | −2.9866% | 276523.00 | 1.888595 | 2/4 |
| Delete 40 | 188510.50 | 180798.75 | −4.0909% | 7968.00 | 22.690606 | 2/4 |

Fourteen of 24 matched BFS pairs are adverse. In initial BFS-first, initial
PFS3-first, repeat BFS-first, repeat PFS3-first matched order, write increases
by 52801, 7414, 6754 and 13314 µs. Delete deltas are +12672, −17599, −40767
and +14847 µs: its lower mean is not a consistent per-pair improvement.

Every candidate create, write and delete run exceeds five times same-run
PFS3: twelve of 24 ratios fail. Their ranges are respectively
7.253002–12.220943, 8.540576–12.077632 and 21.780361–23.293132.
Lookup, small read and sequential read have zero threshold failures in this
sample. Ratios of means do not replace the individual-run gate.

PFS3 baseline-to-candidate mean drift is +0.9137% create, +7.5519% lookup,
−16.5754% small read, −13.2388% write, −6.1075% read and −1.7721% delete.
Four observations per revision with uncontrolled scheduling are descriptive,
not causal or statistically precise. The results do not isolate compiler
layout, scheduling, short-suffix libc overhead or another explanation for
the regression. Correctness and reduced static loop work are insufficient
for performance acceptance. Read-only changes are not attributed to helpers
they do not execute.

A Luna worker independently reads and validates all sixteen raw normal TSVs
before the primary JSON exists. Means, matched deltas, all 24 ratios and
adverse/threshold counts agree. Chronological execution is baseline BFS-first,
candidate PFS3-first, baseline PFS3-first, candidate BFS-first, repeat candidate
BFS-first, repeat baseline PFS3-first, repeat candidate PFS3-first, repeat
baseline BFS-first. Two subsequent candidate probes also pass: ten systems,
twenty TSVs. Against retained absence-shortcut probe controls, both orders
have zero structural and zero other non-time counter differences. Those
controls are not contemporaneous normal timing evidence. Inclusive wall
scopes overlap and do not provide exclusive CPU attribution.

## Equivalent movement and actual target assembly

Only `leaf_insert_at`, `leaf_remove_at`, `internal_insert_at` and
`internal_remove_at` change. Each contiguous key/value or key/child suffix
moves once with overlap-safe standard `memmove`. Internal child bytes remain
big-endian. Valid caller indices and validated node capacity bound every
length; zero suffixes perform no move. Incoming item copies, counts, untouched
bytes, canonicalization, split/merge policy, other borrow loops, COW, CRC,
reads, errors and cleanup remain unchanged. An independent Luna source
review finds no equivalence blocker. No private libc replacement is added.

Root-generated old/candidate assembly uses the normal `-O2 -m68020` flags,
not 68040 code generation. A separate Luna reviewer checks actual loop
backedges. With positive suffix length s, emitted helper behavior is:

| Helper | Old copy calls | Candidate copy calls | Old DIVU | Candidate DIVU |
| --- | --- | --- | --- | --- |
| Leaf insert | 2s+2 memcpy | 2 memmove+2 memcpy | s+1 | 2 |
| Leaf remove | 2s memcpy | 2 memmove | s | 1 |
| Internal insert | s+1 memcpy; direct child moves | 2 memmove+1 memcpy | s+1 | 2 |
| Internal remove | s memcpy; direct child moves | 2 memmove | s | 1 |

All four candidate shift backedges disappear, but fixed layout divisions
remain. Zero-suffix insertions retain incoming copies and one division;
zero-suffix removals have neither a copy nor a division. These are static
instruction findings, not dynamic workload call counts or a speed forecast.

The first Amiga build fails because the local string-header shim lacks the
standard `memmove` prototype. That failed log is retained. Adding only the
normal declaration corrects the build without suppressing warnings. The
actual link map selects `libm020/libnix.a`, not the generic archive initially
considered. Its 32-byte memmove wrapper saves D2 and calls the selected
252-byte bcopy body, which saves A2/D2, chooses direction, handles alignment,
uses 32-byte iterations and tails, and includes 68020-safe BFEXTU. Selected
archive/member hashes and textual disassemblies are retained; objects and
library binaries remain private. The compiler/linker choice is source-bound,
not an assumption about whole-workload performance.

## Tests, failures and qualification limits

A separate Luna author adds four public-operation tests. The first fixture
source incorrectly uses unsupported 512-byte blocks in three cases. Both
old-source focused and candidate full normal/ASan attempts fail those setup
assertions; only the 4 KiB max-key case passes. The exact first source and
failed logs remain, never overwritten or counted as successful gates.

Correction uses shared `BFS_MIN_BLOCK_SIZE` (1024) and a 31/5 odd key/value
shape for the 1600-entry multilevel case. Existing oracles remain unchanged.
The exact final source passes 4/4 on both old and candidate B-tree sources.
Final candidate normal and actual ASan/UBSan each pass 49 suites/472 cases,
zero failures, exit zero. Leak detection is disabled, not qualified. Earlier
prebuild runs exclude the new source and pass 48 suites/468 cases; they do not
substitute for the final gates. All 78 quality tests, ShellCheck, actionlint
and clang static analysis pass. Two separate Amiga directories reproduce
normal/probe handlers byte-for-byte; a third normal map build also agrees.
The probe guest is copied unchanged, not claimed to be rebuilt.

The reviewer is distinct from the test author. Explicit leaf cases exercise
first/middle/last insertion/deletion and zero shifts, independent bitwise
full-block CRC, exact scan/value/search results, recursive child/separator
bounds and old-root bytes/readability. At 1 KiB, leaf/internal capacities
27/28 bound height two at 29×27=783 entries, below 1600; at 4 KiB, maximum
512-byte keys give capacities 7/7 and height-two bound 56, below 240. Thus
both force deeper trees. Internal exact slot indices are not instrumented.
The old tree is a standalone transaction-age view, not a mounted snapshot.
Faults cover initial leaf-insert read and full COW write rejected before
backing storage, not partial-write/delete faults. Intermediate deletion
states are not independently scanned. Existing fault/graph/snapshot/low-space
regressions remain unchanged and pass; no stronger fault scope is implied.

Linux passes 20 conformance tests and two actual FUSE runs: default and
options7 pressure/interrupted daemon. That private source copy initially
contains the first additional test, which those commands do not execute.
Only the corrected final test is then copied there and built/run separately:
4/4 pass with its exact final hash. Production source is unchanged between
these gates. A full FUSE geometry matrix is not qualified.

The [retained comparison](bfs-single-block-absence-performance-2026-10-01.md)
environment remains Cachy FS-UAE3.2.35, A1200/68040 max, 8 MiB fast/2 MiB chip,
fresh 255.5 MiB partitions, BFS 4 KiB/options0/30 buffers and unchanged normal
and probe guests, formatter, PFS3 and ROM. Idle preflight verifies input
identities and no concurrent compiler/emulator. `coffin.service` is inactive,
not disabled. No physical power/controller/media qualification is implied.

## Restoration and evidence

Rollback restores only the four helpers and the added shim declaration. The
B-tree and shim bytes exactly match the retained checkpoint; a rebuilt normal
handler is byte-identical to the measured baseline. Equivalent new tests
remain, with restored normal and ASan/UBSan gates separately recorded.

SHA-256 identities:

- Baseline/restored B-tree: `5095d6dd4e9e2984eb88a68779d1f5ea3fbb7e39c3c10a22f32de0479d2cb7da`.
- Rejected B-tree: `d9ab6caddb8ae775638c01b0ffd1e349129843eaa846fd968346fa530252290d`.
- Baseline/restored shim: `8f402e80b8c9c097ef50b7e71a211514f3d97962e8c5802f66614316878b7254`.
- Rejected shim: `7bc432ebb42394c0afc38f41b6e531fbbfeaae48cb8be8c54705f7ad481f66c6`.
- Retained final tests: `cf0333a3b7025ce0d4e18c6c65a70b8c5db00eda3345975ac48d366a40e607ea`.
- Baseline/restored normal: `7ec433160c6aa37c3ebbb5345097ef549638bde634dd8a3aa34e1b898a4a7d19`.
- Rejected normal: `fcc6ed6a3e88aaf5a92309911e7bcd1ce074838e7be0540ac6d71397d75145b5`.
- Rejected probe: `361418d854f121f068d753d120a97fd5e47ca813a24de03761a94775e2b7de2c`.
- Normal/probe guests: unchanged `1fccf207…` / `5126e864…`; full input hashes are retained.

The [versioned plan](../plans/bfs-btree-bulk-slots-v1.md) owns requirements;
this report owns the rejected outcome. The [evidence directory](evidence/bfs-btree-bulk-slots-2026-10-01/)
contains the exact old source, rejected patch/shim, initial/final test source,
all failed and final gates, target assembly/disassembly/map, raw records,
extractors, configurations and identities. All ten completed remote systems
are recoverably archived at
`/home/fabian/.cache/bfs-performance/btree-bulk-slots-2026-10-01/`, with all
twenty before/after TSV hashes identical. Copied raw TSVs remain byte-identical.
Only new copied formatter C1 CSI text is ASCII-CSI normalized, retaining
original/output hashes. The new scoped asset check is separate from the
known global audit failure on older unchanged C1 formatter records. Neither
the checker nor older evidence is weakened.

No format/API, ownership, allocator stock/placement/refill/warmup, in-place
COW, writeback, cross-packet batching, snapshot, fence, flush, publication,
rollback or workload change is retained. Earlier rejected pilots remain
rejected. No CI, push, PR, merge, release, external publication, new soak or
hardware qualification. Short-copy dispatch before the eleven-register
memcpy prologue is a separate possible pilot, not an accepted improvement
or a reinstatement of the rejected wider large-copy kernel.
