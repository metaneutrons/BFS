# BFS batched append performance qualification — 2026-09-29

## Result

The paired ABBA comparison below contains two baseline and two pre-final-code candidate runs, ordered baseline-1, candidate-1, candidate-2, baseline-2. Its mean 8 MiB fresh-file Write time fell from 10,415,553.5 μs to 1,161,156 μs: a reduction of 9,254,397.5 μs (88.85%). Mean overwrite Write time fell from 1,430,165.5 μs to 1,118,601 μs (311,564.5 μs; 21.79%). The subsequent 8 MiB read-and-verify time fell by 331,454 μs (35.57%). The pre-final-code candidate-refactor-1 and final-code candidate-final-1 runs are validations only and are excluded from these means.

In those pre-final-code paired runs, fresh-creation device reads fell from 2,072 to 2 and writes from 16,510 to 3,529. Free-space allocation calls fell from 6,161 to 512; extent-map calls fell from 2,048 to 128. Overwrite write requests changed from 2,919 to 2,911. These are handler request and function-call counts, not physical-media write amplification.

The optimization is limited to aligned sequential appends on volumes with data checksums disabled and no snapshots. Other write modes continue to use the existing per-block path.

## Exact run data

Timings are microseconds; all other values are counts. The first four columns are the pre-final-code paired run order. The fifth column is pre-final-code validation; the sixth is final-code validation. Neither validation run is included in the ABBA means.

| Metric | Baseline 1 | Candidate 1 | Candidate 2 | Baseline 2 | Candidate refactor 1 | Candidate final 1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| FRESH_WRITE_US | 10,131,933 | 1,174,651 | 1,147,661 | 10,699,174 | 1,118,600 | 1,173,787 |
| FRESH_FLUSH_US | 0 | 0 | 0 | 0 | 0 | 0 |
| FRESH_CLOSE_US | 61,760 | 59,456 | 59,200 | 64,256 | 57,855 | 59,391 |
| FRESH_BIO_READS | 2,072 | 2 | 2 | 2,072 | 2 | 2 |
| FRESH_BIO_WRITES | 16,510 | 3,529 | 3,529 | 16,510 | 3,529 | 3,529 |
| FRESH_BIO_UPDATES | 4 | 4 | 4 | 4 | 4 | 4 |
| FRESH_FREESPACE_ALLOCS | 6,161 | 512 | 512 | 6,161 | 512 | 512 |
| FRESH_EXTENT_MAPS | 2,048 | 128 | 128 | 2,048 | 128 | 128 |
| OVERWRITE_WRITE_US | 1,367,964 | 1,087,001 | 1,150,201 | 1,492,367 | 1,069,389 | 1,110,433 |
| OVERWRITE_FLUSH_US | 0 | 0 | 0 | 0 | 0 | 0 |
| OVERWRITE_CLOSE_US | 61,504 | 59,840 | 60,033 | 64,640 | 59,648 | 61,760 |
| OVERWRITE_BIO_READS | 34 | 2 | 2 | 34 | 2 | 2 |
| OVERWRITE_BIO_WRITES | 2,919 | 2,911 | 2,911 | 2,919 | 2,911 | 2,911 |
| OVERWRITE_BIO_UPDATES | 4 | 4 | 4 | 4 | 4 | 4 |
| OVERWRITE_FREESPACE_ALLOCS | 256 | 256 | 256 | 256 | 256 | 256 |
| OVERWRITE_EXTENT_MAPS | 0 | 0 | 0 | 0 | 0 | 0 |
| READ_VERIFY_8M_US | 913,211 | 603,837 | 596,860 | 950,394 | 593,660 | 603,197 |

## Mean reductions

These pre-final-code ABBA means compare baseline-1/2 with candidate-1/2 only. Candidate-refactor-1 and candidate-final-1 are validation samples and are not included. Reduction percentage is (baseline mean − candidate mean) / baseline mean. Flush measurements were zero in all runs; zero is below the timer's reported resolution and does not establish zero cost.

| Metric | Baseline mean | Candidate mean | Absolute reduction | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Fresh Write time (μs) | 10,415,553.5 | 1,161,156 | 9,254,397.5 μs | 88.85% |
| Fresh Close time (μs) | 63,008 | 59,328 | 3,680 μs | 5.84% |
| Fresh device reads | 2,072 | 2 | 2,070 requests | 99.90% |
| Fresh device writes | 16,510 | 3,529 | 12,981 requests | 78.63% |
| Fresh free-space allocations | 6,161 | 512 | 5,649 calls | 91.69% |
| Fresh extent-map calls | 2,048 | 128 | 1,920 calls | 93.75% |
| Overwrite Write time (μs) | 1,430,165.5 | 1,118,601 | 311,564.5 μs | 21.79% |
| Overwrite Close time (μs) | 63,072 | 59,936.5 | 3,135.5 μs | 4.97% |
| Overwrite device reads | 34 | 2 | 32 requests | 94.12% |
| Overwrite device writes | 2,919 | 2,911 | 8 requests | 0.27% |
| Read and verify 8 MiB (μs) | 931,802.5 | 600,348.5 | 331,454 μs | 35.57% |

The following counters were unchanged: fresh and overwrite CMD_UPDATE requests (4 each), overwrite free-space allocations (256), and overwrite extent-map calls (0).

## Pre-final-code refactor validation

The separate candidate-refactor-1 run used a fresh HDF and passed the strict internal verifier. It predates the Codacy-driven final core fix. Its exact TSV is included in the table and preserved with its inventory and completion marker; it is not another sample in the ABBA means.

## Final core Codacy-fix validation

The final core fix added a scratch_capacity check and replaced the slower bytewise probe with memcpy. A fresh-HDF internal run, candidate-final-1, passed the strict internal verifier with debug handler SHA-256 939de3adc6fe6e4d9a2818375d9eeeb85714d54c91bf5f058998b4b046a7371f and guest-tool SHA-256 4ef37a8b78ec6cbb5eddb454a33c99604475aa32b105b150384e70ab69e8c2ac. Fresh Write measured 1,173,787 μs; its counters were 2 reads, 3,529 writes, 4 updates, 512 free-space allocations, and 128 extent-map calls. The remaining timing and counter values appear in the final column above. This is a single validation run, not part of the pre-final-code paired means.

## Method and verification

Each isolated FS-UAE run used the same A1200 configuration with a 68040 CPU at maximum emulated speed, the same guest system and 4 KiB BFS block size, and a fresh benchmark disk image. The BFS partition was 255.5 MiB. FS-UAE logs identify version 3.2.35. This is an emulator measurement, not a physical Amiga or storage-device measurement.

The guest tool wrote an 8 MiB deterministic pattern in 128 sequential 64 KiB Write calls. For the fresh phase, it opened a new file, reset handler counters, timed Write calls, then timed Flush and Close and read the counters. It then reopened the same file read-write, sought to offset zero, repeated the 8 MiB write, and finally read the full file back, checked every byte and checked EOF. The overwrite always followed creation in the same run, so order and cache warmth may influence timings. The counters measure underlying handler DoIO read/write/update attempts, plus free-space allocation and extent-map function calls. The timed Write totals exclude file Open; the counter window begins after Open and includes Write, Flush, and Close. The read time includes byte comparison and EOF verification.

The four pre-final-code paired runs, candidate-refactor-1, and candidate-final-1 all passed emulator-test/verify-bench-results.sh in internal mode. The verifier checks the completion marker, nonempty post-format inventory text containing the expected DH1 and DH2 read/write entries, and the ordered BFS-only TSV ending in PASS. It does not run a PFS3 workload or establish that DH2 stayed available during or after BFS profiling. Earlier pre-format info.txt captures reported DH2 as Not a DOS disk; the post-format inventory files list PFSTest as read/write. The guest tool checked the 8 MiB readback. Raw TSVs, post-format inventories, and completion markers are preserved under evidence/bfs-batched-append-2026-09-29/; their SHA-256 values are listed in [SHA256SUMS](evidence/bfs-batched-append-2026-09-29/SHA256SUMS).

| Run | Raw TSV SHA-256 |
| --- | --- |
| baseline-1 | 50f179b80c4093bf4c0815e58daac65496b6de245d346b8066978e59e69521f9 |
| candidate-1 | bc824f87390ca1bae8d18bbf1b6ec875ecea25083d818edeabef14b19e8faa47 |
| candidate-2 | b35673dcaff5c3f4da3cb81d66a87f4a06f0a8421404dc64f3140b7d3e0e7603 |
| baseline-2 | f027448f25056846c91228ee07fb1be4c7d5fd88558be99405b442d2e406b8cf |
| candidate-refactor-1 | b6de61d926edc8129996cb2d33532b8d8d95d993a3a9ff32dcdced898ce0b92a |
| candidate-final-1 | b8d13fd8edcc6be267f6c2a8c367afbce15c42e8eb11631a2e9beff6a151ec63 |

## Binary provenance and current status

The measured artifacts were the handler and guest tool copies installed in the benchmark system directories. Both baseline runs used the same baseline handler, SHA-256 9cd34594dc113e3d43f66e4e1bd9132658858b748507d31b094e78df0e5ff33c. The two paired candidate runs used the same pre-refactor handler, SHA-256 7ccaa867ec9be52410c23b83536e1976203ce2ce81db569e48e56a2d8cf02bfb. The guest fs-profile-bench tool SHA-256 was 4ef37a8b78ec6cbb5eddb454a33c99604475aa32b105b150384e70ab69e8c2ac.

The pre-final-code candidate-refactor-1 validation used handler SHA-256 bdd5924571eb4b4c3646ecd63d954f41c6667e1fb8804cc1f9faa4dc7de0281e and the same guest-tool hash. The final debug handler used for candidate-final-1 has SHA-256 939de3adc6fe6e4d9a2818375d9eeeb85714d54c91bf5f058998b4b046a7371f. This final-code validation is a single fresh-HDF run.

Before the final Codacy fix, host tests, including fault-injection and crash-cut tests, completed; `make sanitize`, `make analyze`, `make quality-gates`, `make conformance-test`, and `make compatibility-test` passed. Coverage tests passed and `gcovr` reported 88.5% line coverage of `src/core`, above the 85% gate. The crash-cut test found three repairable metadata leaks, also reproducible with the legacy per-block writer. Its success condition requires the file to be old or complete and a successful fsck repair; it does not require zero leaks immediately after a crash. The separate `make fault-qualification` run completed 9 cases with `qualified=true` and passed. These host/fault results predate the final core fix.

The first full Amiga `make ci-test` run made no progress for over six minutes at `deep_04` and was manually terminated; the cause is unknown. The isolated deep and first-four filters passed afterward. A full rerun, `build/emulator/run.SRjoqA`, passed 46/46 checks with the pre-final normal handler. That run predates the Codacy-driven final core fix; CI for the final fix is pending. This records local results only and makes no claim about other CI systems.

## Limits

FS-UAE is not physical hardware. The measured device operation counters do not measure physical-media write amplification, controller behavior, or power-loss durability. The fixed workload is an aligned 8 MiB sequential write followed by a same-file overwrite and verified read; it does not establish performance for random writes, partial blocks, checksummed volumes, snapshot-enabled volumes, or physical devices.

The general disk-full suite passed, but a deterministic test of the contiguous-allocation `NOSPC` fallback with free blocks available only in fragments was not achieved without altering allocator internals. That branch remains a qualification gap.

## Direct BFS/PFS3 comparison before the final Codacy fix

This is a separate pre-final-code compare-mode measurement; its timings are not included in the internal-profile means above and do not measure the final Codacy-fixed handler. Both runs used the then-current non-probe BFS handler, SHA-256 b728f2858f34d994d5e4a35da8c2bb9d5ffbe81976443d9032385db7ee404ba0, and the same guest fs-compare-bench tool (SHA-256 e34342ef9f960405b4004f990351bbba79958b1b944649bc0a7d4ab6250b7802). Run 1 measured BFS first; run 2 reversed the order and measured PFS3 first. Both used FS-UAE 3.2.35, fresh HDFs, the same A1200/68040 emulator setup, and the same 255.5 MiB partitions. Both passed emulator-test/verify-bench-results.sh build/benchmark/<run> compare.

The compare verifier checked the completion marker, the post-format inventory's BFS and PFS3 read/write entries, both filesystem TSVs with the expected drive labels and six positive timing fields, and each PASS row. The guest workload byte-checked the 40 small-file reads and the full 8 MiB sequential read. It timed 40 creations of 1 KiB files, 400 Lock/UnLock lookups, 40 checked 1 KiB reads, a new 8 MiB sequential file written in 64 KiB calls including Flush and Close, an 8 MiB checked read, and 40 file deletions. All values below are microseconds. The last column is the ratio of the two-run arithmetic means, BFS/PFS3.

| Phase | BFS run 1 | PFS3 run 1 | BFS run 2 | PFS3 run 2 | BFS mean | PFS3 mean | Mean ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 × 1 KiB files | 3,572,269 | 5,824 | 3,381,869 | 5,824 | 3,477,069 | 5,824 | 597.02× |
| Lock/Unlock 400 files | 291,453 | 23,935 | 281,023 | 23,871 | 286,238 | 23,903 | 11.97× |
| Read/check 40 × 1 KiB files | 90,496 | 4,928 | 87,423 | 4,608 | 88,959.5 | 4,768 | 18.66× |
| Write/flush/close new 8 MiB file | 1,266,425 | 14,719 | 1,230,265 | 14,784 | 1,248,345 | 14,751.5 | 84.62× |
| Read/check 8 MiB | 609,213 | 106,048 | 600,380 | 104,448 | 604,796.5 | 105,248 | 5.75× |
| Delete 40 files | 3,348,717 | 3,072 | 3,311,982 | 2,752 | 3,330,349.5 | 2,912 | 1,143.66× |

The pre-final handler still had a large gap in this workload: the mean create phase was about 597 times slower than PFS3, the delete phase about 1,144 times slower, and the new sequential 8 MiB write about 84.6 times slower. The two order-reversed runs show the measured timings for this exact workload; they do not explain the cause or establish a general performance ranking.

The pre-final direct comparison is guest-observed AmigaDOS latency in FS-UAE, not physical-device throughput, write amplification, or power-loss durability. It covers these six operations only and uses two runs, so it provides no confidence interval. Format defaults, handler algorithms, guest and host caches, and emulation remain possible contributors. It does not establish performance for random or partial-block writes, checksum-enabled or snapshot-enabled volumes, or physical Amiga hardware. Exact guest output is preserved in evidence/bfs-batched-append-2026-09-29/candidate-compare-1/ and candidate-compare-2/.

## Final-handler direct comparison validation

The separate candidate-final-compare-1 run used PFS3-first order and the final normal handler (SHA-256 32f32ee0fc1ce78649e47bbc634d2c53730c75501afff080f0d1ee7c822398b9) with guest fs-compare-bench SHA-256 e34342ef9f960405b4004f990351bbba79958b1b944649bc0a7d4ab6250b7802. It used a fresh HDF and passed the strict compare verifier. This one-run validation is not included in the pre-final two-run means or ratios above.

| Phase | BFS time (μs) | PFS3 time (μs) |
| --- | ---: | ---: |
| Create 40 × 1 KiB files | 3,603,562 | 5,631 |
| Lock/Unlock 400 files | 290,046 | 23,871 |
| Read/check 40 × 1 KiB files | 91,137 | 4,544 |
| Write/flush/close new 8 MiB file | 1,209,850 | 14,144 |
| Read/check 8 MiB | 613,116 | 105,727 |
| Delete 40 files | 3,454,509 | 2,815 |

This PFS3-first final-handler run validates the complete checked workload, but one run does not establish a new two-run mean ratio. Its exact BFS and PFS3 TSVs, post-format inventory, completion marker, and verifier result are preserved under evidence/bfs-batched-append-2026-09-29/candidate-final-compare-1/.
