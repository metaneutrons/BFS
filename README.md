# BFS - B+tree File System for AmigaOS / AROS

![CI](https://github.com/metaneutrons/bfs/actions/workflows/ci.yml/badge.svg)
[![License: MPL-2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](LICENSE)

> **⚠️ WARNING: Experimental software.** BFS has not yet been battle-tested.
> Always keep backups of your data and use at your own risk.

## Motivation

[PFS3](https://github.com/tonioni/pfs3aio) is the gold standard Amiga filesystem — fast, reliable, and battle-tested for over 30 years. But its 1990s architecture has hard limits:

- **O(n) directory scans** — linear search through linked blocks
- **Anode chains** — file extent lookup is O(n) in fragment count
- **No checksums** — silent corruption goes undetected
- **~1.6 TB limit** — 32-bit block numbers × 512-byte blocks

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
| Data consistency | None | Optional `data=ordered` mode |
| Snapshots | — | B+tree based (Read-only) |
| Defragmentation | Offline | **Online Compaction** |
| Max filename | 107 chars | 255 chars |
| Max volume size | ~1.6 TB | 16 TB (4K blocks) |
| Hard links | Yes | Yes |
| Soft links | Yes | Yes |
| File comments | Yes | Yes |
| Free space tracking | Bitmap | Self-hosting B+tree |
| Automated tests | — | **220 tests** + emulator integration |

## Architecture

```plain
┌────────────────────────────────────────────────────────┐
│                    AmigaOS Glue Layer                  │
│  handler.c (DOS packets)    amiga_bio.c    startup.s   │
├────────────────────────────────────────────────────────┤
│                  Portable Core (C99)                   │
│                                                        │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌──────────┐   │
│  │ btree.c │  │ alloc.c │  │  dir.c  │  │ extent.c │   │
│  │ unified │  │  free   │  │  dir    │  │  file    │   │
│  │ B+tree  │  │  space  │  │  ops    │  │ extents  │   │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬─────┘   │
│       └────────────┴────────────┴────────────┘         │
│                        │                               │
│  ┌─────────┐  ┌────────┴──┐  ┌──────────┐  ┌────────┐  │
│  │  fs.c   │  │   txn.c   │  │ crc32.c  │  │ file.c │  │
│  │ format  │  │ COW txns  │  │checksums │  │ I/O    │  │
│  │ mount   │  │ dual-sb   │  │          │  │        │  │
│  └─────────┘  └───────────┘  └──────────┘  └────────┘  │
└────────────────────────────────────────────────────────┘
```

The B+tree engine is shared across all metadata types, utilizing a **dynamic transaction tracking** architecture that ensures session-wide consistency and safe COW reclamation. It supports **online compaction** for metadata trees to maintain performance without downtime.

- **Directory tree** — (parent_id, hash, name) → inode
- **Extent tree** — file_block → (disk_block, length)
- **Free space tree** — block_nr → length
- **Inode tree** — inode_id → metadata

## Limitations

- **Data blocks are not COW'd** — metadata is always consistent; data consistency can be enforced using the optional `data=ordered` mode.
- **Very large snapshots** — creating or deleting a snapshot on an extremely large volume (or reclaiming a single multi-GB shared file) can exhaust the bounded deferred-free queue and fail safely with an out-of-space error rather than completing; a resumable reclaim is on the roadmap. Ordinary metadata operations reserve queue headroom up front and never overflow.
- **Needs real-world testing** — no production use on actual Amiga hardware yet.

## Building

### Host tests (macOS / Linux)

```bash
make host-test
```

### Amiga handler (cross-compile)

Requires [bebbo's m68k-amigaos-gcc](https://github.com/bebbo/amiga-gcc):

```bash
brew install metaneutrons/tap/amiga-gcc   # macOS
make amiga
```

Output: `build/amiga/bfshandler`

### Stress test binary

```bash
make amiga-stresstest
```

### fsck tool

```bash
make tools
```

### Benchmark

```bash
make bench
```

## Testing

220 host tests across 28 suites:

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
- **Crash injection** — power-loss simulation at every write point
- **Model checking** — property-based invariant verification (12,500 checks)
- **Real-world** — large directory workloads, fragmentation patterns
- **Hunt** — targeted regression tests
- **Snapshots** — create, delete, list, mount (read-only)
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
   Format DRIVE BFS: NAME "Work" NOICONS
   ```

## License

[Mozilla Public License 2.0](LICENSE)