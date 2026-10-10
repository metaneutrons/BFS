# BFS inode write-back bound

A handler whose file handle writes its inode only when it is closed bounds
what inode write-back within a transaction can gain
(`docs/plans/bfs-inode-write-back-v1.md`). On the emulated 68040, appending
4 KiB packets falls to 0.765 of the reference (2.10 to 1.64 times PFS3),
1 KiB packets to 0.720 (2.56 to 1.83) and the 8 MiB sequential write to
0.705 (1.08 to 0.73). Workloads that write each file with one packet do not
change.

## Prototype

Both handlers are uninstrumented production builds (`-O2 -m68020`) of commit
`5136708` with the retained big-endian load change.

- `reference` (`ea37a89d…`): that tree as is.
- `bound` (`c4fc8efc…`): the same tree with `defer-inode.patch`. After the first
  Write packet of a handle, later packets neither read nor write the inode;
  ACTION_END writes it. A commit between packets persists a stale inode, so the
  handler is unsafe and only a measuring device.

The bound omits the table lookup on every inode read, the flush before every
commit and the write-through of other inode changes. It overstates the gain.

## Run

The [scope](evidence/bfs-inode-write-back-bound-2026-10-10/PILOT_SCOPE.md) was
fixed before the run: 16 fresh `compare` starts on Cachy, eight rounds of both
handlers, BFS first in odd rounds and PFS3 first in even rounds, handler order
alternating; guest, PFS3, ROM, formatter and RDB Buffers = 30 of the
production baseline pilot. All 16 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 11:24:47–11:34:22 UTC
on 10 October 2026; runner and Coffin were restored with status 0. Nothing is
retained or reverted.

## Results

| Phase | BFS / PFS3 reference | bound | bound / reference (range) |
| --- | ---: | ---: | --- |
| Append 4 KiB × 256 | 2.10 | 1.64 | 0.765 (0.65–0.86) |
| Append 1 KiB × 256 | 2.56 | 1.83 | 0.720 (0.48–1.02) |
| Sequential write 8 MiB | 1.08 | 0.73 | 0.705 (0.52–0.99) |
| Small create 40 | 1.75 | 1.90 | 0.945 (0.90–1.14) |

Per Write packet, appending 4 KiB costs 273 µs with the reference and 209 µs
with the bound (PFS3 134 µs); 1 KiB 232 and 169 µs (PFS3 92 µs). The other
phases have median paired ratios from 0.962 to 1.017. Small create writes
each file with one packet; its paired median is 0.945, and its BFS / PFS3
medians (1.75 and 1.90) differ more than that because the PFS3 times of the
starts scatter. The full table is in `summary.json`.

## Consequence

Inode write-back removes about a quarter of the append time and a third of
the sequential write time on the 68k. It is implemented next, under the plan's
safeguards; the remaining append cost is the allocation and mapping of each
new block.
