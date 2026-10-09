# BFS scan admission performance experiment

Two-touch directory-leaf admission saves reads in the 400-entry ExAll pilot,
but adds reads to warm small listings and both 1,000-entry listing APIs.
The candidate is rejected. Its eight source/test changes are removed exactly;
the retained M5 implementation and ABI16 diagnostics remain unchanged. The
<=5x PFS3 goal stays open. No full normal/durable series is started.

## Admission policy

Only directory scans opt in to an optional shared BIO read hook. A first
absent leaf returns fresh device bytes without admitting them to the block
cache; its block number enters a FIFO history bounded by the cache slot count.
A later qualifying miss admits the block through the existing clean LRU
replacement path. Validated resident views retain ordinary MRU access,
including dirty images. An unchanged consumed cursor leaf visited solely to
reconstruct the successor path does not record or qualify reuse. Internal
nodes and demand reads retain ordinary admission.

The history contains no bytes or validation proofs. Misses still receive
full CRC, structural, expected-level and parent-bound checks. Mutation and
invalidation clear the history without an extra epoch advance; failed device
reads do not record it. All-dirty victim protection remains unchanged. Failure
of the optional history allocation falls back to ordinary admission instead
of failing cache initialization. No cursor path retention, read-ahead, flush
change or on-disk migration is included.

Compiler constants show unchanged m68k cache slots at 54 bytes and cursors
at 40 bytes. The cache handle grows from 88 to 100 bytes and stores 30 four-byte
block IDs: 132 additional bytes per 30-buffer cache, without additional block
buffers. The B-tree operations table grows from 20 to 24 bytes; the BIO table
also adds one optional function pointer. In-tree designated initializers remain
source-compatible after rebuilding, not binary-compatible with separately
compiled historical tables.

## Production pilot

Four fresh schema4 comparisons run M5 and the admission candidate in both
filesystem orders at the unchanged 30-buffer capacity. Every run passes the
strict result, installed-input identity and actual RDB-capacity checks. These
two-pair statistics are exploratory, not full qualification. All phases,
controls, individual ratios and paired PFS3 timings remain in the
[pilot summary](evidence/bfs-scan-admission-performance-2026-10-09/admission-pilot-summary.log).
Its legacy `LIST_EXALL_400` and `LIST_EXNEXT_400` phases scan 40 entries over
ten passes, not a 400-entry directory. The explicit `*_400_ENTRIES_*` phases
and the 400-entry rows below use an actual 400-entry fixture.

| Repeated phase | Median candidate over M5 | Paired range | Slower pairs |
| --- | ---: | ---: | ---: |
| ExAll 40 entries | 1.2236 | 1.1793–1.2679 | 2/2 |
| ExAll 400 entries | 0.7230 | 0.7216–0.7244 | 0/2 |
| ExAll 1000 entries | 1.0819 | 1.0777–1.0862 | 2/2 |
| ExNext 40 entries | 1.0688 | 1.0529–1.0846 | 2/2 |
| ExNext 400 entries | 1.1793 | 0.8476–1.5110 | 1/2 |
| ExNext 1000 entries | 1.2359 | 1.2244–1.2473 | 2/2 |
| Read 8 MiB control | 1.0490 | 1.0327–1.0653 | 2/2 |
| Write 8 MiB control | 1.0158 | 1.0017–1.0300 | 2/2 |

Repeated ExAll 400 remains 12.28–14.26x PFS3 despite the local improvement;
ExAll 1000 remains 17.78–18.74x. The warm small-directory regression and
additional large-directory reads reject the candidate before the remaining
32 qualification runs. Unrelated write/control timings fluctuate; this pilot
does not isolate a causal admission-policy cost for every phase. No runs or
outliers are discarded.

## Read and validation distribution

Four separate ABI16/schema15 comparisons pass strict verification. Every
count in the table repeats exactly in both filesystem orders. Nonresident
views are node-view helper calls without a validated resident result, not
unique block counts. Positive hint peeks bypass that helper and are excluded.
Expected traversal levels, not unchecked node headers, classify leaves and
internal nodes. These counters are not exclusive CPU times.

| Repeated phase and handler | Device reads and CRC computations | Nonresident DIR leaf / internal views | Nonresident INODE leaf / internal views |
| --- | ---: | ---: | ---: |
| ExAll 40 M5 | 0 | 0 / 0 | 0 / 0 |
| ExAll 40 candidate | 5 | 5 / 0 | 0 / 0 |
| ExAll 400 M5 | 692 | 441 / 61 | 190 / 0 |
| ExAll 400 candidate | 440 | 440 / 0 | 0 / 0 |
| ExAll 1000 M5 | 1713 | 1031 / 122 | 560 / 0 |
| ExAll 1000 candidate | 1823 | 1171 / 112 | 540 / 0 |
| ExNext 400 M5 | 680 | 440 / 60 | 180 / 0 |
| ExNext 400 candidate | 810 | 810 / 0 | 0 / 0 |
| ExNext 1000 M5 | 1840 | 1090 / 150 | 600 / 0 |
| ExNext 1000 candidate | 2860 | 2150 / 150 | 560 / 0 |

ExAll 400 saves 252 reads/CRCs by preserving parent and inode residency.
ExNext 400 instead gains 370 directory-leaf misses and saves 240 parent/inode
misses: 130 additional reads/CRCs. At 1,000 entries, ExNext gains 1,060
directory-leaf misses while saving 40 inode-leaf misses; ExAll gains 140
directory-leaf misses while saving 10 parent and 20 inode-leaf misses.
The total increases are 1,020 and 110 reads/CRCs respectively. The repeated
40-entry measurements include five additional qualifying misses before the
small working set becomes resident; ExNext has the same five-read increase.

At 1,000 entries, the admission candidate has only one resident directory-leaf
helper view in repeated ExNext and none in repeated ExAll. The loss of residency
is consistent with FIFO histories churning during long forward scans. It does
not establish the timing share of history lookup or a benefit from a larger
history. The
[deep summary](evidence/bfs-scan-admission-performance-2026-10-09/admission-deep-summary.log)
retains all 12 first/repeated listing phases and every timing control.
Device reads count Amiga DoIO calls, not host-disk transfers or physical-media
qualification. Nested sampled scopes and diagnostic elapsed times are not
production speedup estimates.

## Verification and restoration

The candidate host and corrected ASan/UBSan runs each pass 578 tests in 61
suites, with leak detection. Eleven new admission
tests cover first/qualifying/resident reads, ordinary demand, cursor-boundary
dispatch, scan opt-out, failed reads, dirty pressure, mutation/epoch behavior,
bounded history and directory semantic corruption with a valid CRC. The
allocation-fault suite covers all four required two-slot init allocations
and the optional fifth allocation's leak-free fallback. Existing cursor tests
exercise callback mutation and invalidation with directory admission enabled.
Independent Luna review finds no supported-use correctness blocker.

The first final sanitizer run passed 577 tests because a copied source file's
mtime left an older allocation-fault executable newer than that source. The
suite-by-suite count check detected the stale executable. A forced rebuild
of that target followed by the complete sanitizer rerun passes all 578 tests.
Both the original and corrected logs remain; the stale run is not substituted
for the corrected evidence. Handler identities and timing runs are unaffected.

Candidate and restored quality/static checks pass all 117 quality tests and
their counter-probes. All 12 summary oracles pass. The default summary rejects
the absent full inventory; the pilot and deep selections cannot be mistaken
for full qualification. Seven existing source/test files match their frozen
pre-candidate bytes, and the new admission suite is removed from the live tree.
The recoverable eight-file patch passes an apply check against restored source.
Rebuilt production, probe and guest binaries reproduce their baseline digests
exactly. Fresh restored host and ASan/UBSan runs each pass 566 tests in 60
suites. Earlier dirty changes and frozen evidence are preserved.

## Inputs and evidence

The environment matches the
[preceding cache experiment](bfs-scan-cache-performance-2026-10-09.md): Cachy
KVM/FS-UAE3.2.35, A1200/68040 maximum speed, 2 MiB chip and 8 MiB fast memory,
Kickstart47.102/Workbench3.2 commands, fresh 256 MiB RDBs, 4 KiB blocks and
30 buffers. The six fixtures contain 2880 files; ED_COMMENT is requested but
comments are empty. Setup and flush precede timers. First/repeated passes are
not cold-cache claims. No heavy Cachy build or test overlaps timed runs; one
emulator runs at a time. Host scheduling remains uncontrolled.

| Input | SHA256 |
| --- | --- |
| M5 production | `79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4` |
| Rejected admission production | `3e646ffb66c0b4dc15d3322c723579da88636ea056cb8836456a348a3852f12a` |
| M5 ABI16 probe | `18f5415afa658dd570fbefe9baa2e9e72ba12c3a36d15eeca3570756998dbf43` |
| Rejected admission ABI16 probe | `2ae0f0894f18c2ce8cbec5cc0d88ecbb177b1cb919e6f5874967568945455eb6` |
| Shared guest | `eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d` |

The [text evidence bundle](evidence/bfs-scan-admission-performance-2026-10-09/)
contains all eight runs, configurations, raw timing/counter TSVs, strict
summaries, build/test logs, installed/build input digests, 362 baseline and
363 candidate exported-file identities, the candidate patch, memory constants
and independent review. Source inventories exclude build outputs and evidence.
Installed hashes are recorded after each series, not forensic hashes of embedded
RDB modules. Format progress logs are losslessly encoded with original-byte
digests; timing TSVs remain unchanged. Licensed inputs, executables and HDFs
are excluded. Independent evidence review confirms the raw arithmetic and
identities. The complete asset audit and new checksum manifest pass; all six
earlier evidence manifests remain unchanged and pass verification.

Format v3, integrity, snapshots, recovery, commit/flush policy and frontend
behavior remain unchanged. Native AROS, Linux FUSE, nonempty-comment performance
and physical hardware are separate qualifications. No commit, push, merge or
remote CI run is started. The CI runner remains inactive and no emulator remains.

## Next optimization scope

The bounded cursor-path, demotion and admission experiments do not justify
another generic scan policy. The next candidate should reduce the cost of
fresh leaf access, such as validated I/O batching or leaf-validation CPU work,
with a control that isolates the selected component. Cache capacity and full
validation remain fixed. The current counters identify repeated directory-leaf
misses, but do not predict a speedup or establish that a particular batching
or CRC change is safe and useful.
