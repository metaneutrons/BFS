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

## Kickstart Runs

CI uses only the AROS ROM. The same suites run locally on a lawfully obtained
Kickstart ROM with Workbench commands from the asset directory below. The
integration suite uses `ci-local.sh`; the compatibility, reset and baseline
refusal checks take `--kickstart` and `--workbench`:

```bash
xvfb-run -a ./emulator-test/ci-local.sh 1800
python3 emulator-test/compatibility-test.py \
    --kickstart emulator-test/.assets/A1200.47.102.rom --workbench emulator-test/.assets
python3 emulator-test/reset-test.py \
    --kickstart emulator-test/.assets/A1200.47.102.rom --workbench emulator-test/.assets
```

Under Kickstart the reset test also requires the machine to boot again after
the reset; the AROS ROM halts at that point, also without BFS.

## External Benchmark Assets

The optional BFS/PFS3 comparison uses third-party or licensed Amiga files that must not be
committed. By default, local copies are read from:

```text
emulator-test/.assets/A1200.47.102.rom
emulator-test/.assets/C/
emulator-test/.assets/L/
emulator-test/.assets/Libs/
emulator-test/.cache/pfs3aio
```

The locations can be overridden with `BFS_AMIGA_ASSETS_DIR`, `BFS_ROM_FILE`, and
`BFS_PFS3_HANDLER`.

```bash
make amiga tools amiga-fs-compare-bench
export BFS_BENCH_RUN_DIR="$(pwd)/build/benchmark/run-1"
export BFS_BENCH_ORDER=bfs-first
./emulator-test/build-bench-image.sh
./emulator-test/run-bench.sh 600
```

PFS3, AmigaOS ROMs, and Workbench files must come from lawfully obtained local copies. Any
redistributable dependency added to CI must be downloaded by immutable identity and verified
before execution.

Use `pfs3-first` for `BFS_BENCH_ORDER` in a second, fresh run directory to expose order effects.
Each run keeps its HDFs, FS-UAE configuration, format output, machine information, checked
workload outputs, and completion marker below `BFS_BENCH_RUN_DIR`; the scripts refuse to overwrite an
existing result. Both HDFs have equal 255.5 MiB partitions on the same virtual device type.
The workload measures 40 × 1 KiB file creation, 400 locks, 40 checked small reads,
ten ExNext and ten ExAll listings of the 40 files (each must return every file once),
8 MiB sequential write/read with byte verification, and 40 deletes. It times each phase
using `timer.device`; write phases include `Flush` and `Close`. The run is valid only when
both volumes were mounted, both complete TSVs passed verification, and the guest wrote its
completion marker. Run `emulator-test/verify-bench-results.sh "$BFS_BENCH_RUN_DIR"` to
recheck retained outputs. The result is an emulated AmigaOS comparison, not a
native-hardware throughput claim.

Schema 2 of `FS_COMPARE_BENCH` and `FS_DURABLE_COMPARE` added the two listing
phases; the verifier still accepts schema 1 outputs, which lack them. The deep
profile keeps its phases and does not list.

Set `BFS_BENCH_MODE=durable-compare` to run the same workload with an
`ACTION_FLUSH` to the volume at the end of the create, write and delete phases,
inside the timed region. Each filesystem then pays for its own commit within
the phase that caused it; PFS3, which commits from a timer, otherwise defers
that work past the measurement or into a later phase. The outputs are
`bfs.durable.tsv` and `pfs3.durable.tsv` with the schema header
`FS_DURABLE_COMPARE 2`; validate them with
`emulator-test/verify-bench-results.sh "$BFS_BENCH_RUN_DIR" durable-compare`.

For a deterministic, emulator-free view of where the core spends its work, run
`make core-workload-profile` (requires `valgrind`). It replays the compare
phases through the core API with the handler's calls and cache settings and
prints the instruction count of each phase, once with a commit after every
close and delete (`sync`) and once with one commit per mutating phase
(`group`). `tools/core-workload-profile.sh MODE N` also lists the N most
expensive functions of each phase. The counts are x86 host instructions; they
show relative cost and regressions, not 68k cycles, because CRC and memory
copies use assembler routines on the Amiga.

For diagnostic phase profiling, build `make amiga-fs-profile-bench`, set
`BFS_BENCH_MODE=profile`, and use a new `BFS_BENCH_RUN_DIR` with the same builder and
runner. This measures fresh-file and same-file overwrite calls separately from
`Flush` and `Close`, then verifies an 8 MiB read. The output is
`bfs.profile.tsv` and `pfs3.profile.tsv`; validate it with
`emulator-test/verify-bench-results.sh "$BFS_BENCH_RUN_DIR" profile`. These numbers
separate AmigaDOS API phases, not internal block-I/O or crash durability.

For BFS-only internal call counts, build `make amiga-perf-probe-handler
amiga-fs-profile-bench`, set `BFS_BENCH_MODE=internal` and
`BFS_BENCH_HANDLER_FILE="$(pwd)/build/amiga/bfshandler-probe"`, and choose another
fresh run directory. The debug handler exposes private reset/read packets and counts
underlying device reads, writes, updates, free-space allocations and extent maps.
The normal `make amiga` handler is unaffected; never ship the debug handler as a
release artifact. Validate retained output with
`emulator-test/verify-bench-results.sh "$BFS_BENCH_RUN_DIR" internal`.

For paired API timings plus BFS-only I/O and timer counters, build
`make amiga-perf-probe-handler amiga-fs-profile-bench`, set
`BFS_BENCH_MODE=deep`, and use another fresh run directory. The builder selects
`build/amiga/bfshandler-probe` for BFS by default; set `BFS_BENCH_HANDLER_FILE` to
override it. Deep mode measures fresh write, same-file overwrite, and checked read on
both BFS and PFS3. It reports device and data I/O counts, metadata and transaction
counts, free-space and extent-map counts, elapsed timer ticks, and clock frequency for
BFS only. Validate retained output with
`emulator-test/verify-bench-results.sh "$BFS_BENCH_RUN_DIR" deep`.

For the six-phase BFS/PFS3 comparison with per-phase BFS probe counters, build
`make amiga-perf-probe-handler amiga-fs-compare-bench`, set
`BFS_BENCH_MODE=deep-compare`, and use another fresh run directory. The builder selects
`build/amiga/bfshandler-probe` for BFS by default; set `BFS_BENCH_HANDLER_FILE` to
override it. Both filesystems run the existing checked compare workload; BFS also
reports I/O counts and ticks after each phase. Validate retained output with
`emulator-test/verify-bench-results.sh "$BFS_BENCH_RUN_DIR" deep-compare`.
The v4 schema also attributes B-tree writes to the live free-space, directory,
inode and refcount trees (remaining writes are reported as other), counts all
B-tree CRC calls, and times every 64th CRC call. `CLOCK_PAIR_TICKS` is the
integer mean of 256 empty timer pairs calibrated at each phase reset. Sampled
CRC ticks are not a full CPU profile: extrapolate the mean sample duration to
all calls, and treat the result as an estimate. Only the disposable probe
handler contains these counters.

## Troubleshooting

- **AROS ROM missing:** run `make emulator-setup` and retry.
- **No structured result:** inspect `fs-uae.log` in the run's printed evidence directory
  under `build/emulator/run.*` for a boot or handler failure.
- **Handler not mounted:** inspect `build/emulator/ci-test.fs-uae` and its
  `hard_drive_1_file_system` value.
