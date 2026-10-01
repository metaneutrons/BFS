# BFS sealed preflight geometry qualification

The geometry-hoist experiment is rejected as the default. Write and delete
are slower in all four same-order comparisons, with approximately 10 percent
higher elapsed means. This small uncontrolled sample does not establish a
general regression, but provides insufficient performance evidence for
adoption. Production source is restored exactly to `8b35eec`; the stronger
backup-width tests are retained. The overall goal of at most five times PFS3
elapsed time in every checked workload remains unachieved.

## Fresh normal comparison

All eight fresh-image runs and sixteen filesystem outputs passed the unchanged
strict completion and data verifier. Four runs per version balance both
filesystem orders. They ran sequentially after functional qualification and
independent review, with no competing guest emulator or compiler. Cachy
FS-UAE 3.2.35, A1200/68040, ROM, Workbench assets, PFS3 handler, formatter,
normal guest tool, options=0, 4 KiB BFS blocks and 255.5 MiB partitions are
unchanged. Physical-host scheduling and other VM workloads are uncontrolled.

Times are four-run means in microseconds; the ratio divides candidate BFS
and candidate PFS3 means, not individual ratios.

| Workload | Baseline BFS | Candidate BFS | Change | Candidate PFS3 | Ratio | Candidate runs over five times |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 249358.5 | 238830.25 | −4.22% | 18623.5 | 12.824× | 4/4 |
| Lookup 400 | 118046.5 | 106431.75 | −9.84% | 81273.25 | 1.310× | 0/4 |
| Small read 40 | 56976 | 50399.5 | −11.54% | 14800 | 3.405× | 0/4 |
| Write 8 MiB | 395104 | 434957.25 | +10.09% | 42831.5 | 10.155× | 4/4 |
| Read 8 MiB | 550316.75 | 559580.75 | +1.68% | 314350.25 | 1.780× | 0/4 |
| Delete 40 | 178846.75 | 196974.5 | +10.14% | 9423.75 | 20.902× | 4/4 |

Candidate write times are 391998–480318 µs and delete times 174399–221119 µs.
Write changes by +16126, +79233, +17590 and +46464 µs in the first BFS-first,
first PFS3-first, repeat BFS-first and repeat PFS3-first pairs; corresponding
delete changes are +43071, +5696, +832 and +22912 µs. No outlier is removed.
Unchanged read/lookup paths vary materially too, so their lower means are not
attributable to the preflight optimization. PFS3's 431486 µs read and 61375 µs
write, and the baseline PFS3 67326 µs write, also remain in the evidence.

The [complete summary](evidence/bfs-seal-geometry-2026-10-01/measurement-summary.md)
contains every raw tuple, matched delta, candidate ratio range and threshold
failure. Ratios from this series are not multiplied with improvements from
earlier experiments. Fewer generated divisions are not elapsed-time success.

## Bounded implementation and preserved semantics

The candidate captured data start, block count and working/committed backup
block quotients once at entry to the callback-free sealed-commit preflight.
The context was operation-local, not cached across mutations. Full-width
64-bit equality remained in individual block checks; old Free-Tree range
checks retained their existing explicit 32-bit casts. No root, reserve,
pending, all-32-slot historical-pool guard, validation, COW, publication fence,
flush, format or allocation policy changed. The rejected metadata-stock policy
was not restored.

The mounted-owner gate establishes the superblock/BIO pointer aliases used
by the capture. Geometry must remain stable under the existing serialized
mounted-BIO contract. Independent review found no blocking source or test
issue. Normal m68k assembly confirms that two backup divisions are calculated
once before the checks, replacing repeated calls in pool and input loops.
The [review](evidence/bfs-seal-geometry-2026-10-01/independent-review.md) and
assembly excerpts record the boundaries. Static call sites are not dynamic
counts, speed predictions or evidence of hardware performance.

## Tests and restoration

The revised real mounted-owner tests reject both distinct backup positions in
pending input, reserve input, inactive pool and old Free-Tree overlaps. They
also preserve the deliberately different widths for high-half backup offsets,
separately targeting working and committed superblocks. Successful-seal cases
abandon the instance without committing or remounting the malformed in-memory
superblock. Their fake device counts one write but does not trace its address;
no superblock publication follows from the function/cleanup flow, not a direct
write-address oracle. Existing root and exact no-write/state-snapshot rejection
oracles remain in place.

All ten focused tests pass on both the exact baseline allocator snapshot and
the candidate. Candidate complete normal and ASan/UBSan suites each pass
45 suites / 443 tests. Local quality checks pass 70 tests, ShellCheck and
static analysis. Normal/probe m68k builds, 20 Linux conformance tests and actual
FUSE mounts pass. A separate clean normal rebuild is byte-identical.
ASan uses explicit detect_leaks=0; this is not LeakSanitizer qualification.

After rejection, alloc.c is byte-identical to the `8b35eec` source, SHA-256
`120933149aba4c4c82318819c51ebbd48465f8d6af9e989ef240663a484429b1`.
All ten strengthened tests pass again on that restored source normally and
under ASan/UBSan; Linux conformance and real FUSE checks are repeated. The
complete retained-production qualification is the
[preceding exact-path report](bfs-exact-path-buffer-performance-2026-10-01.md).
The additional tests do not change production code.

## Evidence and reproducibility

[Raw evidence](evidence/bfs-seal-geometry-2026-10-01/) includes all result TSVs,
machine inventories, completion markers, configurations, emulator/runner and
builder logs, functional/build logs, input identities and SHA256SUMS. Timing
TSVs are unmodified. Copied logs have only trailing horizontal whitespace
removed. An initial read-only remote preflight used unavailable shasum; its
failure is retained, and the corrected sha256sum preflight verifies all eight
handler/tool inputs before measurements.

Baseline normal handler SHA-256 is
`c4d4241a45b84c768ec9498e77144e650812e31576c952925f4ed1de65be0d5a`;
candidate normal is
`71abbd520974f1d534cd30dc14ae79c9f18f60fb3a50194b57ef5b9a32ac2bee`.
Candidate alloc.c is
`971e6720e57775be06698af2dce58c65b0cdccd387007a152e7372e0b5359cb4`,
and retained test source is
`dc2eb399da7ad6bb3d3354e629630c2d05224d6e39e46479945e3a1d5175414c`.
The unchanged normal guest tool is
`1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80`.
Full probe and fixed-input identities are in the identity log; the candidate
probe was built but not used for these normal acceptance measurements.

The [candidate patch](evidence/bfs-seal-geometry-2026-10-01/candidate.patch)
contains production and test changes against clean `8b35eec`. Apply only on
that base using git apply --unidiff-zero. Its applicability was checked on
exact baseline file copies without modifying production. Replay is diagnostic,
not authorization to enable the rejected candidate. The preparation and runner
scripts retain the exact private workspace paths used in this experiment.

Completed prior exact-path images were moved recoverably to Cachy's private
SSD directory /home/fabian/.cache/bfs-performance/exact-path-buffers-2026-10-01,
with all sixteen timing-file hashes verified before and after the move. No
licensed binaries, ROM, Workbench assets or disk images are committed. Real
power loss, controller cache/flush lies and specific media remain separate,
device-bound qualification. No CI, push, PR, merge or release ran.
coffin.service remains stopped, not disabled.
