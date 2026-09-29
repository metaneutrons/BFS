# BFS/PFS3 AmigaOS performance bottleneck qualification — 2026-09-29

## Result

BFS is not close to PFS3 performance in this FS-UAE workload. Two fresh-image, reversed-order runs of the **normal, uninstrumented BFS handler** gave the following means. Times are microseconds; the ratio is BFS/PFS3. These are two samples, not a confidence interval.

| Checked AmigaDOS phase | BFS mean | PFS3 mean | Ratio |
| --- | ---: | ---: | ---: |
| Create/write/flush/close 40 × 1 KiB files | 3,563,052 | 5,823.5 | 611.8× |
| Lock 40 files ten times | 291,070 | 24,192 | 12.0× |
| Read and verify 40 small files | 90,463 | 4,896.5 | 18.5× |
| Create/write/flush/close one 8 MiB file | 1,275,480.5 | 14,304.5 | 89.2× |
| Read and verify the 8 MiB file | 624,892.5 | 106,528 | 5.9× |
| Delete the 40 small files | 3,439,149 | 2,847.5 | 1,207.8× |

The normal profile-mode pair also measured fresh 8 MiB `Write` calls at 1,227,340 μs for BFS versus 16,245.5 μs for PFS3, and same-file overwrite `Write` calls at 1,202,471 μs versus 5,044.5 μs. Unlike the table's end-to-end write phases, those totals exclude Open, Flush and Close. The overwrite result rules out extent creation as the only write bottleneck.

## Attribution

The final debug-only `deep-compare` v4 profiler ran the same six phases twice on fresh HDFs, again reversing filesystem order. The counters below matched exactly between the two runs. They count underlying handler requests and B-tree operations, not physical-media writes. `other` includes per-file extent trees in this workload.

| Phase | Data I/O calls | B-tree node writes | Free-space tree | Other tree writes | Commits | `CMD_UPDATE` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 40 creates | 40 writes | 4,467 | 4,192 (93.8%) | directory 75, inode 160, other 40 | 40 | 160 |
| 8 MiB write | 2,048 writes | 1,509 | 1,121 (74.3%) | directory 2, inode 258, other 128 | 1 | 4 |
| 8 MiB read | 2,048 reads | 0 | 0 | 0 | 0 | 0 |
| 40 deletes | 0 | 4,012 | 3,896 (97.1%) | directory 76, inode 40 | 40 | 160 |

For the same phases, measured device `DoIO` time was 83–86 ms for create, 54–56 ms for the 8 MiB write, 27–28 ms for the 8 MiB read, and 78–80 ms for delete. The debug-run wall times were about 3.47 s, 1.20–1.21 s, 0.61 s, and 3.31–3.34 s respectively. The device calls therefore cannot explain most of the elapsed time in this emulator.

The B-tree recomputes a full-node CRC on every `node_read`, including cache hits, and before every `node_write` (`src/core/btree.c`). The profiler counted 15,466 node-read CRCs and 4,467 node-write CRCs in 40 creates; 14,934 and 4,012 in 40 deletes. Even the 400 lock lookups caused 1,600 node-read CRCs with only six underlying device reads. The 8 MiB read caused 2,311 node-read CRCs alongside 2,048 data reads.

The profiler times every 64th CRC call and extrapolates by the measured mean per call:

`estimated CRC μs = (read sample ticks / read samples × read calls + write sample ticks / write samples × write calls) × 1,000,000 / 709,379`.

Across the two final runs, this gives approximately 3.00 s for 40 creates, 2.83 s for 40 deletes, 0.97–1.00 s for the 8 MiB write, 0.34–0.36 s for its read, and 0.25 s for the 400 lookups. The 256 empty timer pairs calibrated at each phase reset averaged **under one whole E-clock tick** (`CLOCK_PAIR_TICKS=0` after integer division). Charging a full tick to every CRC call would change the create/delete estimates by less than 29 ms. The every-64th-call sample is deterministic, not random; these are directional estimates, not an exact CPU profile or proof that removing CRC would save the entire estimated time. The earlier full-CRC-timestamp experiment perturbed execution materially and is excluded from this estimate.

## Optimization order

1. **Free-space-tree mutation and commit churn.** It accounts for 93.8% of create and 97.1% of delete node writes. `bfs_freespace_refill_reserve` updates the free tree one block at a time, `bfs_freespace_return_reserve` frees one reserve block at a time, and commit calls reserve return before and after reclamation (`src/core/alloc.c`, `src/core/txn.c`). Instrument these subpaths next, then prototype coalesced reserve return/refill or transaction-local COW-node reuse. Preserve free-block uniqueness/accounting, old-root reachability until publication, ordered-data semantics, short-write behavior and crash recovery. Fault injection at each allocation/write/superblock boundary is mandatory. The present per-tree count does not establish which of these subpaths dominates.
2. **Avoid repeated CRC work for unchanged cached metadata without weakening disk-corruption checks.** A cache entry could carry a validated generation and be revalidated after any write or eviction. That requires an explicit cache-trust contract and corruption tests, including bit flips between fills and after writes. A blind removal of CRC checking is unacceptable. Reducing free-tree churn should also reduce CRC work without changing the integrity model.
3. **Coalesce inode updates across write chunks.** The 8 MiB write makes 128 AmigaDOS `Write` calls but 258 inode-tree writes. The core publishes size/extent-root after each call and the handler updates modification time/archive state after each call (`src/core/file.c`, `src/amiga/handler.c`). Combining these safely could remove one inode COW per chunk, but visibility to other open handles and crash/short-write semantics must be retained. This is smaller than the free-tree problem.
4. **Improve sequential reads and block transport.** The 8 MiB read makes 2,048 one-block data `DoIO`s and 2,311 metadata CRC reads. A per-handle extent cursor or range read could avoid repeated extent-tree lookups; bounded, DMA-safe multi-block I/O could reduce device calls. These changes need sparse-file, checksum, snapshot, MaxTransfer/Mask and injected I/O-error coverage. They will not alone close the 89× write or 600–1,200× small-file gaps observed in FS-UAE.

The existing m68k handler already uses an assembly CRC32 routine (`src/amiga/crc32_68k.s`). The first priority is to eliminate redundant tree/CRC work, not to add more assembly. No optimization in this list has been implemented or credited with a speedup by this report. Performance parity remains a target, not a demonstrated forecast.

## Method, provenance, and limits

Each run used fresh, equal-sized 256 MiB BFS and PFS3 HDF images under FS-UAE 3.2.35, A1200/68040 at maximum emulated speed, Kickstart 47.102 and Workbench 47.2. The mounted BFS and PFS3 volumes reported about 255 MiB and 234 MiB usable space respectively; image size does not imply identical filesystem geometry. The checked compare tool writes deterministic data, verifies every read byte and EOF, and emits `PASS` only after all phases. The profile tool separates fresh-write, overwrite, Flush and Close timings and verifies the final 8 MiB read. The two run orders were BFS-first and PFS3-first. The benchmark filesystems had no BFS data checksums or snapshots enabled; those features need separate performance qualification.

The normal handler installed in all four uninstrumented compare/profile runs had SHA-256 `32f32ee0fc1ce78649e47bbc634d2c53730c75501afff080f0d1ee7c822398b9`. The final deep-compare probe handler had SHA-256 `18c63d4e0238c0f9975a7ea954c9ac21ba08e2398035e60a2454221647097e6f`; its guest compare tool was `35932d56cd1fd5921890dafb4e37f89ec28930d9e84daf1f7e9b467f65170a8a`. The PFS3 handler installed in every run was `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`. An earlier deep-profile probe, retained for the fresh/overwrite/read split, had SHA-256 `380ce13ae29aba6e0273a1c1e5d649310d58e2bba50d4288d1f500d8dddc0e63`; it is not used for the per-tree or sampled-CRC conclusions. The normal compare tool was `ccf19e8fa87ed07bc8e5af80e8af872801bcadb951d65a12525191d4a8db2751`, and the normal/deep profile tool was `a25393b230515e77a3c5384b0c8ce42f08c1f14ede1d71855a963001d042a82e`.

The [evidence directory](evidence/bfs-pfs3-deep-profile-2026-09-29/) contains eight primary run folders and two post-refactor validation folders with raw BFS/PFS3 TSVs, post-format volume inventories, guest completion markers, FS-UAE configurations and emulator logs; [SHA256SUMS](evidence/bfs-pfs3-deep-profile-2026-09-29/SHA256SUMS) covers all 70 copied files. All ten original run directories passed the mode-specific strict verifier, and all copied files were compared byte-for-byte with their originals before the final manifest was checked. The HDFs, licensed ROM/Workbench files and PFS3 binary are not committed. The eight primary runs were built from BFS commit `bda3da9` plus the diagnostic changes in commit `4732ed1`.

After extracting guest benchmark phase functions to satisfy the code-quality gate, one fresh `deep-compare` and one fresh `deep` run passed again. All non-timing BFS counters matched their corresponding primary BFS-first runs exactly. The post-refactor guest compare and profile binaries had SHA-256 `e4702d5e306a1598bc0a0757938903edd19e35faa0bd8ea4a9a0917282ca3366` and `cc31bab19dd2f2476e738fbb07e872e379f4de90a3739da137b34f59c9aa6ff4` respectively; the probe handler was unchanged. Their timings are retained for reproducibility but are not included in the two-order means or the sampled-CRC ranges above.

These timings are FS-UAE guest timings on a host-cached HDF, not physical Amiga/AROS throughput or a power-loss qualification. The same AmigaDOS calls were issued to both filesystems, but equal internal cache policy, write ordering and persistence guarantees were not proven. `CMD_UPDATE` is a handler request, not proof that a controller or physical medium honored a flush. Two runs establish a repeatable local gap, not a statistical confidence interval or a hardware-wide benchmark.
