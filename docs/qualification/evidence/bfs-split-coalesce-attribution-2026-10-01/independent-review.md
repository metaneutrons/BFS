# Independent review: split/coalesce attribution

Scope: read-only review of the private diagnostic source copy, host fixtures,
and strict schema-12 consumer. No production files were changed by this review.
No builds, emulation, or actual measurements were run independently.

## Instrumentation source

Compared the private files to the qualified tracked sources. Private SHA-256:

- `src/core/btree.c`: `8c51b403bf1d064d97164189cd73219fb8cf77f51367e39a9ba43483e4c2580a`
- `src/core/alloc.c`: `9460027844d8e0224860170171a0fe903fd24ae8706eb6f49a56b3013d1421ae`
- `src/amiga/perf_probe.h`: `67e8bf1e769171c4345737f291e21e5f448ea85862ed992c02ff1591385f26f2`
- `src/amiga/perf_paths.h`: `c9179beea634e146e37b3ee10f1d4c1f3882354f8f27b0910345d0e58aeca0f7`
- `tools/fs-compare-bench.c`: `9a5041041a1da521584821fdc6689e497950fde5213737544fa50f2dd51f5a93`

Tracked comparison identities: `btree.c` `5095d6dd4e9e2984eb88a68779d1f5ea3fbb7e39c3c10a22f32de0479d2cb7da`,
`alloc.c` `b628d72bcc5ba0a8280c801c7ef2c769db9cb0d24523d0ff1eb81055cc06c193`,
`perf_probe.h` `6cff5d94e49cb607be4156d8a1839685900dd5722379e3661b35671718524d7c`, and
`fs-compare-bench.c` `e86d63c8e728a378a2986d4c89220f7fe0af3a57581c76039c6b84ec8bba8d17`.
`src/amiga/perf_probe.c` and `src/amiga/handler.c` are byte-identical to the
reviewed tracked versions (`4448951b…` and `5f8e2532…`).

No source blocker found. Split hooks distinguish threshold attempt, first
right-node `node_write` call, right selection, right-node read call, and rewrite
call. They are at the intended control-flow sites; write/read event counters
are not proof of successful or physical media I/O. Leaf root/deep uses tree
height; internal root/deep uses the current root path position / validated node
level. Coalesce attempts are recorded before the first edit; completion is
recorded only after both edits return `BFS_OK`. Shape is captured before the
edit. Completion does not imply durable commit. `bfs_perf_tree_role` is the
single pointer classifier reused by `node_write` and matrix counters, with the
same fallback to `OTHER_TREE` when the probe filesystem does not own the tree.

The SSOT macros produce the enums, matrix dimensions, and guest row names:
100 split plus 40 coalesce rows per phase. Probe ABI 13 and deep schema 12 are
consistent between header, handler snapshot copy, guest reader, and emitted
header. Whole-snapshot reset uses `memset(..., sizeof(snapshot))`. The matrices
add 140 four-byte `ULONG`s (560 bytes); the target-size check reported 536 to
1096 bytes total. Current fixed names fit their buffers, and the default handler
stack is 32 KiB. Core behavior changes are confined to `BFS_PERF_PROBE`; no
normal-path control/data-flow change was found.

## Host fixtures

Frozen inputs reviewed by SHA-256:

- `fixtures.c`: `7331d4be6fa14fd92cb3e85265f6064c2bd8907374ac4cb0ce7ae2a984dfce6f`
- `probe_stubs.c`: `6c544b527de56be64eec249da392d4a662af37cbab71e4493a1588050760ceaf`
- `replay.sh`: `09b2de8a2e64c3f3059fc41fc5d917c86c281922de77831c268d9b4d774c0662`
- fake Amiga type headers: `types.h` `c460f40a63dd621a5d42ff5b267bffbc82874ea00775838f8b5e13402d995031`,
  `timer.h` `cde7e2c77454ae3be66e61ae0ecafa90f19f76f48e7ff835d3600734e7d47a1f`.

The matrix oracles inspect every cell, including expected zeros. Scenarios cover
non-split and left/right leaf splits; split allocator failure; deep and root
internal cascades; root-leaf two-sided and right-only coalesces; a singleton
root transition; left-only/no-neighbor controls; a deep two-sided coalesce; and
first/second-edit failures with rollback expectations. They use public B-tree
insert/search and freespace operations for the measured actions. Retained logs
report 13 cases / 974 assertions passing in normal and ASan+UBSan builds; the
replay compiles all private core translation units with the probe enabled. This
is reported log evidence, not an independently executed build.

Limits: the host timing stubs are inert; this provides no Amiga timing or
mounted-handler qualification. The fixture uses 4 KiB blocks, a 512-byte key,
and 32-bit `ULONG`; it directly deletes a free-tree entry and adjusts
`total_free` to set up the singleton-root case. It is not a full mounted-image
allocator scenario. Direct role fixtures exercise `FREE_TREE` and
`INODE_TREE`, not every role. The coalescing success oracles confirm the merged
extent exists but do not individually assert removal of every old neighbor;
fault cases check the named pre-existing extents and accounting. Fault BIOs
fail whole writes, not partial writes.

## Strict consumer

Final frozen SHA-256:

- `consume.py`: `704eeec4eb970afaff5b6778accc0ecd02b8b82389321d0398163482bb00f483`
- `test_consumer.py`: `9ce6efa4f057cda1d181cfbf028f85f6ef5ce5e9cf6b14b77bab5018d23feed6`
- `fixture-run.log`: `79df0c01b933528b8f2804d60dca6779c2feaaa89353aca76f5db1de6c3d032b`

No remaining consumer blocker found. It derives dimensions and phases from the
header/verifier, requires ordered complete schema-12 matrices, validates
unsigned widths (including phase-prefixed `_CLOCK_PAIR_TICKS` as 32-bit),
preserves the schema-11 verifier through a projection, checks split event
chains and per-role split-write budgets, and rejects coalescing roles
`DIR_TREE`, `INODE_TREE`, and `REFCOUNT_TREE` while retaining the valid
`FREE_TREE`/`OTHER_TREE` cases. The latter distinction is intentional: the
mounted free-space path classifies as `FREE_TREE`, but standalone or untracked
free-space objects can legitimately fall back to `OTHER_TREE`. The output's
non-time projection excludes both `_US` and `_TICKS` rows. The synthetic suite
reports 23/23 acceptance/rejection cases passing, including these guards.

Coverage limits: there is no dedicated negative synthetic case for the
attempt-versus-initial-write equality check, though the consumer implements
that check and the host fixture covers the failed-allocation attempt-only
case. Synthetic tests do not mutate malformed SSOT source macros. These are
test-coverage limits, not blockers in the reviewed frozen consumer.
