# Metadata and listing performance — 2026-10-08

## Result and scope

This is local staged work on `fix/metadata-listing-performance`, starting from
main `07216b7f17912c9f28b9b4a920caaa6828b00ccb`. Nothing is merged or published;
remote CI remains stopped. The [versioned plan](../plans/bfs-metadata-listing-performance-v1.md)
defines acceptance. The <=5x PFS3 target remains open.

The original operation-local inode-reuse prototype removes a duplicate search,
but its eight-run compare and durable series do **not** establish an elapsed
performance benefit. It is revised to avoid additional inode structure copies;
the revised implementation now has an eight-run normal comparison. Its create
phase is promising, but durable confirmation and final retention are pending.

The new 24-run real-directory comparison exposes a larger baseline limitation:
repeated ExAll listings of actual 400/1,000-entry directories take median
19.45x/20.01x PFS3 in the baseline. All eight runs of both phases exceed five.
They already occur in the original main handler, so they are not attributable
to the new cache. Larger-listing attribution takes priority over tail-cache
optimization. No hardware or device-cold claim is made.

## Implementations and mechanisms

- M1 reuses the freshly validated inode inside one locked write/append only,
  guarded by tree/root/generation, inode/link identity and recovery lifetime.
  The original variant copied the inode into and out of a seed; the revised
  variant reads directly into that seed and updates it in place. Intermediate
  publication, provider timing and recovery are unchanged. The focused test
  sees four key comparisons instead of six; the same test against pinned main
  fails only that mechanism assertion, with its three semantic tests passing.
- M2 has 64 filesystem-owned inode-to-leaf/index hints, never copied inode
  values. A hit needs the current root/generation and BIO mutation epoch, a
  fresh validated resident leaf, a valid index and the current key. Ordinary
  inode validation still runs. The pre-existing last-leaf shortcut also gains
  the epoch guard, so a raw internal-node write cannot hide behind its fallback.
  Missing or saturated epochs disable both search shortcuts. Cache mutation
  attempts, failed writes, deferred images and invalidation advance the epoch.
  The cache/trees must not survive backend destroy/reinitialization.
  The fixed hint table is 1,792 bytes on m68k (28-byte slots), 2,048 bytes on
  the macOS host ABI. Compiler-emitted size constants are retained as evidence.
- The M2 positive oracle clears the old last-leaf hint first, then proves that
  the indexed path needs no traversal buffer. Disabling only the new optional
  key-cache pointer makes this assertion fail (one allocation instead of zero);
  the other seven tests pass. This proves skipped work, not a time saving.
- Schema 4 retains the legacy workload and appends exact-size directory
  fixtures, with a first pass after setup and ten additional same-mount passes.
  Every pass checks names, regular-file types, counts and uniqueness. ExNext
  and ExAll have distinct directories. Six fixtures contain 2,880 empty files
  in total, so the common trees have that global working set even when listing
  the 40-entry fixture. Setup and its flush are outside the timed phases.

The legacy `LIST_*_400_US` rows are ten passes over only 40 files. They must
not be presented as measurements of a 400-file directory. First pass after
setup is not cache-cold: creation, preceding operations and host/device caches
can warm metadata. A separate remount/measurement boot path is still needed
before claiming handler-cache-cold timing.

## Original M1 experiment

Eight fresh runs per handler and mode, interleaved with the pinned baseline;
odd runs start BFS first, even runs PFS3 first. All 32 expected run directories
passed strict guest/schema/data verification. No runs are filtered out.
Adjacent same-order candidate/baseline pairs are not simultaneous, and cannot
remove scheduling noise on the uncontrolled KVM host.

Median of adjacent-run BFS candidate time divided by baseline BFS time:

| Workload | Compare | Durable compare |
| --- | ---: | ---: |
| Create 40 | 0.954 | 0.986 |
| ExNext, ten passes over 40 | 1.008 | 0.958 |
| ExAll, ten passes over 40 | 1.018 | 0.996 |
| Write 8 MiB | 1.014 | 0.978 |
| Append in 4 KiB steps | 1.003 | 1.017 |
| Append in 1 KiB steps | 1.072 | 0.993 |
| Read 8 MiB (control) | 1.088 | 1.042 |

The larger changes in an unaffected read workload, different signs between
modes, and broad run ranges do not support a causal speedup claim. The original
candidate also has ExNext 5.19x and ExAll 5.97x outliers in compare, and an
ExAll 5.10x outlier in durable compare. They remain in the raw evidence.

## Schema 4 normal comparison

Eight fresh runs each of pinned main, revised M1 (`m1b`) and M1+M2 (`m2`),
interleaved with alternating filesystem order. All 24 expected directories
pass strict verification; the retained summary checks their exact inventory.
No failure or outlier is excluded. Selected median per-run BFS/PFS3 factors:

| Actual directory / API | Main | M1 revised | M1 + M2 |
| --- | ---: | ---: | ---: |
| 40 / ExAll first after setup | 6.44 | 6.22 | 6.29 |
| 40 / ExAll ten additional passes | 4.71 | 4.78 | 3.54 |
| 400 / ExNext ten additional passes | 5.68 | 5.63 | 5.49 |
| 400 / ExAll ten additional passes | 19.45 | 18.63 | 19.18 |
| 1,000 / ExNext ten additional passes | 5.91 | 5.60 | 5.82 |
| 1,000 / ExAll ten additional passes | 20.01 | 18.67 | 20.54 |

These ratio differences are not automatically speedups: PFS3 and host timing
also vary. The adjacent BFS M2/main time ratio for 40-entry repeated ExAll has
median 0.759 (range 0.711–0.886); all eight pairs improve. The comparable
1,000-entry repeated ExAll ratio is 1.031 (0.894–1.106), with no established
benefit. The 400-entry first-pass ratio is 1.154 (0.678–1.745), a possible
regression requiring further attribution. M1 revised create has adjacent ratio
0.916 (0.829–1.024), but append results and an unaffected read control are
noisier; no universal M1 speedup is claimed. Durable confirmation is pending.

The large-listing shortfall is not solved. Every run of the 400/1,000-entry
repeated ExAll rows exceeds five for all three variants. This evidence
supersedes pilot-only statements, not the retained pilots themselves.

## M3 normal comparison

The separate M3 series has eight runs each of M2 (`base`, the f5b5 handler)
and M1+M2+M3 (`m3`, the adef handler), using the original identified schema4
guest, not the newly instrumented guest. All 16 expected runs pass strict
verification, without filtering. Adjacent BFS candidate/M2 time ratios are:

| ExNext workload | Median | Range |
| --- | ---: | ---: |
| Legacy ten passes over 40 | 0.955 | 0.828–0.970 |
| Actual 40, ten additional passes | 0.954 | 0.916–0.974 |
| Actual 400, ten additional passes | 1.048 | 0.834–1.236 |
| Actual 1,000, ten additional passes | 1.029 | 0.901–1.079 |

All eight small-directory pairs improve, by about 4.5–4.6% at the median.
Larger cases do not establish a gain; the positive median time ratios require
caution about overhead. M3's repeated large ExAll medians remain 19.73x/19.55x
PFS3, with all eight runs above five. Different PFS3 timings explain part of
the apparent ratio changes; they are not a causal ExAll speedup. Durable and
handler-cache-cold qualification remain open, so retention is provisional.

## Diagnostic findings and next decisions

Two fresh M3 probe runs use opposite filesystem order. Both guests report
PASS and both retained outputs pass the strict schema12 verifier. Initial
verification failed at Linux's single-argument length limit for the expanded
metric inventory; reading the inventory through stdin fixes that harness
limitation. The failing run logs are retained, and neither guest was rerun or
filtered out. A regression oracle keeps its synthetic inventory above 128 KiB.

The exact large-repeat counts are reproducible in both runs:

| Ten additional ExAll passes | 400 entries | 1,000 entries |
| --- | ---: | ---: |
| Inode reads | 4,040 | 10,100 |
| Indexed hint hits | 40 | 100 |
| Legacy leaf hint hits | 1,060 | 2,390 |
| Inode node views | 5,880 | 15,220 |
| Resident inode views | 5,689 | 14,660 |
| Directory node views | 1,184 | 2,794 |
| Resident directory views | 682 | 1,641 |
| Raw reads / CRC read computations | 693 | 1,713 |

The indexed hits match the additional overflow inode reads; from the source
and counts, they are consistent with overflow retries rather than useful
cross-pass coverage of all distinct entries. The same 64-slot table hits all
400 inode reads in ten repeats of the 40-entry ExAll fixture. It therefore
helps a small working set but is not a large-directory solution.

For 1,000-entry repeated ExAll, raw-read intervals are 162.3/163.1 ms within
instrumented phases of 1,089.3/1,098.7 ms. Device transfer time alone cannot
explain the full gap. CRC timing samples cover only 26 of 1,713 computations
(5.59/5.79 ms sampled); they must not be presented as the total CRC time.
Resident-node traversal, search/validation work and adapter processing need
finer CPU attribution. Inclusive packet, BIO and CRC intervals overlap: do
not add them or subtract them into an allegedly exclusive residual. Logical
cache reads exclude validated in-place node peeks, so their zero hit count in
these phases is not a zero overall metadata-cache hit rate.

For the 256-KiB append in 1-KiB steps, each profile has 192 logical cache reads,
128 hits and 64 misses/raw data reads. The first partial write initializes a
new block without reading; the next one admits it, and subsequent writes hit
the resident block. An extra general tail cache is rejected as unsupported by
these data. A bounded first-write admission experiment could remove those 64
reads, but must account for copy cost, metadata eviction, failures and unchanged
visibility/checksum/snapshot behavior. It is lower priority than large listings.

Next bounded experiments: separate inode-search and node-validation CPU costs;
then compare bounded ExAll inode-order batching (original output order retained)
and metadata-cache sensitivity. No copied persistent inode-value cache, relaxed
CRC, changed commit policy or format migration is accepted. These are candidate
designs, not implemented or measured speedups. Create/delete are already below
five in these normal series and remain secondary.

## Verification so far

M1 revised: full host and ASan/UBSan suites pass; production and probe m68k
builds pass; static analysis returns success without diagnostics. An initial
analyzer warning about a nullable seed was fixed with an explicit guard,
then the pointer-based revision was independently reviewed without findings.

M2: 58 host suites / 553 checks and the same ASan/UBSan checks pass, including
eight new lookup-hint tests. They cover warm hits, invalid index, missing and
saturated epoch, same-root and COW writes, collisions, unlinked/deleted inodes,
CRC-valid invalid inode and CRC corruption after invalidation, raw internal-root
rerouting with both old leaf hints warm, reload/remount lifetime and snapshots.
Independent core review finds no correctness issue within the documented
backend lifetime. Static analysis succeeds without diagnostics. Conformance
passes all 21 checks after using its required default `build/host` path; the
first relocated-build attempt failed because its Python tests hard-code that
path, not because of a filesystem result. Both logs are retained.

Schema 4: all 51 verifier tests pass and the m68k guest builds. Both real pilots
and the full 24-run normal series pass strict verification.

M3: the pinned exclusive-resume implementation passes 59 host suites / 558
checks, including five focused multileaf, stale/missing/parent, callback-mutation
and epoch/invalidation tests. The mechanism test compares keyed and keyless
continuation directly and validates all 420 entries exactly once. Static
analysis succeeds without diagnostics; the production m68k handler builds.
ASan/UBSan passes the same 558 checks; conformance passes 21 checks. Independent
M3 review finds no actionable code issue and identified mixed-API handler
coverage as a remaining gap. The integration guest now preserves its packed
4-KiB ExAll test and also alternates small overflowing ExAll batches with
ExNext on one lock, checking exact sequence and terminal behavior without an
entries-per-buffer ABI assumption. The final integration guest passes all 52
Amiga tests in a disposable Cachy FS-UAE run, including `exallpack_50`; its
completion marker and exact 52/52/0 summary are separately checked. The normal
eight-run paired performance series is complete, as described above.

Deep schema12 / probe ABI13 shares the same append/listing workload functions,
adds logical inode/search/cache/node-view counters, and keeps normal schema4
unchanged. Deferred and direct CRC write computations now use one sampled
diagnostic wrapper; earlier probe versions counted deferred CRCs without
sampling them. The production handler with this diagnostic source is byte-
identical to pinned M3. All 54 verifier tests pass, including accounting and
read-only constraints. Profile elapsed time is instrumented and must not be
substituted for production comparisons.

Final current-tree quality checks pass 98 tests, ShellCheck, static analysis
and the repository asset audit. A docs-excluding snapshot could not run the
quality fixtures; that infrastructure-only failure log is retained alongside
the successful complete-workspace run. Native AROS and Linux FUSE execution,
fresh durable candidate series and handler-cache-cold timing are not claimed.

## Identities and retained evidence

| Input | SHA-256 |
| --- | --- |
| Pinned main handler | `d1304d94315767b057b7417142b3ed145f4f125c0272b8452765be31c8f5e8de` |
| Original M1 handler, measured schema 3 | `73f66aa8862a053146561dc29195268ee41cbf1680df30166d7f1734088bd52f` |
| Revised M1 handler | `3ab7b319cda849bb3e8e934b8489550d84f9f862e60167640d16d09e6678b4c4` |
| M2 handler, including revised M1 | `f5b53075684f11b8d904641f9ac45f1aab89745444b1e8f65045385f5463ef28` |
| M3 production handler, including M1+M2 | `adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27` |
| M3 diagnostic handler, probe ABI13 | `814901c22522e341072c131fc9538fbc63360f46d52d77706f2e09238c8aeabb` |
| Deep schema12 guest | `7671a81675c83b25d21789912412ba25833f3a831307e4f50f56ca6f41cb9ecd` |
| Final M3 Amiga integration guest | `42b39509c18bd65c8011fcf5ff0d489054c571c9180a1f5c2878808d9e2c8749` |
| Schema 3 guest | `3946157638aa66b4bf4907d5daa0f15b01103e7455b9777194c78fe316e1480f` |
| Schema 4 guest | `798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52` |
| Cachy pinned-main formatter | `f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55` |
| PFS3 handler | `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7` |
| Kickstart 47.102 | `68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c` |

Environment: Cachy KVM guest, FS-UAE 3.2.35/Xvfb, A1200/68040 at maximum
speed, 2 MiB chip and 8 MiB fast memory, Workbench 3.2 commands, fresh 256 MiB
RDB images and 4 KiB BFS blocks, unchanged mount/commit policy. No competing
emulator was running at setup; the GitHub runner container stays stopped.
The KVM host scheduling is not controlled.

The [evidence directory](evidence/bfs-metadata-listing-2026-10-08/) preserves
TSVs, completion/phase markers, run logs, source patches/identities, verification
logs and both mechanism counter-probes. `summarize.py` recomputes original M1,
revised M1/M2 schema4 and M3 runs and checks the exact expected inventory rather than
discarding missing/failed runs. `profile-summary.py` reproduces the diagnostic
counts and separate inclusive times. `SHA256SUMS` covers the retained text
evidence. Amiga Format progress logs contain legacy control bytes: each
`format-pfs3.txt` uses lossless base64 text plus its original byte digest,
verified by `encode-format-logs.py`; timing/test TSVs are untouched.
All retained files receive an explicit asset audit, including the text emulator
configurations exempted from the runtime-config ignore rule.
`final-current-source.patch` and `final-current-source-identities.txt` identify
the integrated source, including the final verifier fix and new focused tests;
earlier source identities remain tied to their respective build snapshots.
Licensed ROM, Workbench and PFS3 files, images and binaries are not
retained in Git.
