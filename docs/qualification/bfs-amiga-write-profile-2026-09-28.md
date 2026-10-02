# AmigaDOS BFS write-phase profile, 2026-09-28

## Finding

In two complete FS-UAE runs, BFS spent almost all measured time for a new 8 MiB file
inside the `Write` calls: 10.60 and 10.83 seconds. Writing 8 MiB again to the same
file took 1.42 and 1.47 seconds. `Flush` was measured as 0–65 microseconds and
`Close` about 62–67 milliseconds. The new-file write path therefore warrants
investigation first; removing a `Flush` call cannot explain this result.

This profile identifies an AmigaDOS API phase, **not** the exact internal function.
The overwrite followed the fresh write in each run, so allocation/extent work and
cache warmth are not experimentally separated. The source performs a block allocation
and extent mapping for each new 4 KiB file block (`src/core/file.c`), with a
write-through cache (`src/core/cache.c`) and synchronous Amiga device I/O
(`src/amiga/amiga_bio.c`). Those are hypotheses for the internal cost, not yet
measured at function level.

## Method

- Baseline source: `d60a82368298cc768a9a9b025c79127a8b1cbe66`, the checked BFS/PFS3
  comparison branch, plus the profiler in this change. BFS handler binary SHA-256:
  `09ad9eb7308b87ce5c112ba4f7ece786bff0d1a64d416b34f9f690987819a647`.
  Profile binary SHA-256:
  `dd58d7a9e5cabe5d0b18315dd39d401723a96bbc016bbc2dc62446f604714ce4`.
- Host/guest environment, licensed asset identities, HDF geometry and format procedure
  match [the comparison report](bfs-pfs3-performance-2026-09-28.md): Apple M5 Max,
  macOS 26.5.1, FS-UAE 3.2.35, A1200/68040 at `max`, Kickstart 47.102 and
  Workbench 47.2; BFS 4 KiB logical blocks, PFS3 default format. Each run used new
  255.5 MiB RDB partitions on separate HDFs of the same virtual device type.
- The same `fs-profile-bench` binary ran on both mounted volumes. It first wrote a
  new 8 MiB file in 128 `Write` calls of 64 KiB, then opened it with
  `MODE_READWRITE`, sought to offset zero and overwrote 8 MiB, then read all bytes
  back and checked EOF. The `Write` metric sums individual call durations; `Flush`
  and `Close` were timed separately with `timer.device`. The fixed test pattern was
  byte-checked on read. File-open time and timer-call overhead are excluded from
  the write-call sums.
- Run A measured BFS first; run B measured PFS3 first. Both post-format `Info`
  inventories listed DH1: and DH2: read/write. Both runs had the expected guest
  completion marker and passed `verify-bench-results.sh ... profile`. The four
  byte-identical guest TSV copies, common mount inventory and completion marker are
  retained in [`evidence/bfs-amiga-profile-2026-09-28/`](evidence/bfs-amiga-profile-2026-09-28/).

## Timings

Values are milliseconds. Each `Write` row sums the 128 calls; lower is better.

| Phase | BFS A | BFS B | PFS3 A | PFS3 B |
| --- | ---: | ---: | ---: | ---: |
| New-file `Write` calls | 10595.095 | 10826.349 | 15.433 | 14.556 |
| New-file `Flush` | 0 | 0.065 | 0.063 | 0 |
| New-file `Close` | 62.465 | 64.896 | 0.065 | 0 |
| Same-file overwrite `Write` calls | 1416.370 | 1467.509 | 4.753 | 4.234 |
| Overwrite `Flush` | 0 | 0 | 0 | 0 |
| Overwrite `Close` | 63.745 | 66.752 | 0.063 | 0 |
| Read and verify 8 MiB | 936.059 | 972.540 | 105.855 | 108.416 |

BFS's mean new-file `Write` sum was 10.711 s versus 1.442 s for overwrite,
a 7.43× difference. The 9.269 s gap is 86.5% of the new-file `Write` time,
but it must not be assigned entirely to allocation without internal counters and
an order/cache control. Zero-microsecond phase values mean below measured
resolution, not zero cost. PFS3 is shown as a reference only: its cache and flush
semantics may differ, and no physical-media or power-loss throughput is inferred.

## Reproduction and next measurement

Build with `make amiga tools amiga-fs-profile-bench`. Supply the same licensed
Workbench, ROM and PFS3 assets as in the comparison report. For each fresh run
directory, set `BFS_BENCH_MODE=profile`, `BFS_BENCH_RUN_DIR`, and
`BFS_BENCH_ORDER` (`bfs-first` or `pfs3-first`), then run
`emulator-test/build-bench-image.sh` and `emulator-test/run-bench.sh 180`.

The next focused experiment should aggregate, without per-block logging, the
number and elapsed time of device reads, writes and `CMD_UPDATE` calls in
`amiga_bio.c`, plus allocation and extent-map operations in the BFS core. Compare
new-file and overwrite counts on the same workload, and counterbalance their order
or start from equivalent cold-mounted images. Any optimization must retain BFS's
commit and recovery guarantees; replacing write-through or sync semantics solely
to improve these timings would change the contract being measured.

SHA-256 of committed raw guest outputs:

| Artifact | SHA-256 |
| --- | --- |
| Run A BFS TSV | `5df59bd4337db7e918390a0418940f662e0489eb88ffa65f6e1a8b89e789d620` |
| Run A PFS3 TSV | `a3c817755cde8f8a53271fa90165a7132679e3a00e76a2e71dc89f315347a695` |
| Run B BFS TSV | `f5477ad1c0b21e71e9e2cad1c46edc076581829b0903601cf24aec2919e9c988` |
| Run B PFS3 TSV | `c28153a90babacab4dc48f7bcb981593af5fcbf7aca4549c544ca761900e89ca` |
| Mount inventory (identical A/B) | `459660577a72a25fa8134102a459f43d64aa14dc20bc691cdc101925bd485008` |
| Completion marker (identical A/B) | `9b141a513bcf68b9e887e7c35859fd18646ccd35de5449dbe31990191240c273` |
