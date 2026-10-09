# BFS scan cache performance experiment

Demoting copied directory scan leaves improves the 400-entry repeated ExAll
pilot, but destroys the warm 40-entry working set and increases device reads
in the 1,000-entry listings. The standalone policy is rejected. Its eight
source/test changes are removed exactly; M5 and the new diagnostic counters
remain. The <=5x PFS3 goal stays open. No full normal/durable series is started.

## Shared core policy

An optional BIO advisory hook marks a clean resident slot as a preferred
eviction victim after a directory scan copies the leaf into private memory,
before callbacks. Demand reads and validated-node peeks promote it again.
Slot reuse, writes, dirty modification/write-back and invalidation clear the
flag. Dirty slots remain excluded from replacement; existing explicit discard
and failed-write behavior is unchanged. Advice changes no bytes, CRC or
structural-validation state, mutation epoch, flush fence or on-disk format.

Only directory scan operations opt in. A recorded physical leaf block makes
advice safe before callbacks; cursor-owned copies with no recorded path are
not advised. The consumed-leaf boundary saves its last key before advice.
The policy changes replacement after admission, not the initial admission:
a first leaf miss can still evict the ordinary LRU victim. It adds no block
buffers or cursor path retention. Compiler constants show an m68k cache-slot
increase from 54 to 58 bytes, or 120 bytes at 30 buffers; the cursor stays at
40 bytes and the cache handle at 88 bytes. The B-tree operations table grows
from 20 to 24 bytes.

## Production pilot

Four fresh schema4 comparisons use retained M5 and the demotion candidate,
with both filesystem orders for each handler. All pass strict verification
of results, installed input identities and the actual 30-buffer RDB setting.
These two-pair statistics are exploratory, not eight-run qualification or a
causal estimate of unrelated write phases. All phases and controls remain in
the [strict pilot summary](evidence/bfs-scan-cache-performance-2026-10-09/scan-pilot-summary-final.log).

| Repeated phase | Median candidate over M5 | Paired range | Slower pairs |
| --- | ---: | ---: | ---: |
| ExAll 40 entries | 3.0910 | 2.9368–3.2452 | 2/2 |
| ExAll 400 entries | 0.7934 | 0.7509–0.8359 | 0/2 |
| ExAll 1000 entries | 0.9742 | 0.9039–1.0445 | 1/2 |
| ExNext 400 entries | 0.9335 | 0.7920–1.0750 | 1/2 |
| ExNext 1000 entries | 1.1823 | 1.1316–1.2329 | 2/2 |
| Read 8 MiB control | 1.0784 | 0.9441–1.2127 | 1/2 |

Candidate repeated ExAll BFS/PFS3 ratios exceed five in both runs at all
three sizes: 10.60–11.89 for 40, 14.90–14.94 for 400 and 18.05–18.73 for
1,000 entries. Repeated ExNext 1,000 also exceeds five in both runs,
5.55–6.37. The smaller directory regression and additional large-directory
reads outweigh the localized 400-entry benefit.

## Diagnostic read distribution

Four separate ABI16/schema15 comparisons pass strict verification. Each
count below repeats exactly in both filesystem orders. Node-view counts are
partitioned by the traversal's expected level, not by unchecked stored node
bytes. A nonresident view is a helper invocation without a validated resident
view. Positive hint peeks bypass that helper and are not included: zero inode
node views does not mean zero inode accesses. These are counts, not CPU times.

| Repeated phase and handler | Device reads and CRC computations | Nonresident DIR leaf / internal views | Nonresident INODE leaf / internal views |
| --- | ---: | ---: | ---: |
| ExAll 40 M5 | 0 | 0 / 0 | 0 / 0 |
| ExAll 40 candidate | 60 | 60 / 0 | 0 / 0 |
| ExAll 400 M5 | 692 | 441 / 61 | 190 / 0 |
| ExAll 400 candidate | 460 | 460 / 0 | 0 / 0 |
| ExAll 1000 M5 | 1713 | 1031 / 122 | 560 / 0 |
| ExAll 1000 candidate | 1774 | 1141 / 112 | 521 / 0 |
| ExNext 1000 M5 | 1840 | 1090 / 150 | 600 / 0 |
| ExNext 1000 candidate | 2230 | 1520 / 150 | 560 / 0 |

At 400 entries, demotion preserves inode and parent residency, saving 232
reads/CRCs despite 19 additional directory-leaf misses. At 1,000 entries,
ExAll trades 39 fewer inode-leaf misses and 10 fewer directory-internal misses
for 110 extra directory-leaf misses: 61 extra reads/CRCs. ExNext trades 40
fewer inode-leaf misses for 430 extra directory-leaf misses: 390 extra
reads/CRCs. Warm ExAll 40 loses all 60 leaf-resident helper views and gains
60 reads/CRCs. This matches the production regression signal.

Device reads count Amiga DoIO read operations, not host-disk operations or
qualification of physical media. Diagnostic elapsed times and nested sampled
scopes are not production times, exclusive CPU totals or speedup forecasts.
The [deep summary](evidence/bfs-scan-cache-performance-2026-10-09/scan-deep-summary.log)
retains raw counts for every listing phase and timings for all controls.

## Verification and retained instrumentation

Candidate host and ASan/UBSan runs pass 573 tests across 61 suites each, with
leak detection. Five new cache tests cover preferential clean eviction,
validation/epoch preservation, dirty pressure and failed write-back, demand
promotion and slot lifecycle, and unsupported/out-of-range advice. Two
directory/cursor tests cover ordered complete enumeration, opt-in behavior,
copy-before-advice isolation, callback mutation/early stop, and exclusion of
cursor-only copies. Independent Luna review finds no supported-use defect.

All 117 quality tests, quality-gate counter-probes, core/host static analysis
and 11 summary oracles pass. Integration corrected an undefined fixture
constant before the oracle suite passed. The default 40-run summary correctly
rejects the missing 32 qualification runs; pilot/deep results cannot be
mistaken for full qualification. The recoverable eight-file candidate patch
passes an apply check against restored source. The seven existing files
match the frozen pre-policy source byte-for-byte; the new candidate-only
test file is removed.

Fresh restored host and ASan/UBSan suites pass 566 tests across 60 suites
each. Production, ABI16 probe and guest rebuilds reproduce their respective
baseline digests exactly. The additional retained host test covers expected-
level accounting, reset and disabled/OTHER classification; the existing
real-filesystem test checks the level partitions along actual core paths.

The retained diagnostic extension appends eight directory/inode leaf/internal
view counters to probe ABI16 and deep schema15. Existing schemas4–14 remain
supported; normal and durable schema4 workloads are unchanged. Both handler
variants use the same newly compiled guest. Its changed identity prevents
treating historical guest timings as the new baseline. Format v3,
integrity, snapshots, recovery, commit/flush policy and frontend behavior
are unchanged. Native AROS, Linux FUSE, nonempty-comment performance and
physical hardware are separate qualifications.

## Inputs and evidence

The environment matches the [M5 comparison](bfs-leaf-range-performance-2026-10-08.md):
Cachy KVM/FS-UAE, A1200/68040 maximum speed, 2 MiB chip and 8 MiB fast memory,
Kickstart47.102/Workbench3.2 commands, fresh 256 MiB RDBs, 4 KiB blocks
and 30 buffers. The six fixtures contain 2880 files; ED_COMMENT is requested
but comments are empty. Setup and flush precede timers; first/repeated passes
are not cold-cache claims. Heavy tests and builds finish before timing; one
emulator runs at a time, and host scheduling is uncontrolled.

| Input | SHA256 |
| --- | --- |
| Retained M5 production | `79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4` |
| Rejected demotion production | `889dd91850212a555260106ca9c336b9207b98860bf6926ffc2033f6435cbf52` |
| Retained M5 ABI16 probe | `18f5415afa658dd570fbefe9baa2e9e72ba12c3a36d15eeca3570756998dbf43` |
| Rejected demotion ABI16 probe | `11e3a517645ab7c8e28fc06cdce781bba536dccf2d2d385bef8e6e2266d10417` |
| Shared guest | `eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d` |

The [text evidence bundle](evidence/bfs-scan-cache-performance-2026-10-09/)
retains all eight runs, strict summaries, configurations, installed and build
input identities, 216 baseline/217 candidate source identities, the candidate
patch, memory constants, reviews and build/test logs. Installed digests are
recorded after each series, not forensic hashes of embedded RDB modules.
Format progress logs are losslessly encoded with original-byte digests;
timing TSVs are untouched. The complete asset audit and checksum manifest
pass. Licensed inputs, executables and HDFs are excluded.
Earlier evidence manifests remain unchanged. No commit, push, merge or remote
CI run is started; the CI runner remains inactive.

## Next bounded hypothesis

Replacement-only demotion is insufficient. Investigate admission that
distinguishes a reused small-directory working set from a streaming scan,
without permanently deprioritizing every directory leaf. The remaining
1,000-entry misses are predominantly directory leaves; all observed inode-
internal helper views in the repeated phases above are resident. Any next
candidate must protect the
warm 40-entry case and reduce total reads/CRCs at both larger sizes before a
full production/durable qualification is justified. Combining policies or
adding read-ahead requires a separate bounded design and measurement, not
an assumed benefit from the rejected demotion rule.
