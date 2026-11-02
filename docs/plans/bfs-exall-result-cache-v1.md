# BFS ExAll result cache, v1

Decision state: bound experiment only. Nothing is implemented, and the
experiment retains nothing in `src/`. Whether a real implementation follows
is decided by the rule below, which was fixed before any measurement. The
bound ran on 22 October 2026; its reading is in
`docs/qualification/bfs-exall-result-cache-bound-2026-10-22.md`. The rule
below is unchanged since its first commit.

## Problem

ExAll is the slowest workload against PFS3. The 68040 release handler of
v0.3.0 needs 3.55 times PFS3's time for 400 entries ten times, 6.24 times for
the first pass over 1,000 entries and 3.93 times for 1,000 entries ten times
(`docs/qualification/bfs-v0.3.0-performance-2026-10-12.md`). PFS3 keeps size,
protection and date in its directory blocks; BFS reads one inode per listed
entry.

The retained work has taken the device out of the repeated case: the node
cache floor of 64 nodes, the batched ExAll (64 entries, inodes read in
ascending order), the leaf-range hints and the lock-level leaf copy. Repeated
ExAll over 1,000 entries reads nothing from the device. What remains is CPU
per entry: the directory scan, the inode search and the fill. A handler that
reads no inode at all reaches only 2.32 times PFS3 (from 4.00), because scan
and fill stay
(`docs/qualification/bfs-listing-metadata-bound-2026-10-10.md`). Fabian
rejected on-disk duplicate fields (`docs/plans/bfs-listing-metadata-v1.md`),
so format v3 stays as released. A cache in RAM changes no format.

## Candidate

A cache of what a finished listing computed, per directory: for each entry
the name, the entry type, the inode number, the entry's place in key order
(the lock's resume point), and from the inode the size, protection, modify
date and the comment flag. A later ExAll over the same directory takes scan
and inode reads from the cache. The fill (`exall_write_entry`), the pattern
match and the lock's resume point work exactly as before, so a listing returns
what an uncached one returns.

- **Validity.** The cache is used only while neither the directory tree nor
  the inode tree has changed since the listing read them. The ExNext
  read-ahead already decides this with tree root, generation, pending-inode
  version and recovery generation (`ExNextBatchCurrent`, `ExNextInode`); the
  cache would use the same stamps. Both trees change with any write on the
  volume, so validity is volume-wide: one write anywhere ends it.
- **Memory.** Capped in entries and bytes. A directory above the cap is not
  cached. Freed when the lock is released and when an allocation fails
  elsewhere.
- **Lifetime.** The cache belongs to the lock, as the scan cursor and the
  ExNext batch do. A listing through a new lock starts cold. Only repeated
  ExAll passes on one lock hit, which is what the benchmark guest does (one
  lock, ten passes) and what most programs do not do (they lock the directory
  again for each listing).
- **Fill.** The first complete pass over the directory records what it
  gathers. A pass that stops early (full buffer, `ExAllEnd`, error) records
  nothing.

## What helps and what does not

The cache helps only repeated listings of an unchanged directory. The first
listing gets nothing and pays for recording. A listing after any write on the
volume gets nothing. A program that lists a directory once, as `Dir`, a file
requester or a Workbench drawer does, never hits. The benchmark's ten passes
on one lock are the most favourable case there is, and the bound uses them.

## The bound

The bound measures how fast ExAll can be when the scan and the inode reads of
a repeated pass cost almost nothing, by serving them from a fixed array. It
does not implement the candidate. The patch (`resultcache-handler.patch` in
the evidence directory, never in `src/`) adds to `src/amiga/handler.c` at
current `origin/main`:

- a handler-level counter, raised by every packet that can change the
  filesystem and by a remount;
- one array of 1,024 entries holding the gathered entries with their inode
  copies, the directory's inode number and the counter value at recording;
- the batched ExAll takes its gather and its inodes from the array while the
  directory matches and the counter is unchanged. Otherwise it behaves as
  before and, for a complete unfiltered pass that reads inodes, records.

The bound omits what the candidate adds or changes, so it overstates the gain:

| Omitted | Why it matters |
| --- | --- |
| Invalidation | The counter costs one increment per packet. The candidate compares tree stamps per call and throws the cache away after every volume write; the hit rate of real use is not measured. |
| Memory | The array is static, 1,024 entries at a fixed size, and is not allocated, capped per lock or released under memory shortage. A 2 MB machine feels that. |
| Fill | Recording is a copy into a static array. The candidate allocates, and may fail to. The first-pass phases therefore understate the price of the first listing. |
| Lifetime | The array survives `UnLock`. The first-pass phase fills it and the ten-pass phase finds it full. A lock-owned cache would fill in the first of the ten passes. The report derives that figure (below). |
| Comments | An entry with a comment still reads its comment record on every listing. The benchmark has none. |

The array holds a full 56-byte inode copy per entry because the existing fill
takes a `bfs_inode_t`; the candidate needs only the four fields above. The
report states both sizes. The per-entry size of the bound is read from the
compiler-emitted symbol size for m68k, the candidate's from a compiled
record type with those fields.

## Measurement

Two uninstrumented 68040 release handlers (`make release`, `-O2 -m68040`,
`build/release/bfshandler.040`): `reference`, current `origin/main`
(`b8f3e8a`), and `bound`, the same tree with the patch. Sixteen fresh
`compare` starts on Cachy in eight rounds, as in the earlier bound series:
BFS first in odd rounds and PFS3 first in even rounds, the handler order
rotating per round, RDB Buffers = 30, guest, PFS3, ROM and formatter as in the
v0.3.0 series. The scope is written to the evidence directory before the
series starts. The reported figure for each phase is the median of the eight
paired ratios `bound` over `reference` in the same round, and the median
BFS / PFS3 ratio of each handler. Cachy's spread is large (in the v0.3.0 series single paired ratios of
the unchanged ExAll phases ranged from 0.87 to 1.17), so medians decide and no
start is repeated, dropped or pooled with another run.

The lock-owned figure for a phase of ten passes is derived, not measured:
`est = 0.1 · f + 0.9 · r`, with `r` the paired median of that ten-pass phase
and `f` the paired median of the first-pass phase over the same directory.
One pass fills at the cost of a first pass relative to the reference, nine
hit.

## Decision rule

Fixed on 22 October 2026, before the handlers are built and before any start.
A real implementation is planned only if all of the following hold. Every
ratio is the median of the eight paired ratios `bound` over `reference`.

1. **Gain at 1,000 entries.** ExAll 1,000 × 10 has `r ≤ 0.50` and the
   lock-owned estimate `est ≤ 0.55`. The listing-fields bound of 10 October
   reached 0.59 with a format change the owner declined; a cache that works
   only for repeated listings must clearly beat it, so it has to at least
   halve the time.
2. **Gain at 400 entries.** ExAll 400 × 10 has `r ≤ 0.55` and `est ≤ 0.60`.
   The ten-pass phase over 40 entries and the legacy `LIST_EXALL_400` (40
   entries ten times) are reported without a threshold.
3. **The first listing stays cheap.** The first-pass ExAll phases over 40, 400
   and 1,000 entries each have a ratio of at most 1.05. A listing that is
   never repeated must not become more than 5 % slower, and the bound's
   recording is cheaper than the candidate's.
4. **No regression elsewhere.** Every phase the bound does not change (the
   nine phases without listings and the seven ExNext phases) has a ratio of
   at most 1.05, and the geometric mean of those sixteen ratios is at most
   1.02. A phase above 1.05 counts as a miss. The report says whether the
   bound's code can reach it, but the reading of the rule does not change.
5. **Memory.** The candidate's record per entry, excluding the name, is at
   most 32 bytes as the m68k compiler lays it out, so that 1,000 entries with
   AROS-typical names (median 12 bytes) stay below 64 KiB.
6. **Valid runs.** All 16 starts pass `emulator-test/verify-bench-results.sh`
   for `compare`; otherwise the run is reported as failed and not interpreted.

If 1 or 2 fails, the cache is not worth its invalidation and memory cost, and
the plan ends here. If only 3, 4 or 5 fails, the bound is not clean; the
report says why and Fabian decides whether to repeat or change the design. If
all hold, a second plan designs invalidation, the memory cap and the fault
and fuzz tests, and the real implementation must itself reach `r ≤ 0.60` at
1,000 entries ten times against the same reference and pass criteria 3 and 4,
because the bound is a ceiling.

## Strongest objection

A gain that exists only for repeated listings of an unchanged directory
almost never occurs outside this benchmark: programs list a directory once,
with a new lock, and any write on the volume ends the validity. The bound
cannot answer that. It measures how much faster the best case becomes, not how
often the best case occurs. The thresholds are therefore demanding, the
report describes real listing patterns as unmeasured, and the decision to
carry the complexity, if the bound clears the rule, stays with Fabian.

## Open questions

1. Whether a cache that survives `UnLock` (keyed by directory, freed under
   memory pressure) is acceptable. It reaches repeated listings through new
   locks, but holds memory without a lock to tie it to.
2. Whether a per-directory change counter, instead of the volume-wide tree
   stamps, is worth its cost on every write. It would let a write in another
   directory leave the cache valid.
3. Whether the same cache should serve ExNext and `readdir`. ExNext has its
   read-ahead already and is not part of this bound.

## Hit-rate measurement

The bound and its repeat measure how much a result cache gains when it hits.
They cannot say how often it would hit in real use, which the strongest
objection above turns on. Fabian chose to measure that before anyone builds the
cache: a debug build of the handler counts, while a person uses a BFS volume in
an emulator, how many of the listed entries came from repeat listings of an
unchanged volume. The counters change no listing and are not part of any
release build.

### Definitions

- A **pass** is one enumeration of a directory from its start to its end through
  one lock. It **starts** with an ExAll call whose `eac_LastKey` is 0 or an
  ExNext call whose `fib_DiskKey` is 0, and it is **complete** when a call
  reports that no entries remain. A pass that ends earlier (`ExAllEnd`, `UnLock`
  or a restart) is abandoned and fills nothing.
- For every directory the handler keeps its latest complete passes: the volume
  state at their start, and the lock they used while that lock is held. It keeps
  one record for the latest complete ExAll pass, one for the latest ExNext
  pass and one for the latest of either, in a table of 512 directories. A pass
  during which the volume changed fills nothing.
- At every start the handler classifies the pass by the record of its kind:
  - **A, new:** no complete pass of this directory in this mount.
  - **B, same lock:** the latest complete pass left the volume as it is now and
    used the lock that starts this pass, which is still held. A cache owned by
    the lock would hit.
  - **C, other lock:** the volume is as it was, but the latest complete pass used
    another lock, now released or still held. Only a cache that survives
    `UnLock` would hit.
  - **D, changed:** the volume has changed since the latest complete pass.
- It counts starts and entries for each class, separately for ExAll and for
  ExNext, where the entries of a pass are those its calls returned (ExAll:
  `eac_Entries` of every call, ExNext: one per call that returned an entry).
  It also counts ExAll calls and the entries they returned, starts with a
  pattern, abandoned passes and evictions from the table.
- **Validity test.** The primary test is the one the candidate would use, the
  stamps of the ExNext read-ahead: root and generation of the directory tree,
  the recovery generation, root and generation of the inode tree and the
  pending-inode version (`ExNextBatchCurrent`, `ExNextInode`). The second test
  is the bound's counter, raised by every packet that can change the volume,
  including every close of a file. It is coarser: opening and closing a file
  only to read it raises it, which Workbench does for every icon. Both are
  counted, the stamps decide, and the counter shows what the bound would have
  seen.
- **Scope.** Each start is classified twice: against the latest complete pass of
  the same kind (the ExAll cache is filled by ExAll only), and against the
  latest complete pass of either kind (a cache that ExNext also used). The first
  decides.

### Decision rule

The thresholds were proposed by this plan (commit `db1a0d4`). Fabian accepted
them on 23 October 2026 without change. They are fixed from now on and are not
changed once data exists. All figures are shares of the entries listed by ExAll
and ExNext together, under the stamp test and the same-kind scope.

1. **Enough data.** At least 2,000 entries listed and 100 starts in all. With
   less there is no decision and the session is extended.
2. **Lock-owned cache, the candidate of this plan.** Continue to the second plan
   if the ExAll entries of class B are at least 25 % of all entries. A hit saves
   about 60 % of the time of an ExAll entry and a miss costs a few percent
   more, so 25 % saves at most 15 % of the listing time, and less, because an
   ExNext entry costs several times more than an ExAll entry on the emulated
   machine, so the share of entries overstates the share of time of the hits.
3. **Directory-level cache.** If rule 2 fails and the ExAll entries of classes B
   and C together are at least 40 %, a cache that survives `UnLock` is worth
   planning instead (open question 1), with its standing memory and release
   under shortage. Otherwise stop: a result cache is not worth its invalidation
   code and its memory in real use.
4. **Reported, not decided.** If class D holds at least 30 % of the ExAll
   entries, the volume-wide stamps end the validity too often and a
   per-directory change counter (open question 2) should be looked at before
   either result is final. If the ExNext entries are at least 60 % of all
   entries, the ExAll cache serves the minority of listings, and ExNext is the
   place to look.

### What the numbers will and will not show

They show how one person used one emulated machine for the length of a session,
with the programs and the files of that session. They are not a statistic of the
Amiga world. They count entries, not time, and the stamps of the volume come
from the BFS volume alone. A scripted run checks the counter and is not
evidence of real use.
