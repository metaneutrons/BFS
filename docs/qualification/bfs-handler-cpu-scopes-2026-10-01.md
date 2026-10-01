# BFS handler and shared-core gross runtime attribution

The five-times PFS3 goal remains unachieved. This increment changes diagnostic
instrumentation only. Its normal handler is byte-identical to the timestamp
baseline. Four fresh serial AmigaOS runs pass all eight filesystem-output
data, EOF, completion and strict counter checks. No production speedup is claimed.

## Measured scopes and decision

Means below are inclusive EClock microseconds across the two extended probes.
The core columns are separate observed functions, not exclusive cost buckets.

| Workload | Packet | Create core | Write core | Delete core | Sync | Seal | Interface free |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 227827.44 | 78116.92 | 70223.39 | 0 | 63552.77 | 36708.16 | 15722.91 |
| Write 8 MiB | 395565.70 | 2432.41 | 381267.98 | 0 | 1888.27 | 1056.56 | 61040.71 |
| Delete 40 | 198489.81 | 0 | 0 | 117955.99 | 68442.96 | 42144.61 | 415.86 |

Large write remains predominantly inside the common core write operation, not
the final commit. Small creation includes meaningful create, write and sync
work; deletion includes meaningful namespace and sync work. Interface allocation
and public free-space allocation remain about 39–65 ms in these mutating phases,
but overlap each other and the core/CRC/heap/device scopes. Direct B-tree heap
intervals remain small (approximately 1.1/7.3/2.5 ms for create/write/delete),
not a sufficient next primary target. This is prioritization, not exclusive CPU
accounting, a speedup forecast or an Amdahl bound for the normal handler.

Every extended order agrees on packet counts: create 120 (40 open/write/end),
lookup 800 other, small read 160 (40 open/end, 80 read), write 130 (one open/end,
128 write), read 131 (one open/end, 129 read), delete 40 delete. Flush category
counts are zero for these checked workloads; do not invent a separate packet
for the guest Flush call. Core create/write/sync counts are 40/40/40 for small
create and 1/128/1 for large write; delete core/sync counts are 40/40. Interface
free counts are 40/255/1 for create/write/delete. Seal counts are 40/1/40.
Read/lookup phases record no mutations. All call/sample equalities and the
exact seven-category packet count/sample/tick partitions pass.
Independent Luna extraction, performed before reading the primary JSON, agrees
with every raw metric, paired elapsed delta and scope tick/clock conversion.
The retained review record is a primary transcription of the reviewer response,
not a fabricated command transcript.

Scopes measure wall intervals, including nested work and possible reply/task
scheduling. Do not add overlapping columns or subtract them from elapsed to
invent a residual. File-write covers stamped/un-stamped common public writes,
not direct unlocked or append APIs. Seal ends at its return, before later
commit fences. Invalid/error returns are timed too; in_alloc recursive allocator
free returns are excluded from the top-level free scope. Diagnostic reset/read
packets bypass packet timing. One elapsed value records packet and its category.

The next bounded experiment is
[native-word-order CRC assembly](../plans/bfs-crc-native-word-order-v1.md),
with independent algebraic and real-68k ABI/vector evidence before broad
qualification. CRC stride-64 samples do not isolate total CRC cost, so this
kernel experiment is not a claim that CRC dominates or can solve the full gap.
No rejected allocator policy is reinstated.

## Controls, raw observations and limitations

Fixed run sequence: control BFS-first, extended PFS3-first, control PFS3-first,
extended BFS-first. Every partition is fresh. Old controls use ABI 11/schema 10;
extended runs use ABI 12/schema 11 and the matching new guest. Normal compare
schema 1 and all workload operations remain unchanged.

All elapsed tuples are microseconds in create, lookup, small-read, write8MiB,
read8MiB, delete order.

| Run | BFS tuple | PFS3 tuple |
| --- | --- | --- |
| Control BFS-first | 276286, 136959, 57153, 411261, 557117, 178624 | 16442, 66560, 14144, 33343, 271616, 8065 |
| Extended PFS3-first | 274175, 112384, 50431, 409983, 515325, 204991 | 18752, 68416, 15104, 38784, 265919, 7857 |
| Control PFS3-first | 229373, 109439, 49791, 398270, 513662, 183743 | 33280, 95871, 14783, 36287, 263232, 8256 |
| Extended BFS-first | 195327, 111681, 47872, 386814, 523645, 199551 | 17471, 66623, 15489, 35775, 257918, 8000 |

Same-order extended/control changes are −29.303/+19.532% create,
−18.457/+2.691% lookup, −16.239/+1.285% small read, −5.944/+2.941% write,
−6.008/+0.324% read and +11.716/+11.564% delete (BFS-first/PFS3-first).
Adverse observations are retained, not rerun away. This variability, substantial
PFS3 changes and only two observations per probe preclude unbiased overhead,
causal precision, normal-handler acceptance or hardware throughput claims.

Both extended clocks are 709379 Hz; CPU stride is one and CRC stride remains 64.
Calibration is too coarse to subtract exact instrumentation overhead. Existing
heap and allocation scopes include nested probe overhead. Timing categories
partition the recorded parent interval, not all guest end-to-end work.

ROM/Workbench/PFS3/formatter, options=0, 4 KiB BFS blocks, 30 buffers,
255.5 MiB partitions, Cachy FS-UAE 3.2.35 A1200/68040 configuration match the
retained inputs. All images were prepared before timing, with no remote compiler
or second emulator. Physical-host scheduling remains uncontrolled. Completed
images were archived recoverably under
`/home/fabian/.cache/bfs-performance/handler-cpu-scopes-2026-10-01/`;
all eight before/after TSV hashes agree. Original local images remain.

## Qualification and reproduction

Normal and actual ASan/UBSan host regressions each pass 48 suites / 460 cases.
They exercise unchanged production code, not the Amiga-only wrappers.
Leak detection is explicitly disabled. Independent packet/core/guest/builder
reviews, warning-strict Amiga builds and the real checked guest runs qualify
the diagnostic increment. Normal/probe/guest binaries reproduce byte-for-byte
in two build directories. No new Linux/FUSE qualification is claimed for this
normal-preprocessing-preserving diagnostic; actual production optimization
will require its planned Linux/FUSE gates.

The final quality/static gate passes 78 tests, ShellCheck, actionlint and clang
analysis. The verifier's 34 tests retain old deep schemas 4–10, normal/schema 1
and existing data/counter equations. Schema 11 requires 42 scope rows per phase,
stride one, exact call/sample equality, zero ticks with zero calls, uint32
call/sample and uint64 tick bounds and exact decimal packet partitions.
Missing/duplicate/malformed/mixed-version/overflow/precision/partition errors
have negative oracles; legal integer maxima pass.

An initial verifier run failed 18 of 31 tests on an AWK multiline-expression
parse error, not semantic checks. Its log is preserved. A minimal layout repair
passed 31 tests; independent review then found absent integer bounds that could
accept AWK-rounded >2^53 call/sample mismatches. The final exact-decimal range
fix passes 34 tests and independent review. Earlier logs and the pre-range-fix
75-test quality result remain explicitly non-final evidence.

[Raw text evidence](evidence/bfs-handler-cpu-scopes-2026-10-01/) includes every
TSV, configuration, guest completion/inventory, build/gate/failed-attempt log,
source patch against `6598d19`, identities, extraction and reproduction scripts
and SHA256SUMS. Copied log trailing horizontal whitespace/final blank lines are
normalized; raw TSV and unified-diff context bytes remain unchanged. No binary,
HDF or licensed asset is committed. The optional BFS_BENCH_GUEST_FILE builder
input selects matched guests without overwriting the retained default binary.

Normal handler SHA256:
`572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`.
Extended probe:
`289a9f411a6e0ea19ae7f99a1645e53a98d48eb7979b5dc329f7ff0adba1d6dd`.
Extended guest:
`5126e8640256e19bc2f5661d8b735f7961c4882edf3d8046b26bd2badfc86277`.
Control probe:
`070b162d85ebbe2aa67774b48dea2543db7b0e864270c1c05096680498660389`.
Control guest:
`1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80`.
The identity/preflight logs retain full source and fixed-input hashes.

No CI, push, PR, merge, release, external publication or hardware qualification
was performed. coffin.service remains inactive, not disabled.
