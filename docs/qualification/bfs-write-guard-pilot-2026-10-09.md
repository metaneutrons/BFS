# BFS write protection check pilot

The candidate is retained locally under its predeclared rule. All 14 Cachy
starts and 28 BFS/PFS3 outputs pass correctness and identity checks. The
diagnostic shows exactly one logical inode read fewer per Write packet in every
write phase, and no count field rises anywhere. No phase is at least 10% slower
in both pairs of a mode. The timing ratios are reported, but the identical-
baseline controls vary more than the expected effect, so this pilot does not
establish a production speedup. The <=5x PFS3 objective and the preceding
sparse-CRC read-regression question remain open.

## Change and fixed scope

ACTION_WRITE used to read the inode in the handler (`CheckProtection`) and again
in the core's locked refresh. `bfs_file_write_checked` now tests the deny mask
against the inode that the same locked operation refreshes and writes, before
any data, allocation, sampling or inode change. The handler passes FIBF_WRITE
and maps the new `BFS_ERR_PROTECTED` to `ERROR_WRITE_PROTECTED`. Nothing is
cached between packets, and there is no on-disk change. A side effect is that
check and write now run in one critical section.

The [pre-run scope](evidence/bfs-write-guard-pilot-2026-10-09/PILOT_SCOPE.md)
keeps the goal-root design: two diagnostics, two production pairs per normal/
durable mode with reversed order, and one identical-baseline control pair per
mode. Guests, PFS3, ROM, formatter, format v3 and RDB buffers 30 are those of
the goal-root pilot. Both base handlers were rebuilt from the working baseline
(the five preimage files in place of the candidate's) and are byte-identical to
the goal-root base handlers. The candidate handlers rebuild byte-identically
from the 229-file source tree that ran on Cachy. The diagnostics use the
write-probe build, pinned as `bfshandler-probe`.
[Handler identities and build logs](evidence/bfs-write-guard-pilot-2026-10-09/local/handler-identities.sha256)
are retained.

The retention rule differs from earlier pilots. Fabian agreed to the pilot
after this rule was proposed: identical-baseline controls had ranged from 0.81
to 1.03 for 4KiB growth, wider than the expected effect. Retention requires the
strict checks, the exact read saving, no increase of any diagnostic count field,
and no phase at least 10% slower in both pairs of a mode. Medians do not decide.

## Diagnostic result

| Phase | Write packets | Inode reads base | Candidate |
| --- | ---: | ---: | ---: |
| APPEND_4K_1M | 256 | 515 | 259 |
| APPEND_1K_256K | 256 | 515 | 259 |
| SEQ_WRITE_8M | 128 | 259 | 131 |
| SMALL_CREATE_40 | 40 | 200 | 160 |

In every phase the saving equals the number of Write packets; every other
phase is unchanged. Core file-write calls (256) and inode writes (257) in
4KiB growth are unchanged. No count field of any phase, work or volume flush,
is higher for the candidate. The diagnostic work time of 4KiB growth falls from
128,383 to 100,800 microseconds, while the inode-read sidecar ticks fall from
14,380 to 9,024 (about 7.6 ms at 709,379 Hz). The time drop is not attributable
to the change: in the same pair, LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL, which
has no writes and identical counts, moves by a similar ratio (0.797). One probed
pair is not production qualification. All probe timers are inclusive and
nested.

## Production result

Times are microseconds; durable times include the volume flush. Lower
candidate/base ratios are faster.

| Mode and first filesystem | Base 4KiB growth | Candidate | Ratio | PFS3 calibrator |
| --- | ---: | ---: | ---: | ---: |
| Normal 1 BFS | 78,976 | 67,903 | 0.8598 | 0.6996 |
| Normal 2 PFS3 | 82,175 | 68,672 | 0.8357 | 0.9676 |
| Durable 1 BFS | 75,520 | 68,928 | 0.9127 | 1.0204 |
| Durable 2 PFS3 | 74,943 | 79,745 | 1.0641 | 0.8502 |

The paired medians are 0.8477 normal and 0.9884 durable; three of four pairs
are not slower, so the timing checks of the earlier rule would pass. The
earlier rule as a whole would not: it also required fewer diagnostic buffer
requests, and those stay at 517. No phase is at least 10% slower in both pairs
of a mode. The highest
single-pair ratios are 1.069 normal (ExNext 40, first pass) and 1.104 durable
(ExAll 1,000, repeated ten times).

The identical-baseline controls give 1.1189 normal and 1.2367 durable for 4KiB
growth. Normal SMALL_READ_40, which the change does not touch (no count field
differs), is 0.644/0.688: its paired base runs took 29,312/29,504 microseconds,
the same base handler 21,631/20,160 in the control. Normal APPEND_1K_256K
repeat 2 is 0.394 because of one slow base run (144,126 microseconds against
56,638 to 65,471 in the other runs). These show variability. No observation is
corrected or removed. The [summary](evidence/bfs-write-guard-pilot-2026-10-09/summary.json)
retains every phase, both filesystems and all raw fields; the
[decision](evidence/bfs-write-guard-pilot-2026-10-09/decision.json) gives
`retain_locally=true`.

## Tests and review

The candidate passes 587 host tests and the same 587 under ASAN/UBSAN (leak
detection is unsupported on this Mac). Nine new tests cover the denial without
side effects, length-zero writes, other protection bits, protection set and
cleared after open, an inode published by another handle, an open unlinked
inode, one inode lookup per checked write, and argument and read-only
precedence. The m68k production, probe and write-probe handlers build. The
Amiga integration suite passes 52/52 on the AROS ROM, including `protect_12`
and `protectio_40`.

An independent reviewer agent accepted the change with three changes, all
applied: a test gap for in-place bytes written before a denial, the precedence
of `len > INT32_MAX`, and denial without a valid refreshed inode. Its seven
mutations, ported to the final code, each fail at least one test. The
[review record](evidence/bfs-write-guard-pilot-2026-10-09/local/implementation-review.md)
lists them. Observable differences in the handler: on a reloaded volume or
after a recovery error, a write to a protected file reports that error instead
of `ERROR_WRITE_PROTECTED`; and a denied write refreshes the handle's cached
size and extents like any other locked operation, keeping its offset.

## Execution and restoration

The series ran 21:42:10–21:51:12 UTC on 9 October 2026. The launch script
found no active CI job, stopped the runner and Coffin, and restored both with
status 0 at 21:51:14 UTC. A fresh check at 21:51:57 UTC records the runner
container and Coffin running, zero CI worker processes and no emulator
([labelled](evidence/bfs-write-guard-pilot-2026-10-09/local/services-restored-command.txt)). All input identities and all 229 source
identities pass before and after the series. Licensed assets, executables, ROMs
and HDFs are not in the bundle. No CI, commit, push, merge, installation or
issue change was made.
