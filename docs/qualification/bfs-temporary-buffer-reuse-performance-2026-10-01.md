# BFS temporary buffer reuse qualification

The shared BIO buffer recycler is retained after functional qualification and
eight fresh normal-handler comparisons. The overall performance goal remains
unachieved: create, write and delete exceed five times PFS3 in every candidate
run. No durability, integrity, snapshot or crash-recovery requirement was
relaxed, and no CI or external publication was performed.

## Normal measurement decision

Four baseline and four candidate images used the same normal guest benchmark,
formatter, PFS3 handler, ROM, options=0, 4 KiB BFS blocks, 255.5 MiB partition,
Workbench assets and isolated Cachy FS-UAE 3.2.35 A1200/68040 configuration with
8 MiB fast RAM. Each version ran twice in each filesystem order. All sixteen
filesystem outputs passed the unchanged strict schema-1 data/completion
verifier. No probe timings enter this decision.

The first baseline ran while the final functional gates were being completed,
about ten minutes before the candidate series. The remaining seven runs were
serial, in the chronological order recorded in the
[measurement summary](evidence/bfs-temporary-buffer-reuse-2026-10-01/measurement-summary.md).
No remote compiler or other FS-UAE guest ran concurrently. This is a small
descriptive experiment, not a causal estimate or statistical precision claim.
All samples and adverse comparisons are retained.

Times are four-run means in microseconds. Ratios divide candidate BFS and
candidate PFS3 means; the last column counts same-run threshold failures.

| Workload | Baseline BFS | Candidate BFS | Change | BFS / PFS3 | Candidate runs above 5 times |
|---|---:|---:|---:|---:|---:|
| Create 40 | 302301.75 | 268484.75 | −11.187% | 16.517 | 4/4 |
| Lookup 400 | 125296.00 | 107759.50 | −13.996% | 1.578 | 0/4 |
| Small read 40 | 61920.25 | 46927.00 | −24.214% | 3.348 | 0/4 |
| Write 8 MiB | 481421.25 | 434109.50 | −9.828% | 11.711 | 4/4 |
| Read 8 MiB | 575437.00 | 513565.25 | −10.752% | 1.772 | 0/4 |
| Delete 40 | 236926.75 | 198222.75 | −16.336% | 24.691 | 4/4 |

Five workloads improve in all four order-and-repeat-matched BFS comparisons.
Create improves in three; repeat PFS3-first create is 15055 µs slower than its
matched baseline. Candidate create spans 235467–309836 µs, write
398655–486524 µs and delete 182271–229824 µs. The slow PFS3 baseline write
sample of 77695 µs and candidate PFS3 read sample of 364413 µs remain in the
raw table. Neither is replaced or trimmed.

The observed improvements justify retaining this bounded shared change, but
do not meet the target. Insert/delete still request an extra path-buffer block
outside this experiment; removing a demonstrably unused slot is a separate
increment requiring its own qualification and measurements.

## Shared implementation and unchanged guarantees

BIO backends may supply paired temporary-buffer allocation/release hooks.
Incomplete pairs use ordinary malloc/free; allocation failure is not hidden
by a second attempt. The cache lazily retains at most four independently
leased one-block buffers, separate from resident cache bytes. Requests of
other sizes or with all four slots occupied use the heap. Exact-pointer
release returns a lease; NULL release is a no-op. Invalidation leaves active
leases untouched, and destroy frees retained buffers after all operations
and leases have ended. Live-cache geometry and ownership remain fixed until
destroy/reinitialization, as required by resident cache storage.

B-tree node buffers and path allocations use the shared helpers. Scan
continuation keys and census hash storage retain ordinary heap ownership.
The filesystem logic is shared by Amiga and Linux, without an Amiga-only
allocator or global arena. Retained payload is bounded to 16 KiB at the
measured geometry and 256 KiB at the maximum supported 64 KiB geometry.

No node read, copy, validation, CRC, COW allocation, write, publication fence
or flush is removed. Tree ownership, allocator policy, the on-disk format and
workloads are unchanged. Probe ABI 11 and deep schema 10 are unchanged.
Direct B-tree heap counters no longer include allocation inside BIO/cache
helpers; a lower direct-call count must not be mistaken for disappearance of
all backend heap cost. Adoption uses normal elapsed time, not those counters.

## Functional qualification

The final source passed 45 host suites / 440 tests in both normal and
ASan/UBSan builds, 70 local quality tests, ShellCheck, static analysis, and
normal/probe m68k builds. LeakSanitizer is unavailable in this macOS setup;
`ASAN_OPTIONS=detect_leaks=0` was explicit. Linux qualification on Cachy passed
20 conformance tests and actual FUSE mounts. No CI workflow was run.

Fifteen added tests cover optional-hook fallback, incomplete pairs, four
simultaneous distinct leases, overflow/non-block allocation, per-slot lazy
allocation failures, initialization cleanup, invalidation, cache-byte
isolation and destroy/reinit. The NOMEM root-replacement fixture checks the
root and all raw device bytes remain unchanged when the second buffer fails.
Natural mounted Free-Tree recursion at 1 KiB and 4 KiB witnesses multiple
distinct live leases; split/merge write failures and recursive child-read
failure check unwind, retry, height transitions and idle leases. Mounted
create/write/read/sync/delete cycles retain strict data/fsck checks and
unmount before cache destruction. Independent source/test review found no
remaining blocker after explicit alias assertions were added.

Existing buffered-persistence, strict graph/data/fsck, snapshot, recovery,
low-space and small pending-capacity oracles remained enabled. Real power
cuts, controller cache/flush lies and particular hardware media remain
separate device-bound qualification, not claims of this experiment.

Initial Linux setup attempts used an incorrect fixture target and omitted the
repository CI YAML read by one conformance test. Both were corrected without
production changes or weaker assertions; their logs are retained beside the
passing final log. A superseded sanitizer log had overlapping output while
the test-only assertions were finalized. It is excluded from qualification;
the isolated final `buffer-reuse-final-sanitize-v2.log` is complete and passes.

## Input identities and evidence

The source base is `86e9d80`. The baseline normal handler is
`f3b593adb3f377d4ce755f4d08c6e378fb7bb4d0d41fdad2d86a900280ab783c`;
candidate normal is
`6ecdf122885904491dcf8ac2925d1218aa5e6a77344d88f80cf4940695ef62af`,
133256 bytes, 1004 bytes larger. A separate clean-directory m68k rebuild
reproduced that candidate hash. Candidate probe is
`0f0f92cb4cb965448a913dad0d5cad354febe58948d2cbde0f224f3a5deee34f`.
The common guest tool is
`1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80`.

The [evidence directory](evidence/bfs-temporary-buffer-reuse-2026-10-01/)
contains raw timings, data/completion markers, machine information, emulator
configuration/logs, image-builder/runner logs, functional logs, source/input
identities and a SHA-256 manifest. It contains no licensed ROM, Workbench
assets, handler binaries or disk images. Only trailing horizontal whitespace
in copied FS-UAE logs was normalized; timing TSVs are unchanged. This checkpoint is local; no push,
PR, merge or release was performed.
