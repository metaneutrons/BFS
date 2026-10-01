# Split and free-tree coalescence attribution v1

State: bounded diagnostic implementation and measurement completed. The
[qualification report](../qualification/bfs-split-coalesce-attribution-2026-10-01.md)
records two fresh opposite-order systems, passing gates, identical path matrices,
and explicit retained-counter equivalence. Only two right-side directory leaf
split paths occur during create; all measured coalescence paths are zero.
Neither proposed shortcut has a substantial work bound in these workloads;
neither is implemented or accepted. Qualified production B-tree remains
`5095d6dd…`, normal handler `7ec43316…`. Overall target remains <=5 times
same-run PFS3 for each checked workload, with all guarantees intact, and is
unachieved. The combined scratch initializer pilot remains rejected.

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

At 4 KiB/28-byte headers, leaf capacities are directory 14, inode 84, extent 254
and FreeTree 508. Forty creates imply directory splitting, not a known number
of right-side hits because directory keys are name-hashed. At most 128 extent
entries for 128 writes of 64 KiB into 8 MiB do not imply an extent split. File deletion
order does not establish physical adjacency around reserve holes. Existing
create 75 directory writes/write 128 extent maps/delete 197 FreeTree writes prove
activity, not shortcut eligibility or elapsed savings.

## Bounded diagnostic design

Use one uniquely named private source copy of the exact qualified checkpoint.
Keep all instrumentation inside `BFS_PERF_PROBE`; never overwrite the normal
guest or prepared baseline images. Reuse one SSOT for added counter names,
snapshot fields and guest emission. Consolidate/reuse existing tree-pointer
role classification rather than adding a second hand-maintained classifier.
Record leaf versus internal, relevant root/deeper shape, and FreeTree/Dir/
Inode/Refcount/Other roles. Bump snapshot ABI 12 when extending it and define
an explicitly distinct deep schema from 11; producer/consumer version and
size checks must match. Preserve backward verification of prior evidence.

The extension must bump snapshot ABI from 12 to 13 and deep TSV schema from 11
to 12, not reuse the currently qualified versions. Old evidence keeps its
original ABI/schema and backward validation.

Count split attempts, initial-right-write attempts, right-side selection,
read-back attempts and rewrite attempts at their actual decision/I/O sites.
Count two-sided and right-only coalescence after validation, separately for
all heights and height-one/non-null-root shape eligibility sampled before
either edit. A right-only delete can empty a singleton root before insertion;
later height/root checks would misclassify that originally eligible case.
Record completion only after both edits succeed using the saved pre-edit
shape. Shape eligibility is necessary, not proof that every future shortcut
precondition holds. Attempts are not successful physical writes.

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
Include right-only singleton-root replacement and failure/rollback after its
first edit leaves an empty tree, keeping pre-edit eligibility explicit.
Prove counters reflect intended paths, not just generated field presence.
Require independent review of sites, SSOT/version/schema and fixture claims.

Two independent normal builds from the private source must remain byte-
identical to qualified `7ec43316…`; the existing normal guest `1fccf207…` is not
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

## Diagnostic disposition

Do not pursue split-buffer reuse or two-edit coalescence fusion as the next
performance change for this workload. Four directory leaf splits produce only
two right-side read/rewrite hits: at most two removable node writes among 548
create node writes (0.365% by count), not an elapsed forecast. No internal
split, sequential-write split, or root/deep two-edit coalescence is observed.
Zero hits do not establish an unreachable branch or cover other geometries.

Next diagnosis should attribute the frequent non-split allocation/free edits
and their metadata-write costs. Existing FreeTree write counts 353 (create),
142 (sequential write), 197 (delete) identify activity, not a removable operation.
Do not omit `cow_node` writes or weaken validation/commit semantics. Any new
shortcut still needs its own guard/rollback design, equivalence/fault gates,
and fresh balanced normal elapsed comparison before acceptance.
