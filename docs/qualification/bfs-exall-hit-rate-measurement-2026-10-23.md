# BFS ExAll hit-rate measurement

The bound and its repeat show what a result cache gains when it hits, not how
often it would hit. Fabian chose to measure that before anyone builds the
cache: a debug build of the handler counts, while someone uses a BFS volume in
an emulator, how many of the entries listed by ExAll and ExNext came from repeat
listings of a directory the volume had not changed since. This report describes
what was built and checked and how Fabian runs it. No real-use data exists yet.
The [plan](../plans/bfs-exall-result-cache-v1.md) (section "Hit-rate
measurement") defines the classes and the decision rule. Fabian accepted its
thresholds on 23 October 2026 without change, and they are fixed.

## The counter

A patch to `src/amiga/handler.c` and one new header, kept in the
[evidence directory](evidence/bfs-exall-hit-rate-2026-10-23/hitrate-handler.patch)
and not in `src/`. Everything is compiled only with `-DBFS_HITRATE=1`.

- **What it counts.** At every start of an ExAll pass (`eac_LastKey` 0) or an
  ExNext pass (`fib_DiskKey` 0 on a directory lock) it classifies the pass as A
  new, B same lock, C other lock or D changed, for ExAll and ExNext separately,
  and counts starts and entries per class. A pass is complete when a call
  reports that no entries remain. The handler keeps, for 512 directories, the
  volume state and the lock of the latest complete pass of each kind and of
  either kind, and forgets a lock when it is released. Each start is classified
  under two validity tests, the tree stamps of the ExNext read-ahead (the
  candidate's, and the deciding one) and the packet counter of the bound, and
  against the latest pass of its own kind or of either kind. It also counts
  calls, entries returned, abandoned and complete passes, passes during which
  the volume changed, evictions, patterns and entries without a counted start.
- **Cost.** The table takes 125,268 bytes (compiler-emitted, m68k), allocated at
  the first listing, and each lock grows by 80 bytes. Release builds are
  untouched: the patched tree built without the define by `make release` gives a
  68040 handler with the SHA-256 of the unpatched `reference` handler
  (`e2e274bb…`, byte for byte).
- **Readout.** The handler answers a read-only packet (`BFS_ACTION_HITRATE_READ`,
  3020) with its counters, and a reset packet (3021). The small Shell command
  `hitrate BFS:` prints them as tab separated lines, and `hitrate BFS: RESET`
  clears them. The handler does not write a file itself: its request port is the
  process's own `pr_MsgPort`, so a DOS call from the handler would deliver its
  reply to that same port. In the kit a background loop runs the command every
  ten seconds and writes `System/hitrate.txt`, a folder on the Mac. Nothing has
  to be triggered at the end of a session, and a closed emulator leaves the file
  at most ten seconds old.

## What was checked

- **Scripted run** (`hitrate-check`, in FS-UAE with the instrumented 68040
  handler). It makes two directories, lists them in a fixed order and changes
  the volume in between, then compares every counter with the value that the
  definitions give, written down by hand and not computed by the counter's rules.
  Thirteen passes: ExAll twice on one lock (A, B), through a second lock (C), a
  changed volume (D), unchanged again (B), a pass abandoned after one call with a
  small buffer and a following complete pass (B, B), ExNext on the same lock (own
  kind A and the either-kind scope B, then B), ExNext through a third lock (C),
  a file opened and closed only for reading and then ExAll again, a second
  directory (A) and a pattern pass. Result: 13 passes, 0 failures, with the
  final binaries. The read-only open and close raised the packet counter and left
  the tree stamps alone, so the stamp test (B) and the counter test (D) disagree
  on that pass as intended: closing a file to read it does not invalidate what a
  cache built on stamps would hold. The scripted run validates the counter and is
  not evidence of real use.
- **The check can fail.** A handler that forgets to clear released locks (the
  mutation) makes the same run report 16 failures, because a new lock at a freed
  lock's address is mistaken for the old one.
- **Listings unchanged.** The full on-Amiga suite (`bfs-test`) passes against the
  instrumented 68020 handler: 55 passed, 0 failed, which includes the ExAll and
  ExNext tests (`exall_44`, `exalldel_49`, `exallpack_50`, `exnext_37`,
  `exnextdel_48`, `exnextfresh_55`). The compiler warnings are the project's own
  (`-Wall -Wextra -Werror`, frame limit 16 KiB).
- **The kit boots and counts, in the console.** The rebuilt kit (after the
  Workbench finding below) boots in FS-UAE in both modes, and in both the volume
  `BFS:` mounts with 3,396 files in 468 directories (52 % of 63 MB used), a
  `Dir BFS:Projekte ALL` lists its 381 lines, and the Shell console runs the
  commands that come after the Workbench decision: a foreground `List BFS:Berichte`
  (81 lines) and `Dir BFS:Archiv` (600 lines). The counters then show 37 complete
  ExNext passes with 1,863 entries returned and no ExAll call at all: 34 passes of
  class A (1,105 entries) and 3 of class C (758 entries), listings repeated through
  another lock with the volume unchanged. In Workbench mode `verify-kit.sh` ends
  with exit code 3 and the line `WORKBENCH: NOT AVAILABLE`, because `icon.library`
  and `workbench.library` do not open (`results/kit-check-workbench/`); in shell mode
  it ends with 0 (`results/kit-check-shell/`).
- **The own-Workbench route works with a stand-in.** `verify-attach.sh` writes a
  stand-in boot folder and an FS-UAE configuration for it, runs `attach-to-config.sh`
  on that configuration (the copy gets the BFS hard file as hard drive 1 and the
  folder `Hitrate` as hard drive 2, label `HR`, and the original is unchanged), adds
  the line of `Startup-Zeilen.txt` to a `User-Startup`, boots, lists
  `BFS:Projekte ALL` twice and reads the counters through `HR:`. Result: both
  listings have 382 lines, and the counters hold 31 ExNext passes of class A and
  31 of class C with 421 entries each, as the two identical listings through two
  locks give (`results/attach-check/`). This does not test Fabian's Workbench, his
  configuration or a Workbench window.
- **No command of the assets lists through ExAll.** With the counters reset
  before each step, `Copy ... ALL` from BFS and within BFS, `Delete ... ALL`,
  `Protect ... ALL`, `Search ... ALL`, `Filenote ... ALL`, `List ... ALL` and
  `Dir ... ALL` showed 0 ExAll calls and only ExNext calls
  (`results/command-listing-calls/`). The console route therefore measures ExNext
  only. `summarize.py` now prints a note when a session had no ExAll call,
  because the rule's "stop" would then mean only that there was nothing to count.
  The rule, its thresholds and the counters are unchanged.

## The kit

`build/hitrate-kit/` in this checkout (git ignores `build/`; it holds the
Kickstart ROM and the commands of `emulator-test/.assets`). It can be
rebuilt with `docs/qualification/evidence/bfs-exall-hit-rate-2026-10-23/build-kit.sh`
from the instrumented handler built by `build-instrumented.sh 040`. It holds the
boot folder `System`, the hard file `bfs.hdf` (the BFS volume with the tree of
`make-tree.py`: the assets, the sources and documents of the repository and raw
results of earlier measurements, with a 600-entry flat folder and trees up to
14 levels deep), `start.sh`, `summarize.py`, the one-page German instructions
`ANLEITUNG.md`, and for the own-Workbench route `attach-to-config.sh`, the folder
`Hitrate` and `Startup-Zeilen.txt`.

**Workbench does not start from this kit.** `./start.sh` checks at boot whether
`icon.library` and `workbench.library` open (`C:openlibs`, return code 5 if not),
writes the result to `System/workbench-check.txt` and, if they do not open, says so
and leaves the Shell console on screen. The listing commands (`Dir`, `List`,
`Copy`, `Delete`) work there. If both libraries open, the kit runs `LoadWB` and
records its return code in `System/loadwb-rc.txt`. `./start.sh shell` skips the
check. Fabian has three ways to a result (see the instructions): the console,
the Workbench of the kit once two libraries of his Workbench 3.2 are copied into
`System/Libs` (untested, I have no such files), or his own Workbench with the
BFS volume attached to a copy of his FS-UAE configuration by `attach-to-config.sh`
(tested with a stand-in, not with his Workbench). The following section gives the
finding behind this.

Fabian uses the volume for 10 to 20 minutes, waits 15 seconds, closes the
emulator and runs `python3 summarize.py System/hitrate.txt` (route C:
`Hitrate/hitrate.txt`), which prints the decision-rule table in German. The
result file goes to `sessions/` of the evidence directory, one file per session.

## Workbench and the minimal system folder

Fabian reported on 23 October 2026 that the first version of the kit stopped with
`invalid resident library` and `LoadWB failed returncode 20` and left the prompt
`1>`. That was right, and my earlier report was wrong to count "the Workbench
starting" among the checked things: the boot check ran its listings before the
kit's `LoadWB` and never tested whether Workbench came up. The cause, found in the
emulator and read from files only (`results/workbench-diagnosis/`):

- `LoadWB` with DOS requesters suppressed (`quiet`) prints the two lines above and
  returns 20. Without the suppression it waits behind a "System Request" window on
  the Workbench screen, and the console hangs at the next command that needs a
  library. That is why `List` hung after Workbench in my first checks.
- `OpenLibrary` fails for `icon.library` and `workbench.library` (`openlibs`),
  although the Kickstart's resident list holds both (version 47, together with
  `workbench.task` 39; `inventory`). `asl.library` and `amigaguide.library` from the
  assets' `Libs` do not open either. I did not find out why; they presumably need
  `icon.library`. All other libraries that I tried open (exec, dos, intuition,
  graphics, layers, utility, locale, keymap, gadtools, diskfont).
- `SetPatch` and `IPrefs` return 0 and change nothing, and the assigns `ENV:`, `T:`,
  `CLIPS:`, `ENVARC:` do not help (the kit makes them anyway, `List` needs them).
- `emulator-test/.assets` holds the Kickstart ROM, `C` and `Libs` of the CI image
  and no disk copies of `icon.library` and `workbench.library`. The two files can
  only come from a Workbench 3.2 installation, which this checkout does not have.

The kit now tests for exactly this at boot (`workbench-check.txt`, `loadwb-rc.txt`)
and `verify-kit.sh` exits with 3 in Workbench mode if Workbench is not available.

## What the numbers will and will not show

They show how one person used one emulated machine for the length of one
session, with the programs and files of that session, and the share of the
entries listed by ExAll and ExNext that a result cache could have served. They
count entries, not time (an ExNext entry costs several times an ExAll entry on
this machine). They are not a statistic of the Amiga world, and a few sessions
of one person can differ a lot. The stamps are those of the BFS volume alone.
The table holds 512 directories; the summary warns if it was full.

## What I could not check

- **Workbench.** It never started in any of my checks, and I have no files that
  would start it. Not exercised: the Workbench of the kit with the two libraries
  copied in (route B), Fabian's Workbench with the attach script (route C; tested
  only with the stand-in above), Workbench windows and menus, Shell windows, and
  file requesters. The first instructions described such steps and are replaced.
- **Which programs list through ExAll.** The scripted run counts ExAll
  correctly. In every boot check and for every command of the assets that I
  tried, listing went through ExNext. Which part of a real session lists through
  ExAll depends on the programs used, probably on Workbench windows and requesters,
  but I did not show that; the counters will tell, and route A cannot.
- **Why the ROM's `icon.library` and `workbench.library` do not open.** I measured
  that they do not and that `LoadWB` returns 20 with `invalid resident library`;
  I did not find out what in the Kickstart or the missing disk files causes it.

## Evidence

[`evidence/bfs-exall-hit-rate-2026-10-23/`](evidence/bfs-exall-hit-rate-2026-10-23/README.md)
holds the patch, the programs (`hitrate`, `hitrate-check`, `openlibs`, `quiet`,
`taskdump`, `inventory`), the build, run, kit and check scripts, `summarize.py`, the
German instructions, the results above (including `workbench-diagnosis/`) and
`SHA256SUMS`. No screenshot is kept.
