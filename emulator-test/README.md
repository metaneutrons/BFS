# BFS Emulator Integration Test

Tests the BFS handler inside a real AmigaOS-compatible environment
using FS-UAE with AROS m68k.

## Prerequisites

- `fs-uae` (install: `brew install fs-uae`)
- `m68k-amigaos-gcc` (install: `brew install metaneutrons/tap/amiga-gcc`)
- Internet connection (first run downloads AROS ~50MB)

## Usage

```bash
# Interactive (window visible — for development)
make emulator-test

# Headless (for CI)
make emulator-test-ci

# Setup only (download AROS, create config, don't run)
./emulator-test/run.sh --setup-only
```

## What it tests

The test script runs INSIDE the emulated Amiga and exercises:

1. **Format** — `Format DRIVE BFSTEST: NAME "TestVol"`
2. **MakeDir** — nested directory creation
3. **File I/O** — write files via `Echo >`, read via `Type`
4. **Copy** — binary file copy
5. **Delete** — file deletion
6. **Rename** — file rename
7. **Dir/List** — directory listing (tests ExAll/ExNext)
8. **Large files** — copy a binary and verify size

## CI Integration

```yaml
# GitHub Actions example
- name: Install FS-UAE
  run: brew install fs-uae

- name: Build BFS handler
  run: make amiga

- name: Run emulator test
  run: make emulator-test-ci
  timeout-minutes: 5
```

The integration job downloads its encrypted ROM and Workbench bundle at run
time. Those assets are not stored in this repository.

## External benchmark assets

The BFS/PFS3 comparison uses third-party or licensed Amiga files that must not
be committed to this repository. By default, local copies are read from:

```text
emulator-test/.assets/A1200.47.102.rom
emulator-test/.assets/C/
emulator-test/.assets/L/
emulator-test/.assets/Libs/
emulator-test/.cache/DiskSpeed
emulator-test/.cache/pfs3aio
```

The `.assets/` and `.cache/` directories are ignored by Git. Their locations
can be overridden with `BFS_AMIGA_ASSETS_DIR`, `BFS_ROM_FILE`,
`BFS_DISKSPEED`, and `BFS_PFS3_HANDLER`.

```bash
make amiga build/host/mkbfs
./emulator-test/build-bench-image.sh
./emulator-test/run-bench.sh
```

DiskSpeed, AmigaOS ROMs, and Workbench files must come from lawfully obtained
local copies. If a CI job later needs another redistributable dependency, it
must download a pinned artifact at run time and verify its checksum.

## How it works

1. Creates a blank 16MB HDF image (the BFS test partition)
2. Configures FS-UAE with AROS as the system drive
3. Points `hard_drive_1_file_system` to our `bfshandler` binary
4. AROS boots, mounts the BFS partition, runs the test script
5. Test script writes results to `SYS:test-result.txt`
6. Host script checks the result after FS-UAE exits

## Files

```
emulator-test/
├── run.sh              — main test runner
├── README.md           — this file
├── aros/               — AROS m68k files (downloaded on first run)
├── config/             — FS-UAE configuration
├── scripts/            — Amiga test scripts
└── bfstest.hdf        — blank test partition (created by run.sh)
```

## Troubleshooting

- **"kickstart_file not found"**: AROS ROM not downloaded. Run `./run.sh --setup-only`
- **Emulator hangs**: Handler crashed. Check with `--ci` mode (auto-timeout 120s)
- **"Format failed"**: Handler not loaded. Check `hard_drive_1_file_system` path
