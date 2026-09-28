# BFS versus PFS3: emulated AmigaOS performance, 2026-09-28

## Status and scope

Two complete, checked runs on FS-UAE 3.2.35, with reversed filesystem order. This is
guest-observed AmigaDOS operation latency on one emulated machine, **not** physical-media
throughput, power-loss durability, or a general ranking of the filesystems. Lower times
are better. The four unmodified guest TSVs, the identical post-format mount inventories
from both runs, and their identical completion markers are in
[`evidence/bfs-pfs3-2026-09-28/`](evidence/bfs-pfs3-2026-09-28/).

## Method

- Source: `origin/main` at `3243afb7efc4f3c1433cd2e591ed847bf94323a9`, plus the
  benchmark harness in this change. Built with `m68k-amigaos-gcc` 6.5.0b.
- Host: Apple M5 Max, arm64, macOS 26.5.1. FS-UAE A1200, emulated 68040 at `max` speed,
  2 MiB chip RAM, 8 MiB fast RAM, Kickstart 47.102, Workbench 47.2.
- One 256 MiB RDB HDF per filesystem, each with a 255.5 MiB partition (`start=2`,
  `end=1023`, 16 heads, 32 sectors/track, 512-byte sectors, 30 RDB buffers), attached
  through the same FS-UAE HDF backend. DH1: was BFS, formatted with the host BFS CLI
  at 4096-byte logical blocks. DH2: was PFS3, formatted by AmigaOS `Format ... QUICK`
  with its defaults. Both mounted read/write before timing. AmigaDOS `Info` reported
  BFSTest as 255M and PFSTest as 234M usable; equal partition geometry does not imply
  equal reported usable space.
- Same `fs-compare-bench` binary on both: 40 separate 1 KiB file creations with write,
  `Flush`, and `Close`; 400 `Lock`/`UnLock` lookups; 40 1 KiB reads with byte comparison
  and EOF checks; an 8 MiB sequential write in 64 KiB chunks with `Flush` and `Close`;
  an 8 MiB sequential read in 64 KiB chunks with byte comparison and EOF check; and
  40 deletes. All phases use `timer.device` microsecond timestamps. Each run uses new
  HDFs. Run A measured BFS first; run B measured PFS3 first.
- The runner accepted a run only after both guest TSVs had all six positive metrics,
  correct volume identifiers and `PASS`, both volumes appeared in post-format `Info`,
  and a final guest completion marker existed. Both runs passed
  `emulator-test/verify-bench-results.sh`.

## Results

All figures are milliseconds for the whole phase. The last column is the ratio of the
two-run arithmetic means, BFS/PFS3 (thus >1 means PFS3 completed that operation sooner).

| Phase | BFS A | PFS3 A | BFS B | PFS3 B | Mean ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 × 1 KiB files | 3440.812 | 5.632 | 3429.164 | 5.568 | 613.4× |
| Lock/unlock 400 files | 279.550 | 23.551 | 281.790 | 23.680 | 11.9× |
| Read/check 40 × 1 KiB files | 85.632 | 4.543 | 86.975 | 4.672 | 18.7× |
| Write/flush/close 8 MiB | 10300.613 | 13.632 | 10553.541 | 13.440 | 770.3× |
| Read/check 8 MiB | 918.843 | 105.856 | 934.841 | 103.743 | 8.8× |
| Delete 40 files | 3252.015 | 3.008 | 3435.115 | 2.880 | 1135.7× |

Run A and B agree closely despite the reversed order. The large differences are
therefore observable in this exact emulated workload; these two runs do not establish
their cause. `Flush` and `Close` are AmigaDOS handler acknowledgements, not proof that
the bytes reached nonvolatile media. The post-run PFS3 HDF occupied about 17 MiB on
the host, but this does not substitute for a reboot/remount or power-loss check. Host
and guest caches, emulation, different handler algorithms and format defaults remain
possible contributors. No confidence interval is claimed from two runs.

## Reproduction and evidence integrity

Build with `make amiga tools amiga-fs-compare-bench`. Supply lawfully obtained Workbench
3.2 `C`, `L`, and `Libs` directories, Kickstart ROM, and PFS3 handler through
`BFS_AMIGA_ASSETS_DIR`, `BFS_ROM_FILE`, and `BFS_PFS3_HANDLER`. For each fresh run
directory, set `BFS_BENCH_RUN_DIR` and `BFS_BENCH_ORDER` (`bfs-first` or `pfs3-first`),
then run `emulator-test/build-bench-image.sh` and `emulator-test/run-bench.sh 180`.
The scripts refuse to overwrite an existing run. The ignored original run directories
are `build/benchmark/checked-bfs-first-1` and `build/benchmark/checked-pfs3-first-1`;
the committed TSV copies were byte-compared with their guest originals.

SHA-256 identities:

| Artifact | SHA-256 |
| --- | --- |
| Kickstart 47.102 ROM | `68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c` |
| `pfs3aio` 20.0 handler | `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7` |
| BFS handler | `09ad9eb7308b87ce5c112ba4f7ece786bff0d1a64d416b34f9f690987819a647` |
| Checked benchmark binary | `e34342ef9f960405b4004f990351bbba79958b1b944649bc0a7d4ab6250b7802` |
| Host BFS formatting CLI | `8f0ee52e4ece1f48ab58ce3947f1898900231ba25479e0941b29926aabb2e874` |
| Run A BFS TSV | `a92548ec0115630cd006b2f54511efa966f81bc6d19117c7cb5733b0510ef104` |
| Run A PFS3 TSV | `c6694dd59f7c4a1ac5c5b2dead842c0f9abc5f23ef57c64f30b22c80a614bd6c` |
| Run B BFS TSV | `83582cbd0439d4b792c7ce3c727efb098ec8eaae7e2a7fd76654f12963d14a2f` |
| Run B PFS3 TSV | `049c334ddb7ec94051fc16df21184fc29536f1297935da7aa2d9746776b6e95f` |
| Post-format mount inventory (identical in A/B) | `459660577a72a25fa8134102a459f43d64aa14dc20bc691cdc101925bd485008` |
| Completion marker (identical in A/B) | `29229f6e4166508e724e5c1fdc73d9015c120ddabc2d61c24af283287d6afc83` |

Exploratory DiskSpeed attempts are excluded: they never produced a completed,
two-filesystem comparison. No result from those attempts appears in the table.
