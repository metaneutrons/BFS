# BFS production baseline and big-endian load pilot

The uninstrumented handler of the compact directory layout is at parity with
PFS3 for lookups and large sequential transfers, 1.4–1.8 times PFS3 for small
files, 2.2–2.5 times for appends, 2.4–3.0 times for ExNext and 3.2–6.1 times
for ExAll. Loading big-endian words through aliasing word types instead of
`memcpy` on m68k is retained under the rule fixed before the run. The release
build for the 68040 (`-Os`) is slower than the measured `-O2` build in every
phase.

## Handlers

Three uninstrumented production handlers, built on the Mac with
m68k-amigaos-gcc 6.5.0b:

- `base`: commit `5136708` on `feat/compact-directory-records`, `make amiga`
  (`-O2 -m68020`), `c6326fc1…`.
- `candidate`: `base` plus one change. `bfs_load_be16/32/64` and
  `bfs_store_be16/32/64` in `include/bfs_types.h` use aliasing word types on
  m68k; this compiler copied every `memcpy`'d word through a stack slot and
  loaded it again. `all_zero` in `src/core/dir.c` uses the loader. Hosts and
  AROS keep `memcpy`. `dir_key_compare` falls from 132 to 54 bytes of code; the
  handler from 172,656 to 171,240 bytes. `ea37a89d…`.
- `release040`: commit `5136708` with the flags of `make release` for the
  68040 (`-Os -m68040`), 141,672 bytes, `25c68982…`.

The candidate passed `make host-test` and the FS-UAE integration suite on
Cachy (52 of 52) before the scope was fixed.

## Run

The [scope](evidence/bfs-production-codegen-pilot-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: 24 fresh `compare` starts on Cachy, eight rounds of
all three handlers, BFS first in odd rounds and PFS3 first in even rounds,
handler order rotating per round; guest, PFS3, ROM, formatter and RDB
Buffers = 30 of the listing cache and inode batch pilot. All 24 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 09:59:30–10:13:56 UTC
on 10 October 2026; runner and Coffin were restored with status 0.

## Production ratios

BFS / PFS3, median of eight starts per handler (BFS first and PFS3 first: four
each):

| Phase | base | BFS first | PFS3 first | candidate | release040 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lookup 400 | 0.92 | 0.93 | 0.91 | 0.90 | 1.01 |
| Sequential write 8 MiB | 0.95 | 0.94 | 0.95 | 0.88 | 0.99 |
| Sequential read 8 MiB | 0.99 | 0.97 | 0.99 | 1.04 | 1.02 |
| Append read 1,280 KiB | 1.12 | 1.11 | 1.12 | 1.12 | 1.12 |
| Small read 40 | 1.37 | 1.45 | 1.21 | 1.27 | 1.51 |
| Small create 40 | 1.78 | 1.77 | 1.78 | 1.78 | 2.03 |
| Small delete 40 | 1.78 | 1.82 | 1.78 | 1.75 | 2.12 |
| Append 4 KiB × 256 | 2.21 | 2.24 | 2.16 | 2.17 | 2.32 |
| Append 1 KiB × 256 | 2.54 | 2.51 | 2.54 | 2.50 | 2.76 |
| ExNext 400, first pass | 3.01 | 3.01 | 3.01 | 2.93 | 3.43 |
| ExNext 400 × 10 | 2.60 | 2.61 | 2.60 | 2.61 | 3.11 |
| ExNext 1,000, first pass | 3.01 | 2.98 | 3.01 | 2.97 | 3.45 |
| ExNext 1,000 × 10 | 2.58 | 2.63 | 2.54 | 2.59 | 3.05 |
| ExAll 40 × 10 | 3.57 | 3.63 | 3.48 | 3.36 | 3.90 |
| ExAll 400, first pass | 5.85 | 5.68 | 5.85 | 5.10 | 6.73 |
| ExAll 400 × 10 | 3.93 | 4.14 | 3.89 | 3.70 | 4.45 |
| ExAll 1,000, first pass | 6.08 | 5.83 | 6.47 | 6.47 | 7.01 |
| ExAll 1,000 × 10 | 4.04 | 4.09 | 3.99 | 3.84 | 4.53 |

The full table, every start and the remaining phases are in `summary.json`.
Single starts scatter widely in some phases; for example ExAll 400, first
pass, of `base` ranges from 4.84 to 10.48. The probe build of the listing
cache and inode batch pilot measured ExAll 1,000 × 10 at 5.5 and 5.9 and small
create at 2.3 and 2.1, 20–45 % above these medians; that difference includes
the probe's observer cost and the spread between separate runs.

## Big-endian loads

| Rule | Result |
| --- | --- |
| 1. All 24 starts pass the strict verifier | passed |
| 2. Geometric mean of the per-phase median paired ratio ≤ 1.02 | 0.972 |
| 3. No phase with a median paired ratio ≥ 1.10 | none (highest 1.014) |

Twenty of 23 phases have a median below 1; the listing phases gain most, ExAll
1,000 × 10 by 7.4 % (paired ratios 0.88–0.99). Single paired ratios range from
0.58 to 2.27, so the size of the gain per phase is uncertain; its direction
agrees with the removed instructions. The change is retained.

## Release build

`release040` over `base`, median paired ratio per phase: 1.003 (append read)
to 1.194 (ExNext 400 × 10), geometric mean 1.118. No phase is faster. The
release build differs in optimization level and CPU flag; this run does not
separate the two. Users of the 68040 release currently run about 12 % slower
than the handler every pilot measured.

## Consequence

The big-endian load change stays. The production ratios replace the probe
ratios as the reference for the distance to PFS3. Listings remain the largest
gap, followed by appends and small-file operations. Whether releases should be
built with `-O2` is Fabian's decision; a run with `-O2 -m68040` would separate
the two flags first.
