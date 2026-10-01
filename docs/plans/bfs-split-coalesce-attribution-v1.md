# Split and free-tree coalescence attribution v1

State: design prepared; no new counter implementation, build, runtime evidence
or optimization acceptance. Qualified checkpoint is `22982d4`, with B-tree
source `5095d6dd…` and normal handler `7ec43316…`. Overall target remains
<=5 times same-run PFS3 for each checked workload, with all guarantees intact.
The combined scratch initializer pilot remains rejected.

## Why measure first

Read-only Luna audit rules out omitting the write in `cow_node`: all thirteen
call sites supply an already-final modified buffer, and no caller rewrites
its returned block. Allocate/journal, final write, retire, then path publication
ordering must remain. The proposed duplicate pre-edit COW write does not exist.

Real repeated work exists in right-side insertion after leaf/internal split:
the split initially writes its right half, then insertion reads, modifies and
rewrites that unpublished block. Retaining the buffer until final insertion
could remove one read/write/CRC per hit, but also removes existing read-back
validation; that needs an explicit design and fault qualification.

Free-tree two-sided coalescence does delete plus update; right-only coalescence
does delete plus insert, with rollback on failure. A height-one root-leaf
single replacement might remove one edit per eligible hit, but must preserve
stock exclusion, absence/adjacency validation, ownership and rollback semantics.
Do not implement either shortcut from aggregate node-write counts alone.

At 4 KiB/28-byte headers, leaf capacities are directory14, inode84, extent254
and FreeTree508. Forty creates imply directory splitting, not a known number
of right-side hits because directory keys are name-hashed. At most128extent
entries for128writes of64KiB into8MiB do not imply an extent split. File deletion
order does not establish physical adjacency around reserve holes. Existing
create75directory writes/write128extent maps/delete197FreeTree writes prove
activity, not shortcut eligibility or elapsed savings.

## Bounded diagnostic design

Use one uniquely named private source copy of the exact qualified checkpoint.
Keep all instrumentation inside `BFS_PERF_PROBE`; never overwrite the normal
guest or prepared baseline images. Reuse one SSOT for added counter names,
snapshot fields and guest emission. Consolidate/reuse existing tree-pointer
role classification rather than adding a second hand-maintained classifier.
Record leaf versus internal, relevant root/deeper shape, and FreeTree/Dir/
Inode/Refcount/Other roles. Bump snapshot ABI12 when extending it and define
an explicitly distinct deep schema from11; producer/consumer version and
size checks must match. Preserve backward verification of prior evidence.

Count split attempts, initial-right-write attempts, right-side selection,
read-back attempts and rewrite attempts at their actual decision/I/O sites.
Count two-sided and right-only coalescence after validation, separately for
all heights and height-one/non-null-root eligibility. Record completion only
after both edits succeed. Attempts are not successful physical writes.

For fault-free completed splits, attempt >= initial write >= right selection
>= read-back >= rewrite, with right selection/read-back/rewrite equal after
success. Role sums and root/deeper sums must match corresponding totals.
Eligible coalescence <= total; completed eligible <= eligible attempts.
Fault/rollback cases need their own equations and must not be filtered away.
Counter savings are upper bounds on removable work, not runtime forecasts.

## Gates and decision

Before actual diagnosis, add bounded counter fixtures: no split; full root
leaf inserting left and right; internal cascade; two-sided root-leaf merge;
right-only root-leaf merge; one-sided/no-neighbor zero hits; deeper merge not
eligible for root-leaf replacement; failure/rollback attempt versus completion.
Prove counters reflect intended paths, not just generated field presence.
Require independent review of sites, SSOT/version/schema and fixture claims.

Two independent normal builds from the private source must remain byte-
identical to qualified7ec43316…; the existing normal guest1fccf207… is not
rebuilt/overwritten. Existing logical gates remain unchanged. Reproduce the
diagnostic handler/guest separately and test strict consumer rejection of
malformed/missing/duplicate/version/overflow/inconsistent counter records.

Then run two fresh bounded diagnostic systems, one per order, on Cachy with
the same ROM, PFS3, formatter, geometry and data-checked workload. Compare all
pre-existing non-time counters to retained controls explicitly, keeping those
noncontemporaneous controls out of normal elapsed acceptance. Persist every
raw record and failure; independently extract actual path hit counts before
primary conclusions. Recoverably archive completed systems and stop only owned
emulators. No new normal-performance claim follows from diagnostic timing.

Select the next optimization only after actual hit counts show a useful work
bound. Its own versioned design, equivalence/fault tests, full gates and fresh
balanced normal comparison remain required. No normal algorithm, validation,
durability, snapshot or recovery change is authorized by a diagnostic result.
No CI, push, PR, merge, release, new user chat, soak or physical qualification.
