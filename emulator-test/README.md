# BFS Emulator Integration Test

Tests the BFS handler inside an AmigaOS-compatible environment using FS-UAE and a minimal AROS
m68k runtime.

## Prerequisites

- `fs-uae` (macOS: `brew install fs-uae`)
- `m68k-amigaos-gcc` (macOS: `brew install metaneutrons/tap/amiga-gcc`)
- Internet access for the first checksum-verified AROS ROM download

## Usage

```bash
# Build and run the full integration suite
make emulator-test

# Download and verify the AROS ROMs without running tests
make emulator-setup
```

`make emulator-test-ci` is an alias for the same headless, self-verifying test path used in CI.
Generated ROMs, disk images, configuration, and the minimal system directory live under `build/`
and are ignored by Git.

## What It Tests

The on-Amiga `bfs-test` binary exercises file and directory operations, metadata, hard and soft
links, remount durability, large and sparse files, disk-full behavior, snapshots, fragmentation,
rename and delete edge cases, and repeated rewrite patterns. The host runner accepts the run only
when the structured summary is complete, at least one test ran, every test passed, and zero tests
failed.

## CI Assets

The integration job downloads two freely redistributable AROS ROM images from an immutable
upstream commit and verifies pinned SHA-256 digests. It creates a minimal system drive from the
freshly built BFS handler and test binary. No Kickstart, Workbench, downloaded tool, ROM, or disk
image is stored in this repository.

The flow is:

1. Download and verify the AROS ROM and extended ROM.
2. Create and format a blank 32MB BFS HDF image.
3. Create a minimal system drive containing `bfshandler` and `bfs-test`.
4. Boot AROS and mount the HDF through the freshly built handler.
5. Run the complete on-Amiga integrity suite.
6. Require a valid structured summary with zero failures.

## External Benchmark Assets

The optional BFS/PFS3 comparison uses third-party or licensed Amiga files that must not be
committed. By default, local copies are read from:

```text
emulator-test/.assets/A1200.47.102.rom
emulator-test/.assets/C/
emulator-test/.assets/L/
emulator-test/.assets/Libs/
emulator-test/.cache/DiskSpeed
emulator-test/.cache/pfs3aio
```

The locations can be overridden with `BFS_AMIGA_ASSETS_DIR`, `BFS_ROM_FILE`, `BFS_DISKSPEED`, and
`BFS_PFS3_HANDLER`.

```bash
make amiga build/host/bfs
./emulator-test/build-bench-image.sh
./emulator-test/run-bench.sh
```

DiskSpeed, AmigaOS ROMs, and Workbench files must come from lawfully obtained local copies. Any
redistributable dependency added to CI must be downloaded by immutable identity and verified
before execution.

## Troubleshooting

- **AROS ROM missing:** run `make emulator-setup` and retry.
- **No structured result:** inspect `fs-uae.log` in the run's printed evidence directory
  under `build/emulator/run.*` for a boot or handler failure.
- **Handler not mounted:** inspect `build/emulator/ci-test.fs-uae` and its
  `hard_drive_1_file_system` value.
