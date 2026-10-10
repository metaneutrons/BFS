# Append split and ExNext inode batch pilot scope

Fixed before execution, 10 October 2026.

Fabian asked to make ExAll faster and to improve the first ExNext pass before
the release. After 1,000 empty files are created in one directory, their
1,002 inodes fill 30 leaves of the inode tree, about 33 of 67 keys each:
inode numbers ascend, and every split leaves the left leaf half empty. A
first listing reads about 20 directory and 29 inode leaves from the device.
Two changes, measured separately:

- **Append split** (`src/core/btree.c`): a key past the end of the last leaf
  splits that leaf at its end, so the left leaf keeps all keys but one; every
  other insert splits in the middle as before. The same 1,000 files then
  fill 16 inode leaves. No on-disk format change.
- **ExNext inode batch** (`src/amiga/handler.c`, `src/core/inode.c`): the
  ExNext read-ahead reads the inodes of a gathered batch in one ascending
  batch, as ExAll does. An entry uses its copy only while the inode tree's
  root and generation and a new version counter of the pending inode table
  are unchanged, and reads its inode again otherwise.

## Handlers

Uninstrumented production handlers (`make amiga`, `-O2 -m68020`), built on
the Mac with m68k-amigaos-gcc 6.5.0b:

| Handler | Source | SHA-256 |
| --- | --- | --- |
| `ref` | commit `2a212c8` | `95a26862…` |
| `split` | `2a212c8` plus the append split | `1456564c…` |
| `cand` | `split` plus the ExNext inode batch | `bd16e902…` |

Before this scope was fixed: `make host-test` and `make conformance-test`
pass with a new suite `tests/test_btree_append_split.c` (reverting the split
policy makes it fail) and three tests adapted to full leaves, whose
preconditions assumed half-full ones; the FS-UAE suite on the AROS ROM passes
55 of 55, including a new `exnextfresh_55`, which changes protection bits and
grows a file through an open handle in the middle of an ExNext listing and
fails when the copy's validity check is removed. On the host, callgrind on
Cachy counts for the replica, `ref` against `cand`: ExNext 1,000 × 10
0.839, ExAll 1,000 × 10 0.988, creating 1,000 files 0.982, the single
ExNext pass over 400 entries 1.063.

## Emulator run

Exactly 24 fresh Cachy starts in `compare` mode: eight rounds, each with all
three handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3
first. Within round r the handlers run in the rotation (r − 1) mod 3 of
`ref, split, cand`. Guest `fs-compare-production` (`eb641e32…`), PFS3
(`bc72fac…`), ROM (`68f9d3a…`), formatter (`a34639e…`) and RDB Buffers = 30
are those of the earlier pilots of the day. No retries, exclusions or
extensions; no pooling with earlier runs.

## Retention rule

All 24 starts must pass `emulator-test/verify-bench-results.sh` for
`compare`; otherwise nothing is retained. Paired ratios compare BFS times of
the same round.

- **Append split** is retained if, for `split` / `ref`, the median paired
  ratio of LIST_EXALL_1000_ENTRIES_FIRST_PASS is at most 0.97, the geometric
  mean over all phases of the per-phase medians is at most 1.00, and no phase
  has a median of at least 1.10.
- **ExNext inode batch** is retained if, for `cand` / `split`, the median
  paired ratio of LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL is at most 0.95,
  the geometric mean over all phases is at most 1.00, and no phase has a
  median of at least 1.10.

A change that is not retained is reverted with its tests; the ExNext batch
is judged on top of the split, so if the split is reverted and the batch is
retained, the batch is kept without it. Reported, not deciding: the BFS /
PFS3 ratios of all three handlers and `cand` / `ref`.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
