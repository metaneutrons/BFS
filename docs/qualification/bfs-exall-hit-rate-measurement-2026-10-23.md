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
- **The kit boots and counts.** In both modes below, FS-UAE boots the kit, the
  volume `BFS:` mounts with 3,362 files in 462 directories (52 % of 63 MB used)
  and a `Dir BFS:Projekte ALL` lists its 381 lines. The counters then show 35
  complete ExNext passes over 34 directories with 1,181 entries returned: 34 of
  class A and one of class C, the second listing of `Berichte` through another
  lock (a `Dir` started after Workbench was up in Workbench mode, a `List` in
  shell mode) with the volume unchanged.

## The kit

`build/hitrate-kit/` in this checkout (git ignores `build/`; it holds the
Kickstart ROM and the Workbench commands of `emulator-test/.assets`). It can be
rebuilt with `docs/qualification/evidence/bfs-exall-hit-rate-2026-10-23/build-kit.sh`
from the instrumented handler built by `build-instrumented.sh 040`. It holds the
boot folder `System`, the hard file `bfs.hdf` (the BFS volume with the tree of
`make-tree.py`: the assets, the sources and documents of the repository and raw
results of earlier measurements, with a 600-entry flat folder and trees up to
14 levels deep), `start.sh`, `summarize.py` and the one-page German instructions
`ANLEITUNG.md`.

`./start.sh` starts Workbench with the counters at zero. `./start.sh shell` starts
only the boot console, without Workbench. Fabian uses the volume for 10 to 20
minutes, waits 15 seconds, closes the emulator and runs
`python3 summarize.py System/hitrate.txt`, which prints the decision-rule table in
German. The result file goes to `sessions/` of the evidence directory, one file
per session.

## What the numbers will and will not show

They show how one person used one emulated machine for the length of one
session, with the programs and files of that session, and the share of the
entries listed by ExAll and ExNext that a result cache could have served. They
count entries, not time (an ExNext entry costs several times an ExAll entry on
this machine). They are not a statistic of the Amiga world, and a few sessions
of one person can differ a lot. The stamps are those of the BFS volume alone.
The table holds 512 directories; the summary warns if it was full.

## What I could not check

- **Workbench and Shell windows by hand.** I cannot operate the emulator's
  windows. Checked are the boot, the Workbench starting, the volume, listings
  started from scripts, the counters and the file `hitrate.txt`. The menu steps
  and the Shell windows of the instructions were not exercised, and
  whether the file requester `RequestFile` starts a listing here was not shown:
  it produced none within six seconds.
- **`List` after Workbench.** In this minimal system `List` hangs once Workbench is
  running, on `SYS:` and `RAM:` as on BFS, with the instrumented and with the
  reference handler, so BFS and the counter are not the cause. A foreground
  command typed into the boot console after Workbench hangs as well, while
  commands started with `Run` do not. `Dir` works after Workbench, and every
  listing command works in shell mode. The kit's instructions say so. Without
  the standard assigns (`ENV:`, `T:`, `ENVARC:`) `List` hangs even before
  Workbench; the kit makes them.
- **Which programs list through ExAll.** The scripted run counts ExAll
  correctly. In the boot checks `Dir` and `List` used ExNext. Which part of a real
  session lists through ExAll depends on the programs used, and the counters will
  tell.

## Evidence

[`evidence/bfs-exall-hit-rate-2026-10-23/`](evidence/bfs-exall-hit-rate-2026-10-23/README.md)
holds the patch, the programs (`hitrate`, `hitrate-check`), the build, run and
kit scripts, `summarize.py`, the German instructions, the results above and
`SHA256SUMS`. No screenshot is kept.
