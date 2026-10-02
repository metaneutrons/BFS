# B-tree private scratch initialization qualification

The combined count/flag-only initializer candidate is rejected after normal
filesystem comparison. Create and lookup improve in all four matched pairs,
and delete improves in three. However, sequential write has a +5.13% mean
regression and sequential read +8.06%, adverse in two and three pairs. Static
work removal and green equivalence tests do not justify adoption. Exact
qualified B-tree source and the byte-identical retained normal handler are
restored; equivalent tests and textual evidence remain. The overall goal of
<=5 times same-run PFS3 in every checked workload is unachieved.

## Normal elapsed decision

Eight fresh normal systems provide four runs per revision, balanced by order
and repeat. Baseline is the qualified single-block-absence handler at checkpoint
`1e52c04`. All sixteen schema-1 raw TSVs pass the unchanged strict data/completion
verifier. Means are microseconds; no observation is removed. Diagnostic probe
times are excluded from acceptance.

| Workload | Baseline BFS µs | Candidate BFS µs | Change | Candidate PFS3 µs | BFS/PFS3 mean ratio | Adverse BFS pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 204335.50 | 186431.00 | −8.7623% | 17202.50 | 10.837436 | 0/4 |
| Lookup 400 | 114223.00 | 102958.75 | −9.8616% | 78207.75 | 1.316478 | 0/4 |
| Small read 40 | 48192.00 | 45311.75 | −5.9766% | 14128.25 | 3.207174 | 1/4 |
| Write 8 MiB | 329953.25 | 346865.75 | +5.1257% | 39656.50 | 8.746757 | 2/4 |
| Read 8 MiB | 522173.25 | 564281.75 | +8.0641% | 269438.50 | 2.094288 | 3/4 |
| Delete 40 | 188836.75 | 164252.00 | −13.0190% | 7968.50 | 20.612662 | 1/4 |

Seven of 24 matched BFS pairs are adverse. In BFS-first, PFS3-first, repeat
BFS-first, repeat PFS3-first order, write changes are +5.2840%, +15.7758%,
−0.3586%, −0.4859%; read changes +13.5483%, +13.6415%, −2.6952%, +8.0615%.
Delete changes −4.3042%, −13.3711%, +0.5889%, −28.8020%. A lower mean is not
uniform improvement; all adverse observations remain.

Create, write and delete exceed five times same-run PFS3 in all four candidate
runs: 12 of 24 ratios fail. Ranges are 9.716300–13.032236, 6.991102–10.104786
and 20.255512–21.046740 respectively. The other three workloads have no failures
in this sample. Ratios of means do not replace the individual-run gate. PFS3
mean drift is +1.5271% create, +16.7428% lookup, −1.0090% small read, +11.0453%
write, −0.4491% read and −3.1038% delete. Initial PFS3-first matched lookup/write
comparisons include +63.2737%/+52.3782% PFS3 changes. These are retained, not
normalized away. Four runs per revision and uncontrolled host/VM scheduling
do not establish causal or statistical precision. The reason for regressions
and isolated initializer latency are not established. The tested combination
is not accepted despite useful create/lookup means.

A distinct Luna worker extracts all sixteen raw files before primary normal
JSON exists, without reading the primary consumer. Means, paired deltas, ratios,
seven adverse BFS pairs and 12 threshold failures agree with later primary
extraction. Its own schema/order checks and fresh strict-verifier runs pass.
Chronological labels are baseline BFS-first, candidate PFS3-first, baseline
PFS3-first, candidate BFS-first, repeat candidate BFS-first, repeat baseline
PFS3-first, repeat candidate PFS3-first, repeat baseline BFS-first.

Two separate candidate probe systems pass, yielding four diagnostic TSVs.
The whole experiment has ten systems and twenty new TSVs.
All structural and other non-time counters match retained absence-shortcut
controls in both orders. A separate counter-equivalence gate explicitly checks
empty difference arrays: the consumer's input-verification PASS alone does not
assert equivalence. These controls are not contemporaneous normal timing
controls. Inclusive wall scopes overlap and are not exclusive CPU costs;
do not sum or subtract them.

## Mechanism and static evidence

The candidate changes two private helpers and existing call sites. Five mutation
journals initialize their two counts instead of clearing unused arrays. Search
and scan reset validity flags at all four existing source sites instead of
clearing unused keys. Journal slots are fully written before their counts admit
a reader; bounds key bytes are copied before flags admit comparison. Inherited
ancestor bounds retain their semantics. Partial aggregate initializers are not
used: C would still zero unspecified array members.

No public ABI/API, tree geometry, allocation policy, copy algorithm, format,
on-disk initialization, padding/CRC, COW, retirement, rollback, publication,
fence, flush or packet behavior changes. `node_init` still zeroes the full
on-disk block. Shared-core logic is identical for Amiga and Linux. Earlier
rejected reserve, bulk-slot, CRC and memcpy experiments remain rejected.

Actual O2/m68020 old/new assembly uses identical strict flags. Independent
review confirms all five old journal initializers clear 546 longwords/2184
target bytes; candidate sites each write two 32-bit counts. Each of the four
source bounds initialization/reset sites clears 258 longwords/1032 target bytes in baseline;
candidate code writes only two target flags. Scan lowering duplicates mutually
exclusive flag-write blocks, so static sites must not be summed as dynamic
executions. These are static store savings, not measured call counts, stack-
frame savings or elapsed gains.

A private work-removal probe includes actual helper definitions and checks
that defined poisoned arrays remain byte-identical while counts/flags reset.
Its host journal/bounds sizes are 2192/1026 bytes, not m68k 2184/1032 sizes.
Two private full-zero helper controls deliberately fail array-preservation
oracles with expected exit 1. They retain old valid semantics; these are not
core correctness failures or indeterminate-read tests. Production is never
changed for these negative controls. A reviewer distinct from implementation
and test authors finds no dataflow or emitted-code blocker. This is not
performance acceptance.

## Functional and platform qualification

The frozen public fixture has three cases, all passing against frozen old
library and candidate. Root recompiles/replays the old library after checking
its exact identity. Tests exercise 512-byte keys differing only in their final
four bytes in a height-at-least-three tree, forward/reverse hits, misses and
scans across branches; CRC-valid inherited upper-bound corruption rejected by
search/scan after exactly two reads; and multi-level update allocation/third-
write aborts preserving root, root bytes, mapped values and ownership bitmap,
with no invalid allocator operations. Write failure rejects before backing
copy, not a partial media write. No exhaustive fault-ordinal matrix, mirrored
lower-bound corruption or new delete/rekey/root-leaf-replacement faults are
claimed by this file. Successful setup inserts and update aborts directly
exercise two of five journal initialization paths. Existing broader mutation,
snapshot, corruption and crash suites remain mandatory. Independent fixture
review finds no false assertion or blocker.

Full native and actual ASan/UBSan runs pass 50 suites/475 cases. Preliminary
tracked-only runs separately pass 49/472. Leak detection is off; ASan alone
does not qualify reads of indeterminate bytes. Focused Linux MemorySanitizer
uses available clang 22.1.8 with every core translation unit and the fixture
instrumented at O1, origins 2, PIE and halt-on-error. All three cases pass.
This is executed-path coverage, not exhaustive execution, fully instrumented
libc or physical m68k qualification. The existing compiler/runtime is used;
no additional runtime is installed and no physical m68k target is tested.

All 78 quality tests, ShellCheck, actionlint and clang analysis pass, including
a final repeat after focused source freezes. Linux passes 20 conformance tests
and two actual FUSE runs: default and options 7 with pressure/interrupted daemon.
This is not a full geometry/hardware matrix. Source identities are recorded.
Two unique Amiga directories reproduce both handlers byte-for-byte. Normal/
probe guests are unchanged and copied, not rebuilt. Ten fresh image inputs
pass identity/idle preflight with no competing remote compiler/emulator;
coffin.service is inactive, not disabled. Environment is Cachy FS-UAE 3.2.35,
A1200/68040 max, 8 MiB fast/2 MiB chip, 255.5 MiB partitions, BFS 4 KiB/options
0/30 buffers and unchanged ROM, formatter and PFS3 inputs.

## Identities, restoration and evidence

- Qualified B-tree: `5095d6dd4e9e2984eb88a68779d1f5ea3fbb7e39c3c10a22f32de0479d2cb7da`.
- Rejected B-tree: `1069dea2be34004de74a58c8c56ebccbd75ff3ea1bf2f17f75892172aa0bcdc9`.
- Focused test: `847d809f16b63c9893d956ea6e26803e1dd6ad3add7b4765be4cfe31f817e096`.
- Frozen old host library: `2322570d8fc5103aadd2c5d638e0eef098b5071ecf581df4970b712f0b0e6b23`.
- Rejected normal: `a5cf2add3eca2a2df0946669246a0e05d220a32bad67f0a70c8195d2dd6b1cd8`.
- Rejected probe: `0b72401ec47ea57ce4b6b5d9b8a700179f237be1fbc7f4018dd0b10720e66011`.
- Retained normal: `7ec433160c6aa37c3ebbb5345097ef549638bde634dd8a3aa34e1b898a4a7d19`.

Withdrawal uses an exact patch, source `cmp`, and a clean normal rebuild
matching retained 7ec43316… byte-for-byte. Rejected source, patch and assembly
remain as text, not active implementation. Qualified old source already passes
the broader prior matrix; the three new cases separately pass its frozen library.
No new full restoration matrix is claimed. An initial identity command's
nonexistent memory_copy.s path and initial restore command's nonexistent direct
Make target are corrected; partial identity output and Make failure log remain.
Neither is a semantic test failure or discarded timing observation.

The [plan](../plans/bfs-btree-scratch-lifetime-v1.md) owns requirements; this
report owns rejection. [Text evidence](evidence/bfs-btree-scratch-lifetime-2026-10-01/)
contains old/new source and assembly, focused source, negative controls,
commands, build/identity/gate logs, raw normal/probe/control records, consumers,
independent extraction and adverse results. Copied TSV and completion/inventory
bytes remain exact. Only copied formatter C1 CSI text is ASCII-CSI normalized
with original/output hashes; copied log trailing whitespace is trimmed. No
executable, object, HDF or licensed asset is committed.

Three exact C snapshots and the full-context patch contain pre-existing or
context-line trailing whitespace. A staged whitespace check flags these bytes;
its log remains. They are losslessly packaged as wrapped base64 text rather
than changed or hook-bypassed. Decoding reproduces their original byte counts
and SHA-256 identities. Offline replay verifies those identities, all strict
normal/probe records, explicit counter equivalence and exact JSON outputs.
An initial scoped asset invocation mistakenly includes directories and fails;
its log remains. The corrected file-only scope check is distinct from the
repository-wide check and its older unchanged formatter C1-text limitation.
The new file-only asset scope passes. The actual repository-wide check returns
exit 1 for 68 older unchanged C1 formatter records; it is not represented as
a green repository-wide gate. No checker or older evidence is changed.

All ten completed systems are recoverably moved to Cachy's
`/home/fabian/.cache/bfs-performance/btree-scratch-lifetime-2026-10-01/`; all
twenty new TSV hashes match before/after. The bounded runner terminates only
its own emulator; none remains at archive time. No CI, push, PR, merge, release,
new user chat, soak, physical power/controller/media qualification or weaker
durability contract is introduced.
