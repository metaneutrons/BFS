# BFS ExAll result cache bound

A handler that serves repeated ExAll passes over an unchanged directory from a
fixed array in RAM takes 0.44 of the reference time for 1,000 entries ten times
and 0.41 for 400 entries ten times, on the emulated 68040. Against PFS3 that is
3.86 down to 1.71 times and 3.52 down to 1.47 times. The candidate's cache
belongs to the lock, so one of the ten passes fills it. The estimate for that
is 0.49 and 0.47. The first pass over 400 and 1,000 entries costs the same as
before (1.01 and 1.00), over 40 entries 1.12. Of the six criteria of the
[decision rule](../plans/bfs-exall-result-cache-v1.md), the two gain criteria,
the memory criterion and the validity of the runs are met. The first-listing
criterion and the no-regression criterion are missed by small margins that lie
within the spread of this host. By the rule as written the bound is therefore
not clean, and the decision is Fabian's.

## Prototype

Both handlers are uninstrumented 68040 release handlers (`make release`, `-O2
-m68040`, m68k-amigaos-gcc 6.5.0b), built from one scratch worktree with one
procedure.

- `reference` (`e2e274bb…`): `origin/main` at `b8f3e8a`. These are the same
  bytes as the `v0.3.0` handler. The toolchain rebuilt `v0.3.0` to `e2e274bb…`
  before this build, and only MorphOS and string-conversion changes lie between
  the two commits.
- `bound` (`be30c603…`, 1,224 bytes larger): the same tree with
  [`resultcache-handler.patch`](evidence/bfs-exall-result-cache-bound-2026-10-22/resultcache-handler.patch)
  (SHA-256 `87d8335c…`), kept in the evidence directory and not in `src/`.

The patch adds a counter to the handler that every packet raises which can
change the volume (the packets a snapshot refuses, `ACTION_END`,
`ACTION_INHIBIT`, `ACTION_DIE`, the 64-bit resize, and a media remount), and
one array of 1,024 entries, allocated at the first ExAll, that holds the
gathered entries with their 56-byte inode copies. A complete, unfiltered pass
that reads inodes records into the array. A later ExAll call over the same
directory takes its gather and its inodes from the array while the counter is
unchanged. It gathers with the callback the scan uses, so the pattern match and
the room check are the existing code. The fill (`exall_write_entry`) and the
lock's resume point are untouched. The array survives `UnLock`, so the first-pass
phase over a directory fills it and the ten-pass phase finds it full.

The patch is a measuring device. It omits what the candidate adds: comparing
tree stamps instead of a counter, allocating and capping memory per lock and
giving it up when memory is short, a real fill, and the lock-owned lifetime. It
overstates the gain.

### Checks before the run

The dev build (`-m68020`) of the bound passed the on-Amiga tests `exall_44`,
`exalldel_49`, `exallpack_50`, `exnext_37`, `exnextdel_48` and `exnextfresh_55`
in FS-UAE on the Mac. A build in which the cached names were deliberately cut
by one byte failed `exallpack_50`, which shows that the test reaches the cached
path. After a complete pass it alternates ExAll batches from `eac_LastKey` with
ExNext on the same lock. The benchmark guest checks the names, types and counts
of each listing, not sizes or dates, and every fixture file is empty. No test
lists a directory, changes a file and lists again with a valid cache in
between.

## Run

The [scope](evidence/bfs-exall-result-cache-bound-2026-10-22/PILOT_SCOPE.md)
was fixed before the run, and the plan with its decision rule before the first
build. Sixteen fresh `compare` starts on Cachy, eight rounds of both handlers,
BFS first in odd rounds and PFS3 first in even rounds, `reference` first in odd
rounds and `bound` first in even rounds. Guest, PFS3, ROM, formatter and RDB
Buffers = 30 are those of the v0.3.0 series. All 16 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 09:48:07–09:57:37 UTC
on 22 October 2026. The runner container and Coffin were stopped by the launch
script and restored with status 0. Afterwards both run, no CI worker process
exists and no benchmark emulator is left (`services-restored.log`). Nothing is
retained or reverted.

## Results

BFS / PFS3, median of eight starts, and the median of the eight paired ratios
`bound` over `reference` in the same round, with the range of those ratios.
Cachy's spread is large: the single ratios of a phase differ by a factor of two
or more in seven phases and by almost five in one. Medians decide.

**ExAll**

| Phase | reference | bound | bound / reference | range |
| --- | ---: | ---: | ---: | ---: |
| ExAll 40, first pass | 2.89 | 3.23 | 1.116 | 0.78–1.50 |
| ExAll 40 × 10 | 3.14 | 1.30 | 0.420 | 0.39–0.70 |
| ExAll 40 × 10 in `perf` (`LIST_EXALL_400`) | 2.92 | 1.55 | 0.526 | 0.32–0.77 |
| ExAll 400, first pass | 5.52 | 5.87 | 1.008 | 0.75–1.37 |
| ExAll 400 × 10 | 3.52 | 1.47 | 0.415 | 0.37–0.53 |
| ExAll 1,000, first pass | 6.23 | 6.05 | 0.998 | 0.57–1.14 |
| ExAll 1,000 × 10 | 3.86 | 1.71 | 0.437 | 0.33–0.48 |

**ExNext** (the bound does not change it)

| Phase | reference | bound | bound / reference | range |
| --- | ---: | ---: | ---: | ---: |
| ExNext 40, first pass | 1.96 | 2.12 | 1.020 | 0.89–4.41 |
| ExNext 40 × 10 | 1.62 | 1.72 | 1.038 | 0.74–1.95 |
| ExNext 40 × 10 in `perf` (`LIST_EXNEXT_400`) | 1.55 | 1.63 | 1.053 | 0.73–1.52 |
| ExNext 400, first pass | 1.97 | 2.07 | 1.037 | 0.96–3.47 |
| ExNext 400 × 10 | 1.53 | 1.56 | 1.028 | 0.95–1.11 |
| ExNext 1,000, first pass | 2.05 | 2.07 | 1.008 | 0.83–1.05 |
| ExNext 1,000 × 10 | 1.54 | 1.60 | 1.009 | 0.92–1.28 |

**Phases without listings** (unchanged)

| Phase | reference | bound | bound / reference | range |
| --- | ---: | ---: | ---: | ---: |
| Lookup 400 | 0.89 | 0.91 | 1.026 | 0.96–1.65 |
| Sequential write 8 MiB | 0.73 | 0.77 | 1.048 | 0.91–1.60 |
| Sequential read 8 MiB | 1.01 | 1.02 | 1.007 | 0.81–1.43 |
| Append read 1,280 KiB | 1.08 | 1.08 | 1.007 | 0.84–1.09 |
| Small read 40 | 1.26 | 1.30 | 1.006 | 0.77–1.59 |
| Small create 40 | 1.75 | 1.78 | 0.986 | 0.84–1.51 |
| Small delete 40 | 1.72 | 1.76 | 1.014 | 0.98–1.51 |
| Append 4 KiB × 256 | 1.58 | 1.59 | 1.042 | 0.90–1.69 |
| Append 1 KiB × 256 | 1.84 | 1.89 | 1.014 | 0.95–1.15 |

The reference reproduces the figures of the v0.3.0 report within 2 %: 3.52
against 3.55 for 400 × 10, 6.23 against 6.24 for the first pass over 1,000
entries and 3.86 against 3.93 for 1,000 × 10.

Geometric mean of the paired medians: 1.021 over the sixteen phases the bound
does not change (1.016 over the nine without listings, 1.028 over the seven
ExNext phases) and 0.887 over all 23. Geometric mean of the BFS / PFS3 medians:
1.947 for `reference`, 1.741 for `bound`.

The `ExAll 40 × 10 in perf` phase happens to behave like a lock-owned cache:
the directory has not been listed with ExAll before, the first of ten passes
fills the cache and nine hit (it is also the first ExAll of the run, so it
allocates the array). It measured 0.526. The estimate `0.1 · f + 0.9 · r` of
the plan gives 0.49 for 40 entries (`f` = 1.116, `r` = 0.420), so the estimates
for the other sizes may be slightly optimistic.

### Cost per entry

Median time per listed entry in microseconds. PFS3 is the median of all 16
starts.

| Phase | reference | bound | PFS3 |
| --- | ---: | ---: | ---: |
| ExAll 1,000 × 10 | 19.5 | 8.6 | 5.0 |
| ExAll 1,000, first pass | 39.8 | 40.2 | 6.4 |
| ExAll 400 × 10 | 18.4 | 7.5 | 5.1 |
| ExAll 400, first pass | 40.3 | 41.5 | 7.3 |
| ExAll 40 × 10 | 20.0 | 8.4 | 6.4 |
| ExAll 40, first pass | 43.2 | 46.4 | 15.2 |

With the scan and the inode reads gone, a repeated pass costs 8.6 µs per entry
instead of 19.5. The handler that read no inode ([bound of 10
October](bfs-listing-metadata-bound-2026-10-10.md)) kept 11.8 µs. The 8.6 µs
that remain are the gather copy, the fill and the DOS call path, and PFS3 needs
5.0 for the whole listing. The remainder was not profiled here.

### Memory

Sizes as the m68k compiler lays them out (`memory-probe.txt`). The m68k aligns
32-bit integers to 2 bytes. One entry of the bound's array takes 74 bytes plus
the name, and the array 92,196 bytes (1,024 entries, a 16 KiB name area and a
small header). The candidate needs only the four listing fields: its record
without the name takes 30 bytes (`candidate-record.c`), so 1,000 entries with
names of the AROS median length of 12 bytes take 42,000 bytes, and with the
benchmark's 4-byte names 34,000. The ExAll batch that every call allocates
takes 21,912 bytes in the bound, 4 less in the reference.

## Decision rule

The rule of the plan, evaluated by `summarize.py` on the medians above.

| Criterion | Limit | Measured | |
| --- | --- | --- | --- |
| 1. Gain at 1,000 entries: `r`, lock-owned estimate | ≤ 0.50, ≤ 0.55 | 0.437, 0.493 | met |
| 2. Gain at 400 entries: `r`, lock-owned estimate | ≤ 0.55, ≤ 0.60 | 0.415, 0.474 | met |
| 3. First pass over 40, 400, 1,000 entries | each ≤ 1.05 | 1.116, 1.008, 0.998 | not met |
| 4. The 16 unchanged phases: each, geometric mean | ≤ 1.05, ≤ 1.02 | worst 1.053, 1.021 | not met |
| 5. Candidate record, 1,000 entries with 12-byte names | ≤ 32 bytes, < 64 KiB | 30 bytes, 42,000 bytes | met |
| 6. All 16 starts pass the verifier | 16 | 16 | met |

By the plan, if only criteria 3, 4 or 5 fail, the bound is not clean, this
report says why, and Fabian decides whether to repeat or change the design.

**Criterion 3.** Only the 40-entry first pass misses, at 1.116 with ratios of
0.78 to 1.50 over the eight rounds. That phase lasts 1.7–2.6 ms and its
recorded times advance in steps of 64 µs, about 4 % of the phase. The larger
first passes show what recording costs: 1.008 and 0.998, between 0.4 and 1.2 µs
per entry, which would be 1–3 % of the 40-entry phase. The measured excess is
larger than that, and this series cannot say whether the rest is noise. The
bound's recording is a copy into a static array. The candidate's fill allocates
and can fail, so its first-listing cost will exceed the bound's.

**Criterion 4.** One phase exceeds 1.05, ExNext 40 × 10 in `perf` at 1.053 with
ratios of 0.73 to 1.52, and the geometric mean misses by 0.1 point. The bound
reaches these phases only through one added function call per packet and a
handler that is 1.2 KB larger. That is expected to cost well under 1 %, which
was not measured. Fifteen of the sixteen medians lie above 1.00, so a real cost
of 1–2 % cannot be excluded. It also cannot be shown: the geometric mean of the
sixteen phases of one round ranges from 0.92 to 1.26 over the eight rounds. The
limits were fixed in the plan before the run, after the series of 10 October
had shown unchanged phases at 1.049 and 1.043. The rule leaves little room
against noise of this size, and the miss is not a finding about the bound's
code. A series of `reference` against itself in this schedule would show how
much bias this pairing has on Cachy and whether limits of 1.05 and 1.02 can be
met at all.

## What the bound does not show

- **Real invalidation.** The counter costs an increment per packet. The
  candidate compares the root, generation and pending-inode stamps of both
  trees, as the ExNext read-ahead does, and every write on the volume ends the
  validity, not only a write in the listed directory. How often a cache would
  survive between listings in real use is not measured. None of the tests run
  covers a valid cache that a change must invalidate, and the fixtures hold
  empty files, so cached sizes and dates were never compared with changed ones.
- **Real listing patterns.** Ten ExAll passes on one lock over a directory
  nothing writes to are the most favourable case, and only this case gains. The
  first listing of a directory, a listing after any write and a listing through
  a new lock gain nothing and pay for recording. Programs that list a directory
  once, as `Dir`, a file requester or a Workbench drawer does, never hit. A
  cache owned by the lock hits only on a second pass over the same lock.
- **The candidate's costs.** Allocation, the cap, release under memory
  shortage, a failed allocation, entries with comments (each listing still
  reads their comment record), and directories above 1,024 entries are not part
  of the bound.
- **Real hardware.** Figures are relative to PFS3 on FS-UAE's emulated 68040 at
  its maximum speed on a virtual host. Absolute microseconds do not carry over
  to a real 68040 or 68060.

## Consequence

In the best case, repeated listings of an unchanged directory, a result cache
takes about 56 % off ExAll (0.44 of the time) and moves ExAll from 3.9 to 1.7
times PFS3. That goes further than the 0.59 that the listing-fields bound of 10
October reached, which was a ceiling too. Even then the repeated ExAll phases
stay at 1.3–1.7 times PFS3. Reading the decision rule as written, the gain and
memory criteria pass clearly and the two criteria about extra cost are missed
by margins that the spread of Cachy does not let this series resolve. What the
experiment cannot answer is how often the best case occurs, and that is the
strongest objection of the plan. Whether a cache that serves only repeated
passes on one lock is worth the invalidation code, the memory and the added
cost on every first listing is Fabian's decision. To settle the two missed
criteria before that decision, the `reference` against `reference` series is
the way. A real implementation would start with the second plan the rule names.
