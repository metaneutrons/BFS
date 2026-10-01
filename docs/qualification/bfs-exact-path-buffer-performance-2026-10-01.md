# BFS exact path buffer qualification

The exact-height insert/delete path buffers are retained. Eight fresh normal
comparisons show lower mean elapsed time in all six workloads and lower write
and delete time in every order-and-repeat-matched comparison. The overall
five-times-PFS3 goal remains unachieved: create, write and delete exceed it in
all candidate runs, and small read exceeds it in one. No guarantee or workload
was weakened, and no CI or external publication was performed.

## Normal measurements and decision

Four fresh baseline and four candidate images were measured serially after
functional qualification, twice per version in each filesystem order. The
baseline is the retained recycler checkpoint `2c6fa11`; the candidate changes
only insert/delete allocation from height-plus-one to height blocks. Both use
the same normal guest tool, fixed formatter, PFS3 handler, ROM, Workbench
assets, options=0, 4 KiB BFS geometry, 255.5 MiB partitions and isolated Cachy
FS-UAE 3.2.35 A1200/68040 configuration with 8 MiB fast RAM. No remote build or
other FS-UAE guest ran concurrently. All sixteen filesystem outputs passed
the unchanged strict schema-1 data/completion verifier.

Four-run means are microseconds. Ratios divide candidate BFS and candidate
PFS3 means; threshold counts use each candidate's same-run ratio.

| Workload | Baseline BFS | Candidate BFS | Change | BFS / PFS3 | Candidate runs above 5 times |
|---|---:|---:|---:|---:|---:|
| Create 40 | 262367.00 | 236494.50 | −9.861% | 13.968 | 4/4 |
| Lookup 400 | 121775.25 | 118126.50 | −2.996% | 1.645 | 0/4 |
| Small read 40 | 63743.75 | 56031.50 | −12.099% | 3.694 | 1/4 |
| Write 8 MiB | 489788.75 | 409487.25 | −16.395% | 11.210 | 4/4 |
| Read 8 MiB | 559661.75 | 508748.75 | −9.097% | 1.771 | 0/4 |
| Delete 40 | 217646.50 | 173871.25 | −20.113% | 17.583 | 4/4 |

Write and delete improve in all four matched comparisons, create in three.
The repeat BFS-first create is 1088 µs slower than its baseline. First
BFS-first lookup is 2622 µs slower. First
PFS3-first lookup is 5889 µs slower and small read 39424 µs slower; that small
read takes 86783 µs and is 6.191 times its PFS3 measurement. First BFS-first
read is 3135 µs slower. Every adverse observation remains in the
[raw summary](evidence/bfs-exact-path-buffers-2026-10-01/measurement-summary.md).

The PFS3 candidate BFS-first delete sample of 15169 µs is also retained. It
raises the candidate PFS3 mean and lowers the ratio of means; candidate delete
same-run ratios span 11.282–22.715, all above five. Candidate write spans
400765–416318 µs, and create 227711–249407 µs. These are descriptive samples,
not statistical precision or isolated causal attribution. Improvements
between separate qualification series must not be multiplied into a claimed
overall speedup. No probe elapsed times enter this decision.

The smaller request and effective warm-pool fault oracle, combined with the
observed mutation improvements, justify keeping the change. It removes an
unused allocation rather than adding another allocation policy. The adverse
read-side samples prevent any claim that all checked runs meet the target.

## Bounds and unchanged behavior

Both functions validate a nonempty tree's height before allocation. Descent
rejects depth at or beyond that height before touching path storage. Upward
propagation uses existing path indices; siblings and a newly split root have
independent block buffers. Collapse only reads existing root storage. Update
and rekey already allocate exactly height blocks. At valid geometry the
maximum request remains bounded by 32 × 65536 bytes, fitting 32-bit size_t.

At height one the path now uses the previously qualified four-slot BIO
recycler. Nested Free-Tree operations require distinct leases, and occupied
slots still fall back to the heap. No traversal check, node validation, CRC,
COW operation, allocator policy, write, publication fence or flush changed.
The on-disk format and all filesystem semantics remain shared by Amiga and
Linux. Probe ABI 11/deep schema 10 are unchanged; direct B-tree heap metrics
still exclude allocations inside BIO helpers.

Review also identified the pre-existing maximum-height root-split growth
edge: a height-32 split can increment height to 33, which later shape checks
reject. The smaller allocation neither introduces nor fixes that edge; no
path access requires the removed slot even during root creation. This
experiment does not claim a new maximum-height growth qualification.

## Functional and counterfactual proof

The final source passes 45 host suites / 441 tests normally and with
ASan/UBSan, 70 quality tests, ShellCheck, static analysis, normal/probe m68k
builds, 20 Linux conformance tests and actual FUSE mounts. LeakSanitizer is
unavailable in this macOS setup; `detect_leaks=0` was explicit. Existing
split/merge, deeper-tree, recursive read failure, natural mounted allocator,
strict data/graph/fsck, buffered persistence, recovery, snapshot and low-space
oracles were not removed or relaxed. Independent source/test review found no
new blocker.

The added fault test seeds a real height-one tree, warms all four actual cache
scratch buffers, and fails the next ordinary allocation in the actual cache
implementation. Insert/update/delete, including emptying the tree, must make
zero such requests. It checks roots, heights, values, deleted lookups, idle
leases and complete tracked cleanup. The final implementation passes.

An isolated negative-control build used the exact `2c6fa11` B-tree source
snapshot, with the same final test and other core sources. Its first four
fault tests pass; the new test fails at its first armed insert with
`BFS_ERR_NOMEM`, as expected from the old two-block request. This is a
counterfactual success criterion, not an unresolved production failure. The
snapshot is a generated local build input, not a second production algorithm.

## Identities and evidence

Baseline normal handler is
`6ecdf122885904491dcf8ac2925d1218aa5e6a77344d88f80cf4940695ef62af`;
candidate normal is
`c4d4241a45b84c768ec9498e77144e650812e31576c952925f4ed1de65be0d5a`.
Candidate probe is
`be4162868ebfb7a67ff3f44203aec7777edc579887a0f27ca487e4ee9d3f9683`;
the common normal guest tool is
`1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80`.

The [evidence directory](evidence/bfs-exact-path-buffers-2026-10-01/) preserves
all raw outputs, completion markers, machine information, configurations,
emulator/builder/runner logs, functional/negative-control logs, input hashes
and SHA-256 manifest. Only trailing horizontal whitespace in copied
logs is normalized; timing TSVs are unchanged. No licensed ROM, Workbench
assets, disk images or handler binaries are committed. Completed preceding
recycler images remain privately archived on Cachy at
`/home/fabian/.cache/bfs-performance/buffer-reuse-2026-10-01` rather than deleted.

Real power cuts, controller cache/flush lies and specific hardware media remain
separate device-bound qualification. No push, PR, merge or release occurred.
