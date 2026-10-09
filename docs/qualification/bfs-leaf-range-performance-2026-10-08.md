# BFS validated leaf range performance

The bounded leaf-range candidate preserves format v3 and the existing
validation, snapshot, recovery and commit policy. The full correctness suites
pass. All 44 retained runs verify, including eight normal and eight
flush-inclusive comparisons per production handler. Larger repeated listings
improve, but small hot listings and append phases show regression signals.
Retention remains provisional; this is not a universal upgrade and the
<=5x PFS3 target remains open.
Local work remains on `fix/metadata-listing-performance`, based on
`07216b7f17912c9f28b9b4a920caaa6828b00ccb`. Nothing is committed, pushed or
merged, and the GitHub runner remains stopped.

## Shared core lookup

M5 in the [existing plan](../plans/bfs-metadata-listing-performance-v1.md)
adds 64 sorted leaf routes to the filesystem-owned inode lookup cache. Each
route stores a block number and inclusive u32 key endpoints, never inode/value
bytes or a borrowed node pointer. Exact-key hints are tried first, then the
range candidate, then the old last-leaf shortcut and ordinary traversal.

Owner, BIO, layout operations, root, generation and mutation epoch guard the
routes. Owner/backend/layout changes also clear exact-key slots. Every hit
obtains a currently validated resident leaf, checks its level and endpoints,
and verifies the actual requested key. A dense key offset is only a checked
index candidate; sparse keys use ordinary binary search. Missing keys and any
mismatch fall back to traversal, without caching a negative result. The
current value is copied before another BIO call and ordinary inode validation
still follows. Missing or saturated epochs disable the shortcuts. Reset at
tree/BIO lifetime changes remains required; pointer identity is not an
incarnation token.

The route table is fixed-size and allocation-free. No adapter-specific copy
of this logic, ExAll output batching, block-cache policy change, larger cache
default or on-disk migration is included. The diagnostic leaf-hint counter now
combines legacy and range hits; it is not a separate range-hit counter.

## Memory bounds

Compiler-emitted size constants establish the lookup storage per mounted
filesystem. These are structure sizes, not measured resident process memory.

| ABI | M3 exact-key cache | Final combined cache | Additional bytes |
| --- | ---: | ---: | ---: |
| m68k | 1,792 | 2,592 | 800 |
| macOS arm64 host | 2,048 | 2,864 | 816 |

Cursor sizes remain 40/48 bytes respectively. The block-cache descriptor is
54 bytes on m68k and 72 bytes on the host ABI. At 4 KiB blocks, the m68k
resident block buffers, descriptors and hash buckets occupy 124,628 bytes at
30 slots, 265,856 at 64, and 531,712 at 128. These computed allocations exclude
the unchanged 88-byte cache object, allocator overhead and up to four retained
scratch buffers. Increasing from 30 to 128 therefore adds 407,084 allocated
bytes before those exclusions; the production comparisons stay at 30.

## Exploratory cache capacity comparison

Six fresh M3 runs use two filesystem orders at each of 30, 64 and 128 buffers.
All pass the strict verifier; the actual unique DH1/BFS0 RDB partition records
the requested `dos_env.num_buffer`. PFS3 configuration is unchanged.

| M3 buffers | Repeated ExAll 400 BFS mean, microseconds | Median BFS/PFS3 | Repeated ExAll 1,000 BFS mean, microseconds | Median BFS/PFS3 |
| --- | ---: | ---: | ---: | ---: |
| 30 | 392,030 | 18.74 | 1,032,538 | 15.66 |
| 64 | 350,302 | 16.98 | 1,097,305 | 19.00 |
| 128 | 175,990 | 8.54 | 910,458 | 18.14 |

Two runs do not qualify a new default or a causal cache-policy speedup. One
30-buffer PFS3 1,000-entry measurement is unusually slow, reducing its ratio;
the absolute BFS means prevent interpreting that ratio as a BFS improvement.
Larger buffers alone do not satisfy the goal. This pilot changes only capacity;
it has no diagnostic read/CRC counters and does not establish their reduction.

## Production qualification

The final identified handler is compared with pinned M3 using eight fresh
runs per handler in normal mode and eight per handler in durable mode.
Odd runs start BFS first, even runs PFS3 first; each adjacent candidate/M3
pair uses the same filesystem order. Two further diagnostic runs use the
final probe. The 10 historical pilot runs remain separate from these 34 runs.
The final summary requires the exact 44-directory inventory, without filtering
failures or outliers.

All comparisons use the original identified schema4 guest; deep runs use the
identified schema14 guest and ABI15 probe. Six real listing fixtures contain
2,880 files across separate 40-, 400- and 1,000-entry directories for each API.
ExAll requests ED_COMMENT. First pass after setup and ten additional passes
are distinct phases; setup and its explicit flush are outside listing timers.
The listing files have no comments: the handler returns an empty comment
before the hidden-comment lookup. This workload does not measure nonempty
comment retrieval or a separate comment-tree optimization.
Neither phase is a handler-cache-cold or device-cold claim. Legacy rows named
`LIST_*_400` still mean ten passes over 40 files.

The measurement host is Cachy KVM, FS-UAE 3.2.35/Xvfb, A1200/68040 at maximum
speed, 2 MiB chip and 8 MiB fast memory, Kickstart 47.102 and Workbench 3.2
commands. Fresh 256 MiB RDB images use 4 KiB BFS blocks and 30 buffers.
Mount/commit and PFS3 settings are unchanged. Tests and other emulators finish
before timing begins; host scheduling remains uncontrolled. Diagnostic timings
and nested sampled scopes are not production times or exclusive CPU totals.

The completed normal series has the following adjacent candidate/M3 time
ratios. Values below one favor the candidate. The phase-mean reduction is
computed from BFS means, not changes in PFS3 timing; it differs from the
median paired ratio.

| Normal phase | Median candidate/M3 | Paired range | Phase mean reduction |
| --- | ---: | ---: | ---: |
| Actual 400, repeated ExAll | 0.912 | 0.716–0.995 | 10.30% |
| Actual 1,000, repeated ExAll | 0.870 | 0.826–0.975 | 11.64% |
| Actual 1,000, repeated ExNext | 0.884 | 0.859–0.935 | 10.61% |
| Actual 40, repeated ExAll | 1.045 | 1.014–1.320 | -7.91% |
| Append 1 MiB in 4 KiB steps | 1.072 | 0.767–1.895 | -11.24% |
| Append 256 KiB in 1 KiB steps | 1.058 | 0.909–1.607 | -13.04% |
| Read 8 MiB control | 1.046 | 0.891–1.187 | -4.06% |
| Write 8 MiB | 0.894 | 0.575–1.460 | 11.62% |

All eight repeated large-ExAll pairs improve, as do all eight 1,000-entry
ExNext pairs. All eight actual-40 repeated ExAll pairs slow down. This is not
a universal performance upgrade: extra hint guards can add work to an already
hot exact-key path. The append regression signals require comparison across
modes; wide ranges and movement in the read control limit causal claims about
writes or general speedup. The candidate's normal repeated 400/1,000-entry
ExAll BFS/PFS3 medians remain 18.35/18.26, with all eight runs above five in
both phases. The 1,000-entry repeated ExNext median is 5.48, with seven of
eight above five. No goal is closed by these results.

The durable series includes ACTION_FLUSH in each mutating phase, not in
read-only listing phases. It confirms the large-listing benefit, with the
following paired ratios and phase-mean reductions:

| Durable phase | Median candidate/M3 | Paired range | Phase mean reduction |
| --- | ---: | ---: | ---: |
| Actual 400, repeated ExAll | 0.853 | 0.695–0.954 | 17.13% |
| Actual 1,000, repeated ExAll | 0.890 | 0.782–1.044 | 9.81% |
| Actual 1,000, repeated ExNext | 0.921 | 0.805–0.980 | 8.48% |
| Actual 40, repeated ExAll | 1.031 | 0.685–1.077 | 2.33% |
| Append 1 MiB in 4 KiB steps | 1.035 | 0.910–1.377 | -5.65% |
| Append 256 KiB in 1 KiB steps | 1.025 | 0.790–1.419 | -7.46% |
| Read 8 MiB control | 0.996 | 0.790–1.291 | 2.13% |
| Write 8 MiB including flush | 1.040 | 0.820–1.075 | 0.35% |

All eight 400-entry repeated ExAll pairs improve; seven of eight 1,000-entry
pairs improve. The small repeated ExAll phase is slower in seven of eight
pairs, despite its favorable mean caused by a slow M3 outlier. Its median
regression is therefore not erased by presenting that mean. Both append
phases have slower medians in both modes; no write improvement is claimed.
The durable candidate's repeated ExAll BFS/PFS3 medians are 17.10/18.32 for
400/1,000 entries, with all runs above five. Repeated ExNext medians are
4.32/5.22, with one/five of eight above five respectively.

## Diagnostic mechanism and remaining work

Both fresh final probe runs reproduce these counts for ten additional ExAll
passes over 1,000 entries. The M3 counts come from the retained schema14
[detailed profile](bfs-listing-detail-profile-2026-10-08.md), not another
production elapsed-time comparison.

| Counter | M3 | Final range candidate |
| --- | ---: | ---: |
| Inode reads | 10,100 | 10,100 |
| Exact-key hint hits | 100 | 100 |
| Generic leaf-hint hits | 2,390 legacy | 9,440 legacy plus range |
| Inode node views / resident views | 15,220 / 14,660 | 1,120 / 560 |
| Directory node views / resident views | 2,794 / 1,641 | 2,794 / 1,641 |
| Raw BIO reads / node CRC computations | 1,713 / 1,713 | 1,713 / 1,713 |

Generic hint counters cover B-tree searches, not exclusively inode searches;
node-view counters identify the live tree. Hinted peeks are not node views or
logical cache-read calls. The large reduction in traversal-helper views is
not a 93% CPU reduction. Physical reads and CRC computations do not decrease,
and directory views remain unchanged. The observed production gain is much
smaller than the helper-count reduction. This explains why location hints
alone are insufficient; cache locality and the remaining validated directory
access need a separate bounded experiment.

M5-A1 and M5-A2 are satisfied by the focused and complete correctness evidence.
M5-A3's matched normal/durable measurements and memory accounting are complete,
but retention is provisional because the hot small-listing regression repeats
across modes and append medians also worsen. Before proposing a merge, test
the plan's guarded exact-key early-return hypothesis and qualify the revised
binary. Do not weaken identity, epoch or current-leaf validation to remove
that overhead. Handler-cache-cold timing and the global <=5x goal remain open.

## Correctness and review

The complete Clang 22.1.8 host and ASan/UBSan suites each pass 565 tests across
60 suites; sanitizers include leak detection. The 11 inode lookup-hint tests
cover distinct-key range hits with exact/last-leaf routes disabled, sparse and
missing keys, endpoint mismatch, owner/BIO/layout mismatch with an active
exact hint, bounded replacement, collisions, missing/saturated epochs,
mutation/COW, deletion/unlink, raw leaf/internal-node mutation, recovery,
remount and snapshot separation. Existing cache failure/epoch and B-tree
fault-injection oracles also pass.

All 113 quality tests pass, including seven capacity-harness and 62 benchmark
verifier tests. Quality counter-probes, ShellCheck and core/host Clang static
analysis pass. m68k production, probe, test and public CLI builds pass. The
Amiga integration suite passes all 52 checks under the checksum-verified AROS
ROM. Nine Kickstart compatibility scenarios pass, including snapshots, legacy
replacement, future-version/options refusal and a damaged version slot.
These are m68k emulator claims, not native AROS or Linux FUSE qualification.

A read-only follow-up confirms that real hidden comments use the live
directory tree, not an auxiliary tree misclassified as OTHER. Their cursor
scans are not counted as `BTREE_SEARCH_CALLS`, but their node views would
belong to DIR. The retained M3 1,000-entry repeated ExAll phase has zero
Free/Refcount/Other views and 10,100 inode reads: 10,000 entries plus 100
overflow retries. Its 2,490 search-hint hits leave 7,610 traversals and exactly
15,220 inode node views. This supports investigating inode traversal; it
does not attribute the measured gap to comment scans. A real-comment workload
would be a separate additional qualification.

Independent Luna review identified and resolved an owner/backend identity gap
in the exact-key cache and a collision test that could be masked by a range
hit. The final review found no actionable defect; it did not substitute for
runtime tests. Two initial test oracles wrongly expected an empty range cache
after inode validation failure or reload. A positively searched leaf may be
remembered before inode validation, and reload legitimately validates the root
inode. Corrected tests check unchanged inode rejection and stale-route removal.
The failing sanitizer test exited before fixture cleanup, producing leak
reports; corrected focused and complete sanitizer suites pass. All original
failure logs remain. A conformance attempt on an incomplete isolated source
export lacked `.github/workflows/ci.yml`; after restoring the fixture, all 21
conformance tests pass without changing code or relaxing an oracle.

## Identities and evidence

| Artifact | SHA-256 |
| --- | --- |
| M3 production handler | `adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27` |
| Final range production handler | `79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4` |
| Final range diagnostic handler | `a46a153a387de6cac773aee8bc6b757ce0bccf7a6dbc03b2c6ec8f8f8cc7981c` |
| Schema4 production guest | `798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52` |
| Schema14 diagnostic guest | `7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661` |
| Pinned formatter | `f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55` |
| Superseded pilot range handler | `cc77f452e54dd192694ae14010c8d597f8fecc124af9d21db4c5e5cc69602d14` |

Five read-only summary counter-probes also pass: exact inventory, installed
input identities, duplicate/schema rejection and actual-capacity rejection.

The [evidence directory](evidence/bfs-leaf-range-performance-2026-10-08/)
contains raw text outputs/configurations, strict summarization and reproduction
scripts, build/test logs, input/source identities and the integrated patch
against the stated base. Licensed assets, executable binaries and HDFs remain
outside the repository. Amiga Format control-byte logs are losslessly base64
encoded with their original byte digests; timing TSVs are untouched. Earlier
873-file production and 103-file detailed-profile manifests verify unchanged.
Installed handler/PFS3/guest copy digests match for every run. They were
collected after measurement; they are not forensic hashes of the embedded RDB
filesystem modules. The measured runner is retained separately from the
reproduction script, whose additional post-run digest recording changes no
timed workload. Run `summarize.py` to reproduce the strict inventory/statistics
and `test-summary.py` for the rejection counter-probes.
