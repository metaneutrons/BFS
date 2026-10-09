# BFS detailed listing profile

The 8 October 2026 detailed profile prioritizes inode lookup and directory-node
access for the next performance step. Final inode validation is a much smaller
sampled interval. This is diagnostic evidence, not a new production speedup:
the production handler remains byte-identical to M3, and the <=5x PFS3 goal
remains open. The preceding
[production comparison](bfs-metadata-listing-performance-2026-10-08.md)
still governs elapsed-performance claims.

## Workload and sampling

Two fresh runs alternate filesystem order, using the same schema4 workload
body with deep schema14 output and probe ABI15. ExAll requests ED_COMMENT.
Separate directories contain 40, 400 and 1,000 entries for each listing API;
together the fixtures contain 2,880 files. First pass and ten additional passes
are separate phases. Setup and an explicit flush precede the listing timers;
neither first pass nor repeated passes establish a cold-cache qualification.

The environment is Cachy KVM, FS-UAE 3.2.35/Xvfb, A1200/68040 at maximum speed,
2 MiB chip and 8 MiB fast memory, Kickstart 47.102 and Workbench 3.2 commands.
Each run uses fresh 256 MiB RDB images, 4 KiB BFS blocks and RDB Buffers=30,
with unchanged mount and commit policy. The stopped GitHub runner and other
emulators do not compete with these runs; host scheduling is uncontrolled.

Every detailed scope counts all calls and times every seventeenth call.
Stride17 avoids a fixed even-stride alias between alternating root/leaf views.
The clock is EClock at 709,379 Hz; short intervals can quantize to zero. Calls
include error returns. Scope intervals are inclusive and nested: inode read
contains search and validation, search contains node access, and ExAll fill
contains metadata reads. Their means must not be added, subtracted or scaled
into exclusive CPU totals, production time or an expected speedup. Sample
selection is deterministic, not a random estimator of all calls.

## Repeated ExAll over 1,000 entries

Both runs have the following exact counts for ten additional passes:

| Work | Calls or views |
| --- | ---: |
| Inode reads and detailed inode searches | 10,100 each |
| ExAll fill callbacks | 10,110 |
| Inode node views / resident views | 15,220 / 14,660 |
| Directory node views / resident views | 2,794 / 1,641 |
| Raw block reads / node CRC read computations | 1,713 / 1,713 |
| Buffer leases / releases | 7,721 / 7,721 |
| Actual B-tree malloc / free calls | 111 / 111 |
| Indexed hints / last-leaf hints | 100 / 2,390 |

Instrumented phase elapsed times are 1,316,665 and 1,348,408 microseconds.
The mean sampled inclusive intervals below compare opposite filesystem order;
they are not production timings.

| Detailed scope | BFS first, microseconds | PFS3 first, microseconds |
| --- | ---: | ---: |
| Inode read | 78.223 | 76.757 |
| Inode search | 64.119 | 61.822 |
| Final inode validation | 3.066 | 3.519 |
| Inode node view | 16.469 | 19.959 |
| Directory node view | 172.807 | 166.016 |
| Inode binary search | 5.798 | 4.461 |
| Directory binary search | 4.375 | 6.611 |
| Node structure validation | 32.395 | 25.050 |
| Cache peek | 4.529 | 5.246 |
| Buffer lease | 4.446 | 5.263 |
| Buffer release | 4.760 | 3.397 |
| ExAll fill | 82.227 | 81.166 |

The reproducible call counts and larger search intervals prioritize reducing
validated traversal work rather than weakening inode validation. Directory
views have substantially lower residency than inode views: 1,153 directory
views and 560 inode views take the nonresident path. Those counts equal the
1,713 raw reads and CRC computations in this phase. Their much larger sampled
intervals make cache pressure a concrete candidate, not a proven sole cause.
Raw-read intervals alone are approximately 152.6/168.9 ms. CRC and structure
validation remain separate nested work, so subtracting these times from phase
elapsed time would not produce a reliable exclusive residual.

Buffer leases mostly use the existing scratch pool; 7,721 leases are not
7,721 heap allocations. Enlarging or redesigning that pool is not justified by
the allocation count alone. Likewise, the 64-slot inode hint table does not
cover this working set: its 100 indexed hits are consistent with overflow
retries, not useful coverage of 1,000 distinct inodes across passes.

## Corrected diagnostic accounting

The first schema13 run reports guest PASS but fails strict host verification
in APPEND_READ_1280K: one sealed commit has four aggregate node writes but zero
attributed free-tree writes. A timer-triggered commit exposes a pre-existing
diagnostic omission. Deferred node finalization counted aggregate writes,
whereas per-tree attribution existed only on direct finalization. The second
schema13 run passes the verifier. Both original runs and the failing log are
retained; neither is substituted for corrected schema14 evidence.

One shared probe helper now attributes both direct and deferred finalization.
DEFERRED_NODE_WRITES is a subset of NODE_WRITES; the five layout classes must
partition all node writes. The existing sealed-commit/free-tree and free-phase
partition checks remain strict. Both fresh corrected schema14 runs report
guest PASS and pass the strict verifier.

Write categories use the immutable B-tree layout pointer, so auxiliary trees
with inode layout belong to the inode write class. Node-view categories and
detailed tree scopes instead identify the current live tree instance.
Free-tree phases reflect context at finalization, not necessarily the original
mutation site. Write counters count attempts after successful CRC finalization,
before the BIO write; they are not counts of successfully persisted writes.
Retries and failures may therefore contribute attempts. These semantics apply
only to the disposable profiler, not the on-disk format or production policy.

## Validation and scope

The m68k production and diagnostic handler builds pass against the real NDK.
A rebuild after adding host oracles reproduces all three measured binary
identities below. The production handler is byte-identical to pinned M3.
Four new host oracles pass ASan/UBSan with leak detection on Cachy. They cover
stride/reset calibration, disabled tree scopes, inode-read errors, direct and
deferred layout attribution, and deferred finalization through the real core
and block cache. Test-only Exec/Timer headers model behavior, not native ABI.

All 106 quality tests pass, including 62 benchmark-verifier tests. ShellCheck
and core/host Clang static analysis pass. The Mac full host run was stopped
after repeated pre-main launch stalls; a sampled process remained at
_dyld_start with no test code running. A Linux GCC 16.2 build rejects existing
mock cache callbacks with array-bounds diagnostics. Those logs are retained;
warnings were not disabled. The full Linux Clang 22.1.8 host suite passes
562 tests across 60 suites, including the four new probe oracles. An
independent Luna review finds no
actionable defects in schema14 accounting, the verifier or the new host tests;
its layout-versus-instance attribution limitation is stated above. Failed-I/O
retry injection is not included in this profiler qualification.

This step does not qualify new production elapsed performance, durable or
handler-cache-cold timings, native AROS, Linux FUSE or physical hardware. The
earlier M1-M3 retention conditions and complete pre-push checks remain in the
[plan](../plans/bfs-metadata-listing-performance-v1.md). No CI, push or merge
is part of this step.

## Next bounded experiment

Measure the same checked workload with 30, 64 and 128 metadata-cache slots,
without changing block size, commit policy or validation. Report memory cost,
raw reads, CRC work and paired production timings separately from profiler
results. This tests whether working-set pressure justifies a cache-policy or
bounded traversal change; a larger default is not accepted in advance.

If locality remains the relevant limit, design bounded common-core batching
that gathers directory entries, orders validated inode lookups for locality,
then emits entries in original directory order. It must cover ED_COMMENT,
hidden-comment validation, patterns, original error order and retrying an
unconsumed overflow entry. A proposal restricted to fixed-size non-comment
ExAll output does not optimize this benchmark. No batching is implemented or
claimed as a speedup here.

## Identities and reproducible evidence

| Artifact | SHA-256 |
| --- | --- |
| M3 production handler | `adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27` |
| ABI15 diagnostic handler | `96f0d91fee63b77059005510cd160a8283657c595efe8ae449d94b170755189f` |
| Deep schema14 guest | `7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661` |
| Pinned main Linux formatter | `f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55` |

The [evidence directory](evidence/bfs-listing-detail-2026-10-08/) retains exact
schema13 and schema14 runs, source diffs and identities, binary digests, build
and validation logs. summarize-detail.py requires the exact two corrected
run directories, verifies each with the strict host verifier, and recomputes
summary.tsv directly from their BFS TSVs and the shared scope macro. It emits
sampled intervals and means, never extrapolated totals. Format progress logs
are losslessly base64 encoded with their original SHA-256; timing TSVs are
unchanged. SHA256SUMS covers this evidence independently of the immutable
preceding-stage manifest. Licensed assets, binaries and HDFs remain outside
Git.
