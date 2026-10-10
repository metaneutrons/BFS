# BFS write phase confirmation

A second run of the ExNext read-ahead pilot's two handlers, with the handler
order reversed, confirms a slowdown only for appending 4 KiB packets
(median paired ratio 1.039, after 1.045 in the pilot). Sequential read and
write, small read and small create, which were 1.038–1.078 in the pilot, are
0.937–0.967 here. The ExNext gain repeats (0.726 and 0.749 for 1,000 and 400
entries ten times).

## Run

The [scope](evidence/bfs-write-phase-confirmation-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: 16 fresh `compare` starts on Cachy with the
pilot's handlers (`reference` `2c3645ba…`, `candidate` `95a26862…`), BFS first
in odd rounds and PFS3 first in even rounds, the candidate first in odd
rounds; guest, PFS3, ROM, formatter and RDB Buffers = 30 unchanged. All 16
starts pass `emulator-test/verify-bench-results.sh`. The series ran
12:52:55–13:02:27 UTC on 10 October 2026; runner and Coffin were restored with
status 0. Nothing is pooled with the pilot.

| Phase | Pilot | Confirmation (range) | Confirmed (≥ 1.03 again) |
| --- | ---: | --- | --- |
| Append 4 KiB × 256 | 1.045 | 1.039 (0.98–1.23) | yes |
| Sequential write 8 MiB | 1.078 | 0.958 (0.68–1.28) | no |
| Small read 40 | 1.063 | 0.967 (0.53–1.17) | no |
| Small create 40 | 1.042 | 0.940 (0.93–1.20) | no |
| Sequential read 8 MiB | 1.038 | 0.937 (0.73–1.06) | no |
| ExNext 1,000 × 10 | 0.745 | 0.726 (0.65–0.78) | |
| ExNext 400 × 10 | 0.734 | 0.749 (0.69–0.84) | |

The geometric mean over all phases is 0.905. The full table is in
`summary.json`.

## Investigation of the append slowdown

At 1.04 of about 212 µs, appending costs roughly 8 µs more per 4 KiB Write
packet. What changed between the handlers on that path:

- The packet dispatcher: gcc inlines the ExNext read-ahead into
  `HandlePacket`, whose frame grows from 904 to 908 bytes with the same saved
  registers; the Write case is unchanged.
- The core: inode reads test the used-slot count; a write through another
  path drops the pending copy after instead of before the tree operation;
  `bfs_btree_update` became a wrapper around a function shared with the
  commit's headroom variant.
- The host replica counts 3,445,053 instructions for the phase before these
  corrections and 3,445,784 after, three per packet.

None of these explains 8 µs per packet. The cause is not established; code
layout in the emulated 68040 remains a candidate. Appending 1 KiB packets,
which runs the same path, is 1.019 and 0.996 in the two runs.

## Consequence

The slowdown of appending 4 KiB packets is small but reproduced; the other
four phases are not. Attributing it needs the probe builds of both handlers
in `deep-compare`, which can split the Write packet into its scopes. The
read-ahead and the corrections stay.
