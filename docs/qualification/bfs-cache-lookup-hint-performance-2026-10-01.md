# BFS cache lookup hint performance

The checked resident-slot hint is rejected as the default: its eight fresh
normal comparisons do not show a sufficiently consistent benefit. Production
cache and header are restored exactly to `358f125`, whose production source is
unchanged from `8b35eec`. The five-times PFS3 goal remains unachieved. This is
an evidence-based rejection, not a proven general regression or a safety fault.

## Normal measurements

Means below use four fresh runs per revision, balanced by filesystem order.
Microseconds are elapsed time; negative changes mean lower candidate time.

| Workload | Baseline BFS | Candidate BFS | Change | Candidate PFS3 | Ratio of candidate means |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 282511 | 285262.25 | +0.97% | 17856.25 | 15.975 |
| Lookup 400 | 110495.5 | 114382.75 | +3.52% | 71500.75 | 1.600 |
| Small read 40 | 52335.75 | 46031.5 | −12.05% | 14223.75 | 3.236 |
| Write 8 MiB | 411853.25 | 404141.75 | −1.87% | 40338.25 | 10.019 |
| Read 8 MiB | 516685 | 537212.75 | +3.97% | 310654.75 | 1.729 |
| Delete 40 | 223755.75 | 194326.25 | −13.15% | 11311.5 | 17.180 |

Ten of 24 order/repeat-matched BFS changes are adverse. Every candidate
create, write and delete remains above five times its same-run PFS3 time;
lookup and both read phases remain below five in these four observations.
The 421372 µs candidate create is retained, as are all slower baseline and
PFS3 observations. No sample is trimmed. The
[complete measurements](evidence/bfs-cache-lookup-hint-2026-10-01/measurement-summary.md)
give chronological raw values, matched changes, all same-run ratios and
threshold failures. Independent Luna extraction and primary calculations agree.

Four observations per revision do not establish statistical or causal precision.
In particular, PFS3 variation cannot be attributed to the BFS source change.
The code avoids repeated array scans, but that fact alone does not establish
an elapsed-time improvement. Mixed normal results are insufficient to retain
the additional hint state in pursuit of the present target.

## Candidate and proof boundaries

The candidate adds one uint32_t index field, not another heap buffer. Every
lookup bounds-checks that index and compares the actual resident block ID;
mismatch falls back to the original first-match linear search. Reads,
validation callbacks and successful writes share the lookup. Insertion records
the victim index; failed writes invalidate every matching slot as before and
clear the hint. Invalidate/destroy/reinit clear it. No byte copy, CRC, validation
context comparison, COW write, reclamation guard, I/O, fence or flush is removed.
Complete-operation serialization, no callback reentry and unique resident
block IDs remain the existing cache contract.

Independent production/test review found no blocker. The missing retained-node
write to an already resident slot was added and independently reviewed before
the final gates. Six focused tests cover capacities 1/2/30/128, resident bytes,
stale/out-of-range hint fallback, LRU replacement, both write paths, failed and
partial writes, all five validation-context fields, invalidation, lifecycle
and separate caches. Observable hit tests do not alone prove a linear scan
was avoided; that property is established by direct source inspection.

The candidate passed 46 host suites with 449 tests in both normal and actual
ASan/UBSan builds, 70 local quality tests plus shell/static checks, normal and
probe m68k builds, and 20 Linux conformance tests plus actual FUSE qualification.
Separate clean m68k builds reproduce both binary hashes. The first attempted
sanitizer run used the wrong make variable and compiled ordinary O2 binaries;
it is retained but is not sanitizer evidence. The v2 log shows the real flags.

After rejection, six byte/validation/write-failure tests are adapted to the
original resident-cache contract without any hint field, assignment or oracle.
The complete candidate-only test source remains in evidence. The adapted
tests add coverage, not permission to change the restored production policy.
An initial mechanical test-runner rename failed compilation and was corrected;
only the subsequent successful builds can qualify the retained test source.
The restored source with those adapted tests passes all 46 suites and 449 tests
in normal and ASan/UBSan builds, 70 local quality tests plus shell/static checks,
and 20 Linux conformance tests plus actual FUSE qualification. Independent
review confirms that the adapted assertions remain meaningful without a hint.

## Reproduction and evidence

Baseline normal handler SHA-256 is
`c4d4241a45b84c768ec9498e77144e650812e31576c952925f4ed1de65be0d5a`.
Candidate normal is
`c832040b679669bda0de4e88c047da078d05dcc08624ebf7e6cfcca0b2f0e16e`;
candidate probe is
`ec87e9c231a51d4863f8fdd3a48421572e716f4b30d4f95fe276651b2d602031`
and was built, not measured. Candidate cache/header identities and the exact
zero-context patch are preserved. Replay the production-only patch with
`git apply --unidiff-zero` against `358f125`; the archived candidate tests need
that header, not the restored header.

All eight normal images were prepared before serial timing after the final
functional gates. Input hashes are checked before guest execution. The normal
guest `1fccf207…`, formatter, ROM, Workbench, PFS3, options=0, 4 KiB BFS geometry,
255.5 MiB partitions and Cachy A1200/68040 FS-UAE configuration are unchanged.
The measured RDB requests 30 cache slots. Preflight found no other guest emulator
or compiler; none was started concurrently by this workflow. Physical-host
scheduling is uncontrolled. Completed geometry images were
moved to a recoverable home-directory archive to free tmpfs, not deleted.

[Raw evidence](evidence/bfs-cache-lookup-hint-2026-10-01/) preserves all sixteen
TSVs, completion/phase files, configurations, image preparation and runner logs,
functional gates, source/binary input identities, candidate patch/test source
and SHA256SUMS. Timing TSVs are unmodified; only copied log trailing horizontal
whitespace is removed. No binary, HDF or licensed asset is committed. No CI,
push, PR, merge, release or device-bound hardware qualification ran.
