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


## Addendum of 22 October 2026: `reference` against `reference`

The report above says that criteria 3 and 4 are narrowly missed and that a
series of `reference` against itself would show how much bias the pairing has
on Cachy. Fabian ordered that series. The text and the numbers above, and the
plan's decision rule, are unchanged.

### Series

The
[scope](evidence/bfs-exall-result-cache-bound-2026-10-22/null-series/PILOT_SCOPE.md)
was fixed and committed before the first start. One handler, the `reference`
handler (`e2e274bb…`), runs under two names, `reference-a` and `reference-b`,
which are byte-identical copies of one file (the three SHA-256 are equal,
`inputs.sha256`). Schedule, guest, PFS3, ROM, formatter, RDB Buffers = 30,
source tree, run script and verifier are those of the main series: sixteen
fresh `compare` starts in eight rounds, BFS first in odd rounds and PFS3 first
in even rounds, `reference-a` first in odd rounds and `reference-b` first in
even rounds. All 16 starts pass `emulator-test/verify-bench-results.sh`. The
series ran 12:25:20–12:34:45 UTC. Runner container and Coffin were stopped by
the launch script and restored with status 0 at 12:34:48 UTC. A second check at
14:24 UTC finds both running and no CI worker process, benchmark emulator, unit
or watcher process of the session left (`services-restored.log`). Every ratio
below belongs to identical code, so any difference from 1 is the host's bias
and spread, not a handler.

### Results

BFS / PFS3 of each name (median of eight starts), the median of the eight
paired ratios `reference-b` over `reference-a` in the same round with their
range, and for comparison the paired median of the main series (`bound` over
`reference`).

**ExAll**

| Phase | a | b | b / a | range | main series: bound / reference |
| --- | ---: | ---: | ---: | ---: | ---: |
| ExAll 40, first pass | 3.11 | 3.12 | 0.983 | 0.89–1.07 | 1.116 |
| ExAll 40 × 10 | 3.16 | 3.09 | 0.992 | 0.98–1.03 | 0.420 |
| ExAll 40 × 10 in `perf` | 2.96 | 2.94 | 0.991 | 0.74–1.03 | 0.526 |
| ExAll 400, first pass | 5.35 | 5.53 | 1.000 | 0.66–1.05 | 1.008 |
| ExAll 400 × 10 | 3.60 | 3.60 | 1.002 | 0.97–1.15 | 0.415 |
| ExAll 1,000, first pass | 6.20 | 5.65 | 0.952 | 0.48–0.99 | 0.998 |
| ExAll 1,000 × 10 | 3.85 | 3.78 | 0.998 | 0.64–1.12 | 0.437 |

**ExNext**

| Phase | a | b | b / a | range | main series: bound / reference |
| --- | ---: | ---: | ---: | ---: | ---: |
| ExNext 40, first pass | 2.14 | 2.18 | 1.030 | 0.94–1.42 | 1.020 |
| ExNext 40 × 10 | 1.59 | 1.61 | 1.010 | 0.99–1.17 | 1.038 |
| ExNext 40 × 10 in `perf` | 1.57 | 1.53 | 0.980 | 0.71–1.08 | 1.053 |
| ExNext 400, first pass | 2.04 | 2.09 | 0.995 | 0.87–1.07 | 1.037 |
| ExNext 400 × 10 | 1.51 | 1.53 | 1.023 | 0.92–1.09 | 1.028 |
| ExNext 1,000, first pass | 2.04 | 2.00 | 0.968 | 0.75–1.03 | 1.008 |
| ExNext 1,000 × 10 | 1.56 | 1.56 | 0.999 | 0.88–1.34 | 1.009 |

**Phases without listings**

| Phase | a | b | b / a | range | main series: bound / reference |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lookup 400 | 0.90 | 0.90 | 1.012 | 0.84–1.18 | 1.026 |
| Sequential write 8 MiB | 0.69 | 0.60 | 1.015 | 0.86–1.06 | 1.048 |
| Sequential read 8 MiB | 1.01 | 1.01 | 0.999 | 0.95–1.08 | 1.007 |
| Append read 1,280 KiB | 1.14 | 1.14 | 0.991 | 0.58–1.18 | 1.007 |
| Small read 40 | 1.21 | 1.36 | 1.036 | 0.67–1.47 | 1.006 |
| Small create 40 | 1.75 | 1.71 | 1.014 | 0.87–1.13 | 0.986 |
| Small delete 40 | 1.75 | 1.73 | 1.009 | 0.96–1.30 | 1.014 |
| Append 4 KiB × 256 | 1.60 | 1.60 | 1.007 | 0.90–1.33 | 1.042 |
| Append 1 KiB × 256 | 1.88 | 1.85 | 0.964 | 0.87–1.05 | 1.014 |

Over all 23 phases the null medians lie between 0.952 and 1.036, and the
geometric mean is 0.999. Single ratios still scatter widely in some phases:
Small read 40 from 0.67 to 1.47, ExAll 1,000, first pass from 0.48 to 0.99.
Medians decide, as in the main series.

### Criteria 3 and 4 against the null series

| Statistic | Limit | Null series | Main series |
| --- | --- | --- | --- |
| First pass of ExAll over 40, 400, 1,000 entries, each | ≤ 1.05 | 0.983, 1.000, 0.952 | 1.116, 1.008, 0.998 |
| Worst of the 16 phases the bound does not change | ≤ 1.05 | 1.036 (Small read 40) | 1.053 (ExNext 40 × 10 in `perf`) |
| Geometric mean of those 16 phases | ≤ 1.02 | 1.003 | 1.021 |

The null series meets criteria 3 and 4 as written. Identical handlers do not
cross the limits in this series, so the limits are attainable on this host.

### Bias between the names and between the positions

With the order alternating, the effect of the position (second start of a round
over the first) and of the name (`reference-b` over `reference-a`) separate.
From the medians of the odd and the even rounds, over the 16 unchanged phases,
the second start takes 0.997 of the time of the first (bootstrap 95 % interval
0.965–1.017) and `reference-b` 1.009 of that of `reference-a` (0.979–1.028).
The same figures from the means of the logarithms are 0.992 and 1.002. Over all
23 phases they are 0.999 and 1.000. The pairing shows no position effect and no
effect of the name beyond about one percent. The same analysis on the main
series gives 1.040 for the position (0.959–1.140) and 1.032 for the name
(0.993–1.184), with intervals that include 1 and the null's figures.

### Spread

The null series scatters much less from round to round than the main series.
The geometric mean over the 16 unchanged phases of one round lies between 0.947
and 1.042 in the null series (standard deviation of its logarithm 0.031) and
between 0.923 and 1.262 in the main series (0.114). The null series therefore
does not reproduce the main series' spread. Either Cachy was noisier in the
main series' window (09:48–09:57 UTC against 12:25–12:34 UTC), or the bound's
starts vary more than the reference's. One series of each cannot tell which.

A bootstrap over the rounds (5,000 draws of four odd and four even rounds)
estimates how often identical handlers would cross the limits. For the null
series the geometric mean of the 16 medians exceeds 1.02 in 18 % of the draws
(95 % interval 0.972–1.032), the worst of the 16 medians exceeds 1.05 in 68 %
(the interval reaches 1.42, because a few phases have single ratios of 1.3 to
1.5), and the largest of the three first-pass ExAll medians exceeds 1.05 in 0.1 %
(interval 0.965–1.036). With only eight rounds the bootstrap is rough. The
same procedure on the main series gives 61 %, 88 % and 79 %.

### Reading

The null series meets criteria 3 and 4 as written, so the limits are not
unattainable for identical handlers, and the harness shows no position or name
bias that could account for the main series' misses. Neither does the null
series make the limits comfortable. It is one draw, and the bootstrap suggests
that the per-phase limit of 1.05 over 16 phases is crossed by identical
handlers in about two of three repeats, and the limit of 1.02 for the geometric
mean in about one of six. A miss of those two statistics alone carries little
information. The main series' 1.053 and 1.021 lie inside what identical
handlers produce, and on this evidence they do not show a cost of the bound.

The first-pass limit is different. For identical handlers the eight ratios of
the 40-entry first pass lie between 0.89 and 1.07, and the 95 % interval of the
largest of the three first-pass medians ends at 1.036. The main series measured
1.116 for that phase, with ratios from 0.78 to 1.50. Identical handlers did not
produce that. The null series shows no harness bias that could cause it. It leaves
open whether the bound's recording costs something at 40 entries or the main
series' window was noisier, and the larger first passes (1.008 and 0.998) show
no cost.

For criteria 3 and 4 this means that the misses of the main series are not
excused by limits that identical handlers cannot meet, and that the per-phase
limit of criterion 4 is tight against the host's scatter. Whether to repeat the
main series, to read the criteria differently or to accept the result is
Fabian's decision. The plan's decision rule and the numbers of the main series
are unchanged.


## Repeat of the main series, 23 October 2026

Fabian ordered a repeat of the main series after the null series. The text and
the numbers above, the null series' addendum and the plan's decision rule are
unchanged.

### Series

The
[scope](evidence/bfs-exall-result-cache-bound-2026-10-22/repeat-2026-10-23/PILOT_SCOPE.md)
fixed the evaluation before the first start (commit `3aaa70a`): the repeat
alone by the decision rule as written, the pooled data of both main series, and
the per-phase comparison with the first series, with a reproduction test for
the two misses of the first series. The same two handlers (`reference`
`e2e274bb…`, `bound` `be30c603…`, whose hashes recorded again equal those of the
first series), guest, PFS3, ROM, formatter, RDB Buffers = 30, source tree, run
script and verifier. Sixteen fresh `compare` starts in eight rounds, BFS first
in odd rounds and PFS3 first in even rounds, `reference` first in odd rounds
and `bound` first in even rounds. All 16 starts pass
`emulator-test/verify-bench-results.sh`. The series ran detached on Cachy from
22:19:31 to 22:28:55 UTC on 22 October (00:19 to 00:28 CEST on 23 October). The
runner container and Coffin were stopped and restored by the launch script with
status 0 at 22:28:57 UTC, and a check at 22:29 UTC finds both running and no CI
worker, benchmark emulator, unit or watcher process of the session left. A
first marker unit watched the wrong process, wrote a false `SERIES_DONE` within
seconds of the start and was replaced, with the false file deleted. The series
was not touched.

### Results

BFS / PFS3 of each handler (median of eight starts), the median of the eight
paired ratios `bound` over `reference` of the repeat with its range, and the
paired medians of the first series and of the pooled data (16 rounds).

**ExAll**

| Phase | reference | bound | bound / reference | range | first series | pooled |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ExAll 40, first pass | 3.11 | 3.61 | 1.070 | 0.95–1.75 | 1.116 | 1.074 |
| ExAll 40 × 10 | 3.18 | 1.30 | 0.421 | 0.41–0.85 | 0.420 | 0.421 |
| ExAll 40 × 10 in `perf` | 2.98 | 1.54 | 0.528 | 0.49–0.90 | 0.526 | 0.528 |
| ExAll 400, first pass | 5.52 | 5.90 | 1.039 | 0.59–2.02 | 1.008 | 1.026 |
| ExAll 400 × 10 | 3.65 | 1.43 | 0.396 | 0.34–1.62 | 0.415 | 0.407 |
| ExAll 1,000, first pass | 4.97 | 5.59 | 1.073 | 0.95–1.33 | 0.998 | 1.019 |
| ExAll 1,000 × 10 | 3.72 | 1.41 | 0.387 | 0.30–0.77 | 0.437 | 0.398 |

**ExNext**

| Phase | reference | bound | bound / reference | range | first series | pooled |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ExNext 40, first pass | 2.04 | 2.10 | 1.000 | 0.98–1.21 | 1.020 | 1.000 |
| ExNext 40 × 10 | 1.63 | 1.67 | 1.036 | 0.98–1.55 | 1.038 | 1.036 |
| ExNext 40 × 10 in `perf` | 1.52 | 1.59 | 1.032 | 0.88–1.48 | 1.053 | 1.039 |
| ExNext 400, first pass | 1.95 | 2.03 | 1.043 | 0.95–1.14 | 1.037 | 1.037 |
| ExNext 400 × 10 | 1.53 | 1.55 | 1.027 | 0.72–1.37 | 1.028 | 1.027 |
| ExNext 1,000, first pass | 2.03 | 2.00 | 1.011 | 0.85–1.15 | 1.008 | 1.008 |
| ExNext 1,000 × 10 | 1.53 | 1.51 | 1.005 | 0.90–1.18 | 1.009 | 1.009 |

**Phases without listings**

| Phase | reference | bound | bound / reference | range | first series | pooled |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Lookup 400 | 0.88 | 0.90 | 1.021 | 1.01–1.13 | 1.026 | 1.021 |
| Sequential write 8 MiB | 0.70 | 0.73 | 1.054 | 0.95–2.41 | 1.048 | 1.054 |
| Sequential read 8 MiB | 0.99 | 0.99 | 1.041 | 0.93–1.25 | 1.007 | 1.013 |
| Append read 1,280 KiB | 1.04 | 1.09 | 1.019 | 0.97–1.25 | 1.007 | 1.009 |
| Small read 40 | 1.27 | 1.27 | 1.069 | 0.83–1.15 | 1.006 | 1.031 |
| Small create 40 | 1.68 | 1.86 | 1.068 | 0.95–1.41 | 0.986 | 1.006 |
| Small delete 40 | 1.70 | 1.77 | 1.005 | 0.92–1.72 | 1.014 | 1.012 |
| Append 4 KiB × 256 | 1.56 | 1.61 | 1.009 | 0.77–1.57 | 1.042 | 1.017 |
| Append 1 KiB × 256 | 1.84 | 1.86 | 1.033 | 0.93–1.10 | 1.014 | 1.018 |

All 23 medians of the repeat lie inside the range of the first series. The
differences between the two series' medians lie between −0.050 and +0.081 with
a median of +0.001, so the two series agree phase by phase within the host's
spread. Geometric mean of the paired medians over the 16 phases the bound does
not change: 1.029 in the repeat, 1.021 in the first series and 1.021 pooled.
Over all 23 phases 0.888, 0.887 and 0.883. Geometric mean of the BFS / PFS3
medians: 1.923 for `reference` and 1.710 for `bound` in the repeat.

Per entry in microseconds, repeat:

| Phase | reference | bound | PFS3 |
| --- | ---: | ---: | ---: |
| ExAll 1,000 × 10 | 19.4 | 7.3 | 5.2 |
| ExAll 400 × 10 | 18.7 | 7.5 | 5.1 |
| ExAll 1,000, first pass | 37.9 | 40.5 | 7.6 |
| ExAll 400, first pass | 39.4 | 40.9 | 7.0 |
| ExAll 40, first pass | 44.8 | 48.0 | 14.4 |

Pooled, the first pass costs the bound 1.2 µs more per entry at 1,000 entries,
1.5 at 400 and 3.2 at 40.

### The decision rule

The rule of the plan, evaluated by `summarize.py` on the medians, for the
repeat alone as ordered, for the pooled data and, as a reference, for the first
series.

| Criterion | Limit | Repeat | Pooled | First series |
| --- | --- | --- | --- | --- |
| 1. Gain at 1,000 entries: `r`, lock-owned estimate | ≤ 0.50, ≤ 0.55 | 0.387, 0.456 (met) | 0.398, 0.461 (met) | 0.437, 0.493 (met) |
| 2. Gain at 400 entries: `r`, lock-owned estimate | ≤ 0.55, ≤ 0.60 | 0.396, 0.460 (met) | 0.407, 0.469 (met) | 0.415, 0.474 (met) |
| 3. First pass over 40, 400, 1,000 entries, each | ≤ 1.05 | 1.070, 1.039, 1.073 (not met) | 1.074, 1.026, 1.019 (not met) | 1.116, 1.008, 0.998 (not met) |
| 4. The 16 unchanged phases: each, geometric mean | ≤ 1.05, ≤ 1.02 | above 1.05: Small read 40 1.069, Small create 40 1.068, Sequential write 1.054; 1.029 (not met) | above 1.05: Sequential write 1.054; 1.021 (not met) | above 1.05: ExNext 40 × 10 in `perf` 1.053; 1.021 (not met) |
| 5. Candidate record, 1,000 entries with 12-byte names | ≤ 32 bytes, < 64 KiB | 30 bytes, 42,000 bytes (met) | the same (met) | the same (met) |
| 6. All starts pass the verifier | all | 16 (met) | 32 (met) | 16 (met) |

The repeat alone meets criteria 1, 2, 5 and 6 and misses criteria 3 and 4, as
the first series did. The pooled data give the same reading. The pooled data
are not an independent test, because the repeat was ordered after the first
series was known.

### The two misses of the first series

- **ExAll 40, first pass** reproduces: 1.116 in the first series, 1.070 in the
  repeat (ratios from 0.95 to 1.75), 1.074 pooled. Identical handlers gave
  0.983 in the null series with ratios from 0.89 to 1.07.
- **ExNext 40 × 10 in `perf`** does not reproduce: 1.053 in the first series,
  1.032 in the repeat, 1.039 pooled. The bound does not change ExNext.

The set of phases that exceed 1.05 is not stable. In the repeat Small create 40
(first series 0.986), Small read 40 (1.006), Sequential write (1.048) and the
first pass over 1,000 entries (0.998) exceed it, and ExNext 40 × 10 in `perf`
does not. The single ratios of these phases scatter widely, for example Small
create 40 from 0.95 to 1.41 and Sequential write from 0.95 to 2.41, so a single
phase near the limit decides little.

### Bias, spread and the bootstrap

Method of the null series, over the 16 unchanged phases. The position effect
(second start over first) is 1.040 in the first series, 0.996 in the repeat
(bootstrap 95 % interval 0.934–1.030), 1.014 pooled (0.978–1.058) and 0.997 in
the null series. The effect of the handler (`bound` over `reference`, after the
position) is 1.032 in the first series, 1.039 in the repeat (1.018–1.109),
1.027 pooled (1.002–1.089) and 1.009 in the null series (0.979–1.028).

Fourteen of the 16 per-round geometric means of the main series lie above 1.00
(all eight of the repeat), against four of eight in the null series. The
round-to-round scatter, the standard deviation of the logarithm of the
per-round geometric mean, is 0.114 in the first series, 0.054 in the repeat,
0.087 pooled and 0.031 in the null series, so the first series' window was the
noisiest of the three.

The bootstrap over the rounds estimates how often a series with this scatter
would cross the limits: the geometric mean of the 16 medians exceeds 1.02 in 93
% of the draws for the repeat and 64 % pooled (null series 18 %), the worst of
the 16 medians exceeds 1.05 in 99.5 % and 80 % (null 68 %), and the largest
first-pass ExAll median exceeds 1.05 in 97 % and 96 % (null 0.1 %). The
intervals of the geometric mean are 1.018–1.059 for the repeat, 1.006–1.046
pooled and 0.972–1.032 for the null series. With eight or sixteen rounds the
bootstrap is rough.

### Reading

By the decision rule as written the repeat meets the gain, memory and validity
criteria and misses the first-listing and no-regression criteria, and so do the
first series and the pooled data. The gain is not in doubt: ExAll 1,000 × 10
takes 0.387 of the reference in the repeat and 0.398 pooled, and the lock-owned
estimate stays below 0.47.

The repeat points further toward a real cost than the reading of the first
series did.

- **First listing.** The first pass of the bound costs a few percent more:
  pooled medians 1.074, 1.026 and 1.019 at 40, 400 and 1,000 entries, which is
  1.2 to 3.2 µs per entry, and the repeat's three first-pass medians all exceed
  1.0 (1.070, 1.039, 1.073) where identical handlers gave 0.98, 1.00 and 0.95.
  The recording is a copy into a static array, so the candidate's fill will
  cost more. At 40 entries the miss exceeds 1.05 in both series and pooled.
- **Phases the bound does not change.** There is no single phase to blame. The
  geometric mean is 1.021, 1.029 and 1.021 against 1.003 for identical
  handlers, the handler effect is about 3 % in both main series and its
  interval excludes 1 in the repeat, and 14 of 16 rounds lie above 1.00 (a
  one-sided sign test gives about 0.002 if the rounds are taken as
  independent). That points to a small real cost of about 2–3 % across phases,
  which the null series does not show. The phases that exceed 1.05 change from
  series to series and the individual misses (one in the first series, three in
  the repeat, one pooled) are marginal.

### What stays open

- The cause of the 2–3 % on unchanged phases. The bound adds a function call
  per packet, 1.2 KB of code and eight bytes to the handler's state. Phases
  that run before the first ExAll, when the array is not yet allocated, also
  exceed 1.05 in the repeat (Small create 40 1.068, Small read 40 1.069,
  against 0.986 and 1.006 in the first series), so the 92 KB array cannot be
  the whole cause. Attributing it would need a variant without the counter, and
  was not measured.
- The real cost of the first listing for the candidate, which allocates.
- Whether the gain on repeated passes of an unchanged directory is worth a cost
  of a few percent on every first listing and on other work. This and whether
  to go on with the plan's second step is Fabian's decision. The invalidation,
  the real listing patterns and real hardware stay as stated above.
