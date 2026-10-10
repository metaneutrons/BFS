# Write phase confirmation scope

Fixed before execution, 10 October 2026.

In the ExNext read-ahead pilot (`bfs-exnext-read-ahead-pilot-2026-10-10`),
five phases outside the listings had median paired ratios from 1.038 to 1.078
(sequential read and write, small read, small create, append 4 KiB), although
the host counts no extra work in them. Fabian asked for a confirmation run.

## Handlers

Exactly the two handlers of that pilot, unchanged: `reference` (`2c3645ba…`,
retained write-back) and `candidate` (`95a26862…`, plus ExNext read-ahead,
the review corrections and the used-slot count).

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
The handler order is the reverse of the pilot: odd rounds start with
`candidate`, even rounds with `reference`. Guest, PFS3, ROM, formatter and RDB
Buffers = 30 are those of the pilot. No retries, exclusions or extensions; no
pooling with the pilot or earlier runs.

## Reading

For each phase, the eight paired ratios are the candidate's BFS time over the
reference's BFS time in the same round. All 16 starts must pass
`emulator-test/verify-bench-results.sh` for `compare`; otherwise the run is
reported as failed and not interpreted.

A slowdown is confirmed for a phase among SEQ_READ_8M, SEQ_WRITE_8M,
SMALL_READ_40, SMALL_CREATE_40 and APPEND_4K_1M if its median paired ratio is
again at least 1.03. A confirmed slowdown is investigated before further
performance work; nothing is reverted by this run, because the retention rule
of the pilot was met. The ExNext phases are reported to show whether their
gain repeats.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
