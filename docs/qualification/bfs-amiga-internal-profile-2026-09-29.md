# BFS AmigaOS internal I/O profile, 2026-09-29

## Finding

Two fresh-image FS-UAE runs produced **identical internal operation counts** for
the same 8 MiB workload. Creating the file caused 16,510 underlying device writes,
2,072 reads, 6,161 free-space allocation calls and 2,048 extent-map calls.
Overwriting the same file caused 2,919 writes, 34 reads, 256 allocation calls and
no extent-map calls. Both phases issued four `CMD_UPDATE` requests.

The 8 MiB file has 2,048 logical 4 KiB data blocks. The initial phase therefore
issued 8.06 underlying writes per data block in total. The code writes each new
data block once before publishing its extent mapping (`src/core/file.c`); on that
basis, 14,462 of the 16,510 requests are additional metadata or bookkeeping
writes. This is a *request count*, not a physical-media write amplification or
a durability claim. The exact distribution of those additional writes across
free-space, extent and other trees remains unmeasured.

## Method and validity

- Source before this debug-only change: `6d8a7a5f1c113ba402aeb780a0ec12a89fc2cd64`.
  The normal `make amiga` handler remains byte-identical to the prior comparison
  build, SHA-256 `09ad9eb7308b87ce5c112ba4f7ece786bff0d1a64d416b34f9f690987819a647`.
  The separate `bfshandler-probe` binary has SHA-256
  `acaa15b0e6c01d7158cde1187c24262af798c8aebfa292106aef494a72feb689`;
  the guest profile tool has SHA-256
  `244378e8784c496de6c774b1b55c664a284f8129ce93a82873d792680165cd34`.
- Same host, FS-UAE A1200/68040, Kickstart/Workbench and 255.5 MiB BFS geometry
  as [the initial comparison](bfs-pfs3-performance-2026-09-28.md). Only BFS was
  profiled here; the PFS3 volume was mounted but no PFS3 workload was run.
- `BFS_PERF_PROBE` adds counters and two private reset/read packets only to a
  disposable debug handler. It counts actual underlying `DoIO` read/write/update
  attempts (not cache hits), calls to `bfs_freespace_alloc`, and calls to
  `bfs_extent_map_block`. The guest reset the counters after opening each file,
  then sampled after `Write`/`Flush`/`Close`. Thus the counts exclude `Open` but
  include all three measured calls. No per-block logging or output was performed.
- Each run used newly built HDFs and passed the strict internal-mode verifier:
  mounted read/write BFS volume, valid exact-row TSV, checked 8 MiB read, `PASS`,
  and guest completion marker. The guest originals were byte-compared with the
  committed [raw evidence](evidence/bfs-amiga-internal-2026-09-29/).

## Results

| Phase | Metric | Run 1 | Run 2 |
| --- | --- | ---: | ---: |
| New file | `Write` time | 12.201 s | 10.826 s |
| New file | Device reads | 2,072 | 2,072 |
| New file | Device writes | 16,510 | 16,510 |
| New file | `CMD_UPDATE` calls | 4 | 4 |
| New file | Free-space allocation calls | 6,161 | 6,161 |
| New file | Extent-map calls | 2,048 | 2,048 |
| Overwrite | `Write` time | 1.840 s | 1.431 s |
| Overwrite | Device reads | 34 | 34 |
| Overwrite | Device writes | 2,919 | 2,919 |
| Overwrite | `CMD_UPDATE` calls | 4 | 4 |
| Overwrite | Free-space allocation calls | 256 | 256 |
| Overwrite | Extent-map calls | 0 | 0 |

The write-time variation shows that these two samples cannot assign a precise
latency to an individual device request. The deterministic count difference is
nevertheless large: 13,591 more writes and 2,038 more reads on file creation
than overwrite. The previous API-phase profile found that `Write`, not
`Flush`/`Close`, dominated the guest-visible duration. Neither experiment yet
separates device-service time from CPU time in the B-tree/allocator code.
Overwriting follows file creation in both runs, so cache warmth is also a
possible contributor to the time difference.

## Reproduction and next decision

Build `make amiga tools amiga-perf-probe-handler amiga-fs-profile-bench` and set
`BFS_BENCH_MODE=internal`, `BFS_BENCH_HANDLER_FILE` to the absolute path of
`build/amiga/bfshandler-probe`, the licensed AmigaOS/ROM/PFS3 asset variables
described in the comparison report, and a fresh `BFS_BENCH_RUN_DIR`. Then run
`emulator-test/build-bench-image.sh` and `emulator-test/run-bench.sh 180`.
`emulator-test/verify-bench-results.sh RUN_DIR internal` rechecks the output.

The next optimization candidate is reducing per-block metadata COW/allocation
I/O for sequential new extents, while preserving BFS's transaction and recovery
guarantees. Before changing that path, distinguish the 14,462 additional writes
by metadata category or measure aggregate device-service time; do not infer that
removing `CMD_UPDATE` is the answer from these counts. Any candidate must be
rerun with this counter harness, the original workload, and fault/crash tests.

SHA-256 of committed guest evidence:

| Artifact | SHA-256 |
| --- | --- |
| Run 1 TSV | `d260f45e6c1c5c9712ccc326c1ddbaf8939184c6a94d3246568093d35365153e` |
| Run 2 TSV | `13444f91c0e23f1e7ddf7b44e790426e93c6a748767042ae3f67c84ae51a32b1` |
| Mount inventory (identical) | `459660577a72a25fa8134102a459f43d64aa14dc20bc691cdc101925bd485008` |
| Completion marker (identical) | `8ba1a480268413cfe1a54ddfca2468de9f3e70bf697b7cc784fe6171c8aaebd6` |
