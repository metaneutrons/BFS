# Release flags pilot scope

Fixed before execution, 10 October 2026.

The production baseline pilot of the same day found the 68040 release build
(`make release`: `-Os -m68040`) slower than the measured `-O2 -m68020` build in
every phase, by 11.8 % in the geometric mean. That build differs in two
flags. Fabian asked to separate them and, depending on the result, to change
the release build.

## Handlers

All three are built on the Mac with m68k-amigaos-gcc 6.5.0b from the same
tree: commit `5136708` plus the big-endian load change retained earlier the
same day. Apart from the two flags, the release flags (`AMIGA_BASE_FLAGS`)
and the `make amiga` flags are identical.

| Handler | Flags | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `o2-020` | `-O2 -m68020` (`make amiga`, as measured so far) | 171,240 | `ea37a89d…` |
| `o2-040` | `-O2 -m68040` | 173,548 | `bcc9969b…` |
| `os-040` | `-Os -m68040` (`make release` today) | 140,864 | `5a730937…` |

For the other release CPUs, `-O2` instead of `-Os` grows the handler from
136,208–138,960 to 167,800–171,240 bytes.

## Emulator run

Exactly 24 fresh Cachy starts in `compare` mode: eight rounds, each with all
three handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3
first. Within round r the handlers run in the rotation (r − 1) mod 3 of
`o2-020, o2-040, os-040`. Guest, PFS3, ROM, formatter and RDB Buffers = 30
are those of the production baseline pilot. No retries, exclusions or
extensions; no pooling with earlier runs.

## Decision rule

For each phase, the eight paired ratios are `o2-040`'s BFS time over
`os-040`'s BFS time in the same round. The release builds of every CPU switch
from `-Os` to `-O2` if all of the following hold:

1. All 24 starts pass `emulator-test/verify-bench-results.sh` for `compare`.
2. The geometric mean over all phases of the per-phase median paired ratio is
   at most 0.97.
3. No phase has a median paired ratio of at least 1.10.

Otherwise the release flags stay unchanged. The emulator runs a 68040 only;
the switch applies the measured optimization level to the other CPU builds
without measuring them.

Reported, not deciding: `o2-040` over `o2-020` per phase (the effect of the CPU
flag) and the BFS / PFS3 ratios of all three handlers.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
