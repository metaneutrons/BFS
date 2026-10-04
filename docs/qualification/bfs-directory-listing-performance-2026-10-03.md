# Directory listing — 2026-10-03

## Result and acceptance target

The target remains **≤5× PFS3 in every checked AmigaDOS workload**, with
unchanged durability, integrity, snapshot and recovery guarantees. Directory
listing became a checked workload in this round. In the last series, the mean
of every workload is below 5× PFS3 and no run of any workload exceeds it. This
is an emulated comparison on one host, not a hardware qualification.

| Workload | BFS mean (µs) | PFS3 mean (µs) | Ratio of means | Run range | Runs >5× |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 40213 | 16171 | 2.49× | 2.33–2.67× | 0/6 |
| Lookup 400 | 65738 | 60458 | 1.09× | 1.01–1.15× | 0/6 |
| Read 40 | 18261 | 14134 | 1.29× | 1.15–1.54× | 0/6 |
| ExNext listing 400 | 34069 | 12512 | 2.72× | 2.16–3.13× | 0/6 |
| ExAll listing 400 | 9312 | 2368 | 3.93× | 3.76–4.18× | 0/6 |
| Write 8 MiB | 65952 | 40053 | 1.65× | 1.53–1.70× | 0/6 |
| Read 8 MiB | 260254 | 259430 | 1.00× | 0.96–1.05× | 0/6 |
| Delete 40 | 22432 | 7082 | 3.17× | 2.93–3.50× | 0/6 |

## What changed

Before this round, ExNext and ExAll found their place by counting entries from
the start of the directory on every call, so a listing was quadratic in the
directory size: 1000 entries took 24.7 s with ExNext under Kickstart 3.2. After
that fix the listing was linear but still 5.2× (ExNext) and 8.1× (ExAll)
slower than PFS3. The changes, in order:

| Step | Commit | ExNext | ExAll |
| --- | --- | ---: | ---: |
| Baseline, linear listing | `48ca58a` | 5.17× | 8.09× |
| Cache hash index, scan through cached views, leaf cursor | `00187f0` | 3.00× | 5.95× |
| ExAll entries packed by type and comment, last-leaf search hint | `5f7f552` | 2.89× | 4.65× |
| Direct u32 key comparison, one name copy per ExAll entry | `cd16c4a` | 2.72× | 3.93× |

Ratios are ratios of means from the series below. Each row's change is
described in its commits: the cache finds blocks through a hash index; scans
view inner nodes instead of copying them and copy only the leaf they report
from; an enumerating lock keeps that leaf copy and resumes from it while the
directory tree's generation is unchanged; ExAll entries hold only the fields
of the requested type and the real comment; a search answers from the leaf it
last ended in while that leaf is current; node searches compare big-endian
u32 keys without a call per step.

## Series

Two series ran with `emulator-test/bench-series.sh`, six fresh runs per
handler, handlers in turn, odd runs with BFS first and even runs with PFS3
first. `tools/bench-summary.py` produced the summaries.

Series 1 compared the baseline, the leaf cursor and the search hint. Per-run
ratios to PFS3 for the listing phases:

| Handler | ExNext per run | ExAll per run |
| --- | --- | --- |
| Baseline | 5.03, 5.18, 5.20, 5.28, 5.46, 4.89 | 7.80, 7.95, 8.30, 7.74, 9.35, 7.51 |
| Leaf cursor | 3.05, 3.02, 3.17, 2.98, 2.93, 2.86 | 6.03, 5.68, 6.62, 5.91, 5.85, 5.69 |
| Search hint | 2.84, 2.83, 3.09, 2.92, 2.78, 2.90 | 4.17, 4.25, 5.79, 4.32, 4.43, 4.98 |

Series 2 compared the search hint with the final handler:

| Handler | ExNext per run | ExAll per run |
| --- | --- | --- |
| Search hint | 3.02, 2.87, 2.52, 3.06, 2.97, 2.73 | 4.56, 4.17, 3.88, 7.24, 4.42, 4.54 |
| Final | 3.06, 2.72, 2.57, 2.95, 3.13, 2.16 | 3.76, 3.98, 3.78, 4.18, 3.92, 4.00 |

The search-hint handler exceeded 5× in one ExAll run of each series (5.79 and
7.24). Both runs were slower in ExNext as well, which points at the host, but
they remain included. The final handler's ExAll runs spread by 5.2%. The
guest's timer reports in steps of 64 µs, which limits the resolution of the
PFS3 ExAll values around 2.4 ms.

No other workload became slower. The other phases moved within their
run-to-run variation; large write went from 1.83× for the baseline in series 1
to 1.65× for the final handler in series 2.

## Host instruction counts

`make core-workload-profile` replays the workload through the core API with
the handler's cache settings. These are x86 instruction counts, a regression
signal rather than 68k time.

| Phase | Before | After | Change |
| --- | ---: | ---: | ---: |
| ExNext listing 400 | 1661940 | 728445 | -56% |
| ExAll listing 400 | 571211 | 301799 | -47% |
| Lookup 400 | 1347371 | 1249490 | -7% |
| Read 40 | 327626 | 269382 | -18% |
| Write 8 MiB | 3693815 | 3481922 | -6% |
| Read 8 MiB | 2227491 | 2179948 | -2% |
| Create 40 | 6494661 | 6348972 | -2% |
| Delete 40 | 6194443 | 6015756 | -3% |

## Environment and identities

FS-UAE 3.1.66 in a Linux cloud container under Xvfb, A1200, 68040 at maximum
speed, 2 MiB chip and 8 MiB fast memory, Kickstart 47.102 and Workbench 3.2
commands, equal fresh 256 MiB RDB images with 30 buffers. The earlier reports
used FS-UAE 3.2.35 on a different host; their absolute times are not
comparable with these. The container's scheduler is not controlled; during
the runs only light editing ran on the host.

| Binary | SHA-256 |
| --- | --- |
| Baseline handler (`48ca58a`) | `86ddea53c0466014a218db0e74cf38e7a5a55ccc8ccef75a7ed48ba92320d279` |
| Leaf cursor handler (`00187f0`) | `60469a7732ce804488f04f312c9314db80b64aa4b1ce8503e85dc60ae598a5f2` |
| Search hint handler (`5f7f552`) | `bf4f9b9871c691a64334b4b0b40d2681c549f9c8a3c00b55d4015d73c3255e91` |
| Final handler (`cd16c4a`) | `3a156e716f4c6c1363eadc92e7c4f9b96929ad3fd92c1b5967e25614cd6fbbd0` |
| `fs-compare-bench` guest (`872a332`) | `bdfa879fa0d02fa974e686fe8d3733625aff9a73be2b001a99e0feb9c53362ec` |
| `pfs3aio` | `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7` |

Kickstart, Workbench and PFS3 files are licensed and not part of the
repository. The raw run outputs were not retained.
