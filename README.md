# BFS - B+tree File System for AmigaOS / AROS

![CI](https://github.com/metaneutrons/bfs/actions/workflows/ci.yml/badge.svg)
[![License: MPL-2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](LICENSE)

> **⚠️ WARNING: Experimental software.** BFS has not yet been battle-tested.
> Always keep backups of your data and use at your own risk.

> **Release status:** v0.1.1 is qualified by the host and AROS/FS-UAE
> integration gates and ships handler builds for 68020, 68030, 68040, 68060 and
> 68080. Physical Apollo 68080 hardware and AMMX are not qualified.

## Motivation

[PFS3](https://github.com/tonioni/pfs3aio) is the gold standard Amiga filesystem — fast, reliable, and battle-tested for over 30 years. But its 1990s architecture has hard limits:

- **O(n) directory scans** — linear search through linked blocks
- **Anode chains** — file extent lookup is O(n) in fragment count
- **No checksums** — silent corruption goes undetected
- **~1.6 TB limit** — practical PFS3 partition-size ceiling

BFS is a **clean-break successor** with a modern on-disk format. It is NOT a
fork of PFS3 — it is a complete fresh implementation from scratch with zero shared
code.

## What BFS does better

| Feature | PFS3 | BFS |
|---------|------|------|
| Directory lookup | O(n) linear scan | O(log n) B+tree |
| File extent lookup | O(n) anode chain | O(log n) B+tree |
| Metadata checksums | None | CRC32 on every block |
| Crash safety | Journal replay | COW + dual superblocks |
| Data consistency | None | Optional `data=ordered` core API mode |
| Snapshots | — | B+tree based (Read-only) |
| Metadata compaction | — | **Online B+tree compaction** |
| Max filename | 107 chars | 255 chars |
| Address-space ceiling | ~1.6 TB (~1.46 TiB, practical) | <4 TiB at 1 KiB blocks; <16 TiB at 4 KiB blocks (format address limit) |
| Hard links | Yes | Yes |
| Soft links | Yes | Yes |
| File comments | Yes | Yes |
| Free space tracking | Bitmap | Self-hosting B+tree |
| Automated tests | — | Core, fault-injection and emulator suites |

The PFS3 figure is a practical decimal-size limit. The BFS figures are binary
TiB limits imposed by the on-disk block address space; 16 TiB is approximately
17.59 TB.

## Architecture

```plain
┌──────────────────────────────────────────────────────────────────────┐
│ AmigaOS Glue Layer                                                   │
│ handler.c (DOS packets) · amiga_bio.c (device) · startup.s · 68k asm │
├──────────────────────────────────────────────────────────────────────┤
│ Operations                                                           │
│ namespace.c (dir CRUD, rename, links) · file.c (I/O) · snapshot.c    │
├──────────────────────────────────────────────────────────────────────┤
│ Metadata trees  (key → value, all on the shared engine)              │
│ dir.c (names) · extent.c (file maps) · inode.c · refcount.c (snap)   │
├──────────────────────────────────────────────────────────────────────┤
│ B+tree engine                                                        │
│ btree.c — one generic copy-on-write B+tree, used by every tree above │
├──────────────────────────────────────────────────────────────────────┤
│ Storage & transactions                                               │
│ fs.c (format/mount) · txn.c (COW commit) · superblock.c (dual-SB)    │
│ alloc.c + bootstrap_alloc.c (free space) · cache.c (LRU) · crc32.c   │
└──────────────────────────────────────────────────────────────────────┘
```

The B+tree engine is shared across all metadata types, utilizing a **dynamic transaction tracking** architecture that ensures session-wide consistency and safe COW reclamation. It supports **online compaction** for metadata trees to maintain performance without downtime.

`data=ordered` and metadata compaction are core API capabilities. The current
Amiga `bfs format` does not expose arbitrary format-option flags.

The normative v2 byte layout is documented in [the on-disk format specification](docs/on-disk-format.md).

- **Directory tree** — (parent_id, hash, name) → (inode, type)
- **Extent tree** — file_block → (disk_block, length, data CRC32)
- **Inode tree** — inode_id → metadata
- **Free space tree** — block_nr → length (self-hosting)
- **Refcount tree** — block_nr → refcount (snapshot block sharing)
- **Snapshot tree** — snapshot_id → record (tree roots + cursor + name)

## Limitations

- **Data update atomicity** — snapshot-shared data uses COW; unshared live data can be overwritten in place. Ordered writes do not make those in-place updates atomic.
- **Reclamation and memory** — deletion is crash-resumable at committed inode boundaries. Large reclaim units reserve memory before mutation and can exceed the inline deferred-free queue, but memory exhaustion still prevents completion. Deferred frees are not a persistent journal; interrupted operations can leak space. See [failure semantics](docs/failure-semantics.md).
- **Physical hardware qualification** — no production use on actual Amiga hardware has been established. In particular, the 68080 build has not yet been qualified on Apollo hardware and AMMX is not used.

## Building

### Host tests (macOS / Linux)

```bash
make host-test
```

The default compiler is the platform `cc`. Run the same suite with GCC by
setting `HOST_CC=gcc` where GCC is installed.

### Amiga handler (cross-compile)

Requires the [AmigaPorts m68k-amigaos-gcc toolchain](https://github.com/AmigaPorts/m68k-amigaos-gcc):

```bash
brew install metaneutrons/tap/amiga-gcc   # macOS
make amiga
```

Output: `build/amiga/bfshandler`

### AmigaOS administration

Release archives publish one administration binary, `bfs`:

```text
bfs format BFS: Work
bfs snapshot create Work: before-upgrade
bfs snapshot list Work:
bfs snapshot dir Work: before-upgrade
bfs snapshot inspect Work: before-upgrade FILES
bfs snapshot delete Work: before-upgrade
bfs check Work:
bfs info Work:
```

`dir` and `inspect` show the snapshot root. Mounting a snapshot as a distinct
read-only volume is not part of this command and remains future work.

`make release` builds separate handlers for 68020, 68030, 68040, 68060 and Apollo
68080. The unsuffixed `bfshandler` is the 68020 build; `bfshandler.080` targets
Apollo without AMMX. The command-line utilities use the same explicit libnix
runtime as the handler. A successful build is not physical hardware qualification.

Release acceptance follows the [release-readiness plan](docs/plans/release-readiness.md).

### Linux / POSIX administration

`make tools` builds the canonical host command, `build/host/bfs`. Its filesystem
operations use the same core API as the Amiga handler; only path opening and
FUSE protocol handling are platform-specific.

```bash
truncate -s 2G work.bfs
build/host/bfs format work.bfs --label Work --block-size 4096
build/host/bfs check work.bfs
build/host/bfs check work.bfs --repair
build/host/bfs snapshot create work.bfs before-upgrade
build/host/bfs snapshot list work.bfs
build/host/bfs snapshot delete work.bfs before-upgrade
build/host/bfs info work.bfs
```

`format` and every write-capable administration operation accept only regular
image files. `check` opens the image read-only; `--repair` can reclaim only
unreachable blocks after a structurally clean scan. It does not attempt a
general corruption repair. The compatibility binaries `mkbfs` and `bfsfsck`
remain available with their established syntax and invoke the same code paths.

For a Linux mount, build the FUSE adapter as well:

```bash
make fuse
build/host/bfs mount work.bfs /mnt/bfs
build/host/bfs mount work.bfs /mnt/bfs-rw --read-write
```

The default mount is read-only. `bfs mount` starts the sibling `bfs-fuse`
adapter, which retains the single implementation of mount lifecycle, image
range (`--offset`, `--length`) and snapshot selection (`--snapshot` or
`--snapshot-id`). A writable mount is explicit and is refused for snapshots.

### Stress test binary

```bash
make amiga-stresstest
```

### Host tools

```bash
make tools
```

### Benchmark

```bash
make bench
```

## Testing

The host suites cover:

- **B+tree** — insert, split, delete, merge, scan, COW isolation, **compaction**
- **Free space** — alloc, free, coalesce, self-hosting, disk-full
- **Directory** — lookup, case-insensitive, international chars, scan
- **Extents** — single, fragmented, truncate, large files
- **File I/O** — read, write, seek, cross-block, truncate
- **Dir operations** — mkdir, rmdir, create, delete, rename
- **Filesystem** — format, mount, crash recovery, sync cycles, **ordered data**
- **Durability** — stale handles, backup SB protection, batch reclamation
- **Integration** — full workflows, persistence, multiple block sizes
- **Stress** — 2K files, disk-full recovery, random ops, deep dirs
- **Edge cases** — boundary conditions, overflow, corruption handling
- **Robustness** — concurrent-style ops, resource exhaustion
- **Hardware failure** — simulated I/O errors, partial writes
- **Crash injection** — bounded power-loss simulation across create, delete, write and sync cut points
- **Model checking** — six fixed PRNG seeds, 3,000 random operations and invariant checks after every operation
- **Real-world** — large directory workloads, fragmentation patterns
- **Hunt** — targeted regression tests
- **Snapshots** — create, delete, list, and inspect read-only snapshot metadata
- **Deferred-free queue** — headroom reserve, non-silent overflow latch, compaction mass-free, no-leak under delete-storm churn

### Emulator integration test

Full end-to-end test using FS-UAE with AROS:

```bash
make emulator-test
```

## Installation on Amiga

1. Copy `bfshandler` to `L:`:

   ```bash
   Copy bfshandler L:bfshandler
   ```

2. Add a Mountlist entry (e.g., `DEVS:DOSDrivers/BFS`):

   ```bash
   BFS:
       Handler   = L:bfshandler
       Stacksize = 16384
       Priority  = 5
       GlobVec   = -1
       Mount     = 1
   ```

3. Format the partition:

   ```bash
   bfs format BFS: Work
   ```

## License

[Mozilla Public License 2.0](LICENSE)
