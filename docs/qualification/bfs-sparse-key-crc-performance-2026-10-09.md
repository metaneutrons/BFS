# BFS sparse directory-key CRC performance experiment

The candidate reduces the exact read-CRC cost of zero-heavy directory leaves.
All correctness checks and all 40 required comparison runs pass their strict
data/identity/inventory checks. The candidate is provisionally retained locally
for its large-listing improvement, not accepted as a general performance win.
The <=5x PFS3 goal stays open, and possible small-write regressions require a
focused follow-up before publication. No format migration, cache-capacity
change, remote CI or publication is included.

## Implementation and boundaries

`bfs_crc32_sparse` inspects complete 16-byte units and advances zero runs of
at least 64 bytes using the existing immutable zero transform. It hashes
every other byte through the ordinary CRC. It does not skip semantic fields
or assume bytes after `name_len` are zero. A default-false operations flag
opts only directory leaves into this exact CRC implementation for their used
key prefix. Value bytes, internal nodes, node-write CRCs and ordinary file
data retain their previous paths. CRC comparison, structure, expected-level,
parent-bound validation, cache proofs, dirty images and flushes are unchanged.

Directory keys occupy 264 bytes. The four-character benchmark names leave
251 trailing name bytes zero, so this is a measured candidate for the fixed
short-name fixtures, not a universal directory speed claim. Long/nonzero
legacy tails remain fully covered. m68k operations tables grow from 20 to
24 bytes; cache slots remain 54 bytes and cursors 40 bytes. There is no new
allocation, table or block buffer. In-tree tables remain source-compatible
after rebuilding, not binary-compatible with separately compiled old tables.

I/O batching was not selected: existing aggregate diagnostics do not show
that validated successor leaves are physically contiguous, and the current
bulk BIO path cannot generally aggregate mixed resident/nonresident buffers.
No new read-ahead or sibling-authority assumption is introduced.

## Isolated m68k signal

Two fresh A1200/68040-max emulator boots each pass 1297 bitwise-oracle vectors,
2594 positive register-preservation witnesses and two ABI negative controls.
Both slots use symmetric, exactly typed `size_t` adapters and an explicit
`uint32_t` length conversion; the two C types differ on this toolchain despite
equal width. The corrected fail-fast rebuild reproduces the timed protocol2
binary. A strict consumer independently regenerates the
vector digest and validates input/config identities, completion, counters,
five cases and six alternating paired samples per case per boot.

| Synthetic input | Median sparse/ordinary CRC time | Paired range | Slower pairs |
| --- | ---: | ---: | ---: |
| 264-byte short-key density | 0.4081 | 0.3782–0.4248 | 0/12 |
| 4096 zero bytes | 0.1040 | 0.0783–0.1209 | 0/12 |
| 128 nonzero + 384 zero bytes | 0.4449 | 0.3890–0.5000 | 0/12 |
| 4096 dense bytes | 1.1313 | 1.0313–1.5781 | 12/12 |
| 64 dense bytes | 1.2056 | 1.0220–1.3331 | 12/12 |

The key fixture is a synthetic density/alignment pattern, not an actual
saved node. All samples, including noisy dense/zero controls, remain in the
[micro summary](evidence/bfs-sparse-key-crc-performance-2026-10-09/micro-summary.json).
There is no warm-up or randomized starting order. These timings establish
neither whole-filesystem speed nor native/physical-media performance. Dense
CRC work is slower, which is why ordinary file data is not opted in.

The initial direct-cast protocol1 probe is not qualifying evidence. A later
type guard failed, but a chained root command printed the old executable's
digest and was initially mistaken for a successful rebuild. The fail-fast
reproduction exposed this error. Its failed log, initial raw reports and
consumer remain explicitly archived; the table uses only two fresh protocol2
boots after the typed-adapter correction. No production/core code changed
as part of that probe repair.

## Production pilot and diagnostics

Four fresh schema4 pilots compare M5 and the candidate with both filesystem
orders and unchanged actual RDB capacity of 30 buffers. Each passes strict
data checks and the three installed guest-binary identities. Two-pair medians
are exploratory; every phase and PFS3 calibrator remains in the
[pilot summary](evidence/bfs-sparse-key-crc-performance-2026-10-09/pilot-summary.log).

| Repeated listing | Median sparse/M5 | Paired range |
| --- | ---: | ---: |
| ExAll 40 entries | 1.0470 | 1.0136–1.0804 |
| ExNext 40 entries | 1.0233 | 0.9979–1.0486 |
| ExAll 400 entries | 0.8867 | 0.8621–0.9114 |
| ExNext 400 entries | 0.9418 | 0.9226–0.9609 |
| ExAll 1000 entries | 0.9060 | 0.8829–0.9290 |
| ExNext 1000 entries | 0.8850 | 0.8526–0.9173 |

Small phases and unrelated controls fluctuate substantially. No causal
regression is excluded by four pilots, and no sample is discarded. Legacy
`LIST_EXALL_400`/`LIST_EXNEXT_400` scan 40 entries ten times; only the explicit
400-entry phases above use actual 400-entry directories. Setup is outside
timing; first pass is not a handler-cache-cold claim. All six listing fixtures
together hold 2880 files, including empty comments requested through ED_COMMENT.

Four separate schema15/ABI16 diagnostics also pass strict verification.
All 24 matched listing rows have identical non-timing counters, including
reads, CRC calls, sample counts and tree/cache views. Repeated warm 40-entry
listings have zero reads/CRCs in both implementations. At 400 entries,
ExNext/ExAll retain 680/692 reads and CRCs; at 1000 they retain 1840/1713.
The [raw comparison](evidence/bfs-sparse-key-crc-performance-2026-10-09/deep-counter-comparison.log)
records sampled CRC ticks, without interpreting overlapping sampled scopes
as exclusive CPU time or elapsed forecasts.

## Complete production comparison and decision

Eight fresh matched pairs per mode (32 runs), plus the four pilots and four
diagnostics, complete the strict 40-run inventory. Every requested run,
phase, data check, installed input and actual 30-buffer capacity verifies.
No outlier is discarded. The [full summary](evidence/bfs-sparse-key-crc-performance-2026-10-09/full-summary.log)
retains every individual value and control. Derived medians of per-run ratios,
not ratios of mean times, are in
[decision-metrics.json](evidence/bfs-sparse-key-crc-performance-2026-10-09/decision-metrics.json).

| Repeated listing | Normal sparse/M5 | Durable sparse/M5 | Normal BFS/PFS3 | Durable BFS/PFS3 |
| --- | ---: | ---: | ---: | ---: |
| ExAll 40 | 1.0175 | 0.9935 | 3.93 | 3.57 |
| ExNext 40 | 1.0091 | 1.0110 | 2.65 | 2.65 |
| ExAll 400 | 0.8542 | 0.8861 | 15.79 | 15.25 |
| ExNext 400 | 0.8033 | 0.9601 | 4.67 | 4.86 |
| ExAll 1000 | 0.8762 | 0.8856 | 15.97 | 16.98 |
| ExNext 1000 | 0.9590 | 0.9391 | 4.90 | 4.75 |

Large ExAll medians improve by 11–15%; ExNext improves by 4–20%. Normal
ExAll 400/1000 has 3/8 and 1/8 slower pairs, durable 2/8 and 1/8. Normal
ExNext 400/1000 has 1/8 each, durable 4/8 and 2/8. Warm repeated 40-entry
medians change by at most about 1.8%; they have no read CRC to accelerate.
The unchanged PFS3 repeated-listing calibrator medians range from 0.9826 to
1.0773 over M5-run PFS3, with the full per-phase distributions retained.
These are emulator short-name/same-mount results, not universal gains.

| Control or small-write phase | Normal sparse/M5 | Durable sparse/M5 |
| --- | ---: | ---: |
| Append 4KiB / 1MiB | 1.0465 | 1.0695 |
| Append 1KiB / 256KiB | 1.0262 | 1.0181 |
| Create 40 | 0.9783 | 1.0604 |
| Delete 40 | 1.0000 | 0.9995 |
| Write 8MiB | 0.8899 | 0.9770 |
| Read 8MiB | 0.9571 | 0.9516 |

Append 4KiB is slower in 5/8 pairs in each mode, with 4.7%/7.0% slower
medians and ranges 0.9078–1.6476 / 0.9554–1.6580. Append 1KiB is slower in
6/8 normal and 5/8 durable pairs. Durable create is slower in 6/8 pairs.
Scheduling/control scatter prevents isolated causal attribution, but it does
not justify dismissing these measurements. This is not an "everything gets
faster" result. A focused paired append/create confirmation, with the same
binary identities and PFS3 calibrators, is required before broader acceptance.
Unrelated read/write control improvements are not attributed to sparse CRC.

The candidate has 47 normal and 51 durable individual phase ratios above5,
out of 184 in each mode. All large ExAll runs remain above5; repeated ExNext
1000 exceeds5 in 4/8 normal and 2/8 durable runs. Normal ExNext400 stays below5
in all eight runs, but durable exceeds5 in four. The global goal is explicitly
not achieved. Retention is only a bounded, reversible local candidate decision:
keep the exact core change and evidence, require small-write confirmation,
and continue investigating fresh leaf I/O cost rather than another generic
scan-cache policy. No new read-ahead/adjacency claim is made.

## Verification and evidence

The frozen candidate passes 577 Clang host and 577 ASan/UBSan tests in 62
suites each, including leak detection. Seven independent CRC property tests
cover zero lengths, offsets, thresholds, single-byte perturbations, random
inputs and chaining. Four read-path integration tests cover 1K/4K/64K blocks,
both CRC modes, nonzero legacy used-key tails and unused padding, cache
invalidation, and corruption rejection before callbacks.

All 117 quality tests and static analysis pass. The primary-agent rerun passes
25 strict summary oracles, 21 direct-conformance contracts and 30 bounded
fault/qualification contracts. Independent read-only core/probe/consumer
reviews and the requested coverage/type hardening are retained in
[review.log](evidence/bfs-sparse-key-crc-performance-2026-10-09/review.log).
The corrected production and ABI16 handlers rebuild to their original measured
digests. All 52 Amiga runtime checks under the checksum-verified AROS m68k ROM
and all nine Kickstart compatibility scenarios pass. These are m68k emulator
results, not native AROS or Linux FUSE qualification. Linux also reruns all
21 conformance and 30 fault/qualification contracts successfully.

The initial isolated Linux contract run failed two cases solely because the
export had no Git metadata for recording the base commit. Its log is preserved.
Importing the actual base-commit Git bundle into a separate metadata directory
and rerunning without code/oracle changes resolves that setup failure. No
commit, branch change or reset is made in the user's worktree.

The [text-only evidence bundle](evidence/bfs-sparse-key-crc-performance-2026-10-09/README.md)
contains the isolated source inventories, reconstructible eight-file patch,
compiler-size proof, commands, input digests, strict consumers, raw reports
and logs. ROMs, Workbench/PFS3 binaries and disk images are not included.
All 3258 preceding sealed evidence entries verify unchanged. The final bundle
is sealed by its own `SHA256SUMS`; the text-only audit and scoped patch check
pass. There are no missing required comparison runs. No commit/push/merge,
remote CI restart or live emulator is left running. The small-write follow-up
and global <=5x performance acceptance remain open.
