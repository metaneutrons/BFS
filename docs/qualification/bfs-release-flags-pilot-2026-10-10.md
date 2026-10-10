# BFS release flags pilot

The release handlers were slower than the measured build because they were
optimized for size (`-Os`). On the 68040, `-O2` is 10.4 % faster in the
geometric mean of all phases, and no phase is slower. Under the rule fixed
before the run, the release builds of every CPU switch to `-O2`. The 68040
CPU flag itself is 3.1 % faster than the 68020 flag at `-O2`.

## Handlers

Built on the Mac with m68k-amigaos-gcc 6.5.0b from commit `5136708` plus the
retained big-endian load change. Apart from the two flags, the release flags
(`AMIGA_BASE_FLAGS`) and the `make amiga` flags are identical.

| Handler | Flags | Bytes |
| --- | --- | ---: |
| `o2-020` | `-O2 -m68020` (`make amiga`) | 171,240 |
| `o2-040` | `-O2 -m68040` | 173,548 |
| `os-040` | `-Os -m68040` (`make release` before this change) | 140,864 |

`o2-020` reproduces the retained candidate of the production baseline pilot
byte for byte (`ea37a89d…`). With `-O2`, the release handlers of all five
CPUs grow from 136,208–140,864 to 167,800–173,548 bytes.

## Run

The [scope](evidence/bfs-release-flags-pilot-2026-10-10/PILOT_SCOPE.md) was
fixed before the run: 24 fresh `compare` starts on Cachy, eight rounds of all
three handlers, BFS first in odd rounds and PFS3 first in even rounds, handler
order rotating per round; guest, PFS3, ROM, formatter and RDB Buffers = 30 of
the production baseline pilot. All 24 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 11:09:33–11:23:58 UTC on
10 October 2026; runner and Coffin were restored with status 0.

| Rule | Result |
| --- | --- |
| 1. All 24 starts pass the strict verifier | passed |
| 2. Geometric mean of the per-phase median ratio `o2-040` / `os-040` ≤ 0.97 | 0.896 |
| 3. No phase with a median ratio ≥ 1.10 | none (highest 0.994) |

## Results

Median paired ratios per phase, and BFS / PFS3 (median of eight starts):

| Phase | `o2-040` / `os-040` | `o2-040` / `o2-020` | BFS / PFS3 `os-040` | `o2-040` |
| --- | ---: | ---: | ---: | ---: |
| Small create 40 | 0.854 | 0.926 | 1.91 | 1.64 |
| Small read 40 | 0.840 | 0.916 | 1.40 | 1.15 |
| Small delete 40 | 0.865 | 1.000 | 2.02 | 1.72 |
| Lookup 400 | 0.933 | 0.907 | 0.95 | 0.88 |
| Sequential write 8 MiB | 0.900 | 0.964 | 1.00 | 0.88 |
| Append 4 KiB × 256 | 0.933 | 0.962 | 2.00 | 2.04 |
| Append 1 KiB × 256 | 0.878 | 0.976 | 2.68 | 2.40 |
| ExNext 1,000 × 10 | 0.887 | 0.991 | 2.82 | 2.55 |
| ExAll 1,000, first pass | 0.913 | 0.982 | 6.53 | 6.03 |
| ExAll 1,000 × 10 | 0.889 | 0.960 | 4.19 | 3.70 |

Every phase of `o2-040` / `os-040` has a median below 1 (0.840–0.994); the
full table is in `summary.json`. Single paired ratios scatter in some phases
(append read 0.51–1.40, ExAll 40 first pass 0.36–1.11).

## Consequence

`AMIGA_BASE_FLAGS` uses `-O2`; `tools/release/create-archives.sh`,
`tools/release/release_integrity.py` and their test record the new flag. The
release handlers grow by about 32 KB each. Only the 68040 build was
measured; the other CPUs take the same optimization level unmeasured.
