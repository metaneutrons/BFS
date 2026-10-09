# BFS cursor path performance experiment

Guarded cursor paths reduce directory traversal work but do not improve the
measured cache locality. Both diagnostic repetitions increase device reads
and CRC computations in the large repeated listings. Small repeated ExAll
also regresses in the production pilot. The standalone candidate is rejected
before full qualification. Only its three source/test changes are removed;
the previous M5 implementation remains intact. The <=5x PFS3 goal stays open.

## Shared core candidate

The B-tree cursor saves parent block locations and child indices, not node
images or borrowed BIO pointers. An unchanged available mutation epoch and
matching tree, backend, layout and geometry allow the existing validated
next-leaf traversal at a stopped leaf boundary, instead of another root
descent. Parent and leaf CRC, structure, level and bounds checks remain.
Missing guards and callback mutations retain the descent fallback. Inclusive
overflow retry and exclusive consumed-entry continuation remain distinct.

The existing mount/BIO lifetime contract still applies. Rebinding a live tree
or changing its geometry without releasing cursors is unsupported; the path
guard is not a new leaf-copy rebinding API. Format v3, validation, snapshots,
recovery, commit/flush policy, adapters and the 30-buffer capacity are unchanged.
Compiler constants show cursor growth from 40 to 324 bytes on m68k and from
48 to 336 bytes on the host. The fixed path occupies 260 bytes. Path retention
adds no block buffers or heap allocations.

## Production pilot

Four fresh schema4 runs compare retained M5 with the path candidate using
two matched filesystem orders per handler. All pass strict verification.
These are exploratory two-pair statistics, not eight-run normal/durable
qualification or a causal estimate of unrelated write phases.

| Repeated phase | Median path over M5 | Paired range | Slower pairs |
| --- | ---: | ---: | ---: |
| ExAll 40 entries | 1.0401 | 1.0132–1.0671 | 2/2 |
| ExAll 400 entries | 0.9288 | 0.9075–0.9501 | 0/2 |
| ExAll 1000 entries | 0.9743 | 0.9062–1.0424 | 1/2 |
| ExNext 400 entries | 0.9710 | 0.9574–0.9846 | 0/2 |
| ExNext 1000 entries | 0.9536 | 0.8719–1.0353 | 1/2 |
| Read 8 MiB control | 1.0296 | 1.0168–1.0424 | 2/2 |

Every phase, including writes and controls, remains in the summary; no failed
or slow run is filtered. Candidate repeated ExAll median BFS/PFS3 timing ratios are
16.77/16.66 at 400/1000 entries; both runs exceed five in both phases. Repeated
ExNext median ratios are 4.96/5.60, with both 1000-entry runs exceeding five.

## Diagnostic mechanism

Four separate schema14 comparisons use both filesystem orders for each
identified ABI15 probe. Each following count repeats exactly in both orders:

| Repeated phase and counter | Retained M5 | Path candidate |
| --- | ---: | ---: |
| ExAll 400 device reads and CRC computations | 692 | 712 |
| ExAll 400 directory views / resident views | 1184 / 682 | 1064 / 542 |
| ExAll 1000 device reads and CRC computations | 1713 | 1733 |
| ExAll 1000 directory views / resident views | 2794 / 1641 | 2554 / 1381 |
| ExAll 1000 inode views / resident views | 1120 / 560 | 1120 / 560 |
| ExNext 1000 device reads and CRC computations | 1840 | 1860 |
| ExNext 1000 directory views / resident views | 6684 / 5444 | 2444 / 1174 |
| ExNext 1000 inode views / resident views | 1200 / 600 | 1180 / 590 |

ExAll 1000 removes 240 directory-helper views but adds 20 nonresident directory
accesses; inode misses remain 560. ExNext removes 4240 directory views while
directory misses rise from 1240 to 1270 and inode misses fall from 600 to 590.
The resulting read/CRC total increases by 20. Small repeated ExAll still has
zero device reads and 160 resident directory views in both candidates.
Device reads here count Amiga DoIO read operations, not host-disk operations
or qualification of physical media.

Shorter traversal does not necessarily improve global LRU residency. These
counters do not identify the missed node levels or prove the right cache
policy. Diagnostic elapsed and nested sampled scopes are not production
times, exclusive CPU totals or expected-speedup estimates. The modest pilot
benefit, regression signal and extra misses do not justify retaining this
standalone candidate. No full 32-run normal/durable series is started.

## Correctness and restoration

Candidate host and ASan/UBSan suites pass 568 tests across 60 suites each,
including leak detection. Eight focused directory tests cover a 30-buffer
retained-path versus forced-fallback oracle, inclusive leaf-end retry,
exclusive continuation, full ordered enumeration, malformed saved-path guards,
missing/saturated epochs, raw corruption and callback mutation. The oracle
counts BIO node-access and comparison calls, not device reads or elapsed
performance. Its cursors run in order on one cache; separate diagnostics
govern physical-read claims.

All 113 quality tests, quality-gate counter-probes, core/host static analysis,
five summary rejection oracles and production/probe m68k builds pass.
Independent Luna review finds no supported-use correctness defect. Integration
corrected the initial pilot/probe digest mapping and added an independent
identity oracle before verifying real runs. The default 40-run inventory
correctly rejects full qualification with only eight exploratory runs present.

Restored source identities exactly match M5. Fresh restored host/sanitizer
runs pass 565 tests across 60 suites each; production and probe rebuilds
reproduce their previous binary digests. There is no retained production
change, commit, push, merge or remote CI run. Native AROS, Linux FUSE, physical
hardware and nonempty-comment performance are separate qualifications.

## Inputs and evidence

The environment matches the [M5 comparison](bfs-leaf-range-performance-2026-10-08.md):
Cachy KVM/FS-UAE, A1200/68040 maximum speed, 2 MiB chip and 8 MiB fast memory,
Kickstart 47.102/Workbench 3.2 commands, fresh 256 MiB RDBs, 4 KiB blocks
and 30 buffers. The six fixtures contain 2880 files; ED_COMMENT is requested
but comments are empty. Setup/flush precede listing timers; first/repeated
passes are not cold-cache claims. Heavy tests and other emulators finish
before timing; host scheduling is uncontrolled. The CI runner stays inactive.

| Handler | SHA256 |
| --- | --- |
| Retained M5 production | `79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4` |
| Rejected path production | `60c3203d11221aff46fc6e536578a4c644ab37c0106aa28cd44e01db06e0984e` |
| Retained M5 probe | `a46a153a387de6cac773aee8bc6b757ce0bccf7a6dbc03b2c6ec8f8f8cc7981c` |
| Rejected path probe | `f09d043afda2e4fcb33e827984ae355224245c28ad18e819b26dc4d4583aaadc` |

The [text evidence bundle](evidence/bfs-cursor-path-performance-2026-10-09/)
retains all eight runs, strict summaries, configurations, input and 83 source
identities, the recoverable three-file patch, memory constants, review and
build/test logs, and a checksum manifest. Installed input digests are recorded
after each series, not forensic hashes of embedded RDB modules. Format logs
are losslessly encoded with original-byte digests; timing TSVs are untouched.
An initial text-log copy also included 60 runtime libraries; the asset audit
detected them, and they were moved to the ignored build area before any Git
publication. The final asset audit passes. Licensed inputs, executables and
HDFs are excluded; earlier evidence manifests remain unchanged.

## Next bounded cache experiment

Evaluate scan-resistant admission/promotion at the same 30-buffer capacity,
distinguishing directory/inode and leaf/internal misses. Existing leaf copies
allow finishing the sequence without retaining the consumed block. An advisory
policy could prioritize reused inode and parent nodes over consumed scan
leaves, without changing bytes, validation or dirty-node eviction rules.
This is a hypothesis: require fewer device reads/CRCs, matched production
benefit and the existing correctness qualification before retaining it.
