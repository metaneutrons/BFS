# BFS exact key fast path qualification

The exact-key early-return candidate is rejected and removed from production
source. Its complete eight-run normal comparison does not demonstrate the
intended small-directory gain and shows regression signals elsewhere. The
previous leaf-range implementation is restored byte-for-byte. Format v3,
integrity checks, snapshots, recovery, cache capacity and commit policy remain
unchanged. The <=5x PFS3 target remains open.
This local work uses `fix/metadata-listing-performance`, based on `07216b7`;
no commit, push, merge or remote CI is included.

## Rejected candidate

The candidate accepts an exact slot only when the cache owner, BIO and layout match,
and the slot key, root, generation and valid BIO mutation epoch are current.
A fresh validated resident leaf must have the correct level, a valid index
and the requested key. The current value is copied before another BIO call;
ordinary inode validation follows unchanged. Tree-shape validation still
precedes all shortcuts. Missing or saturated epochs cannot authorize a hit.

An exact miss runs the previous range preparation, including identity
invalidation and route reset. Only range bookkeeping is skipped on a successful
exact hit. No cache storage or ABI is added. Tree/BIO lifetime changes still
require reset; pointer equality is not an incarnation token.

GCC 6.5.0b inlines both helpers on m68k; both search versions use the same
1,068-byte frame and saved registers. Hits avoid preparation, but eligible
misses add an owner/BIO/layout comparison set before preparation repeats the
identity checks. Assembly establishes this mechanism, not its elapsed cost.

## Correctness evidence

The new oracle deliberately makes range-only bookkeeping stale while retaining
a valid exact slot, disables the legacy last-leaf shortcut, and checks that
the lookup neither allocates a traversal buffer nor prepares the route table.
An absent-key miss must repair the route state and traverse normally. Against
the previous ordering this oracle fails at the expected route-count assertion;
the other eleven hint tests pass. With the candidate all twelve pass.

Complete Clang host and ASan/UBSan suites pass 566 tests across 60 suites each.
The final sanitizer invocation enables leak detection and halt-on-error.
The 113 quality tests, quality-gate counter-probes, 21 conformance tests,
core/host static analysis, m68k production/probe builds, 52 Amiga integration
checks and nine Kickstart compatibility scenarios pass. Luna's independent
review found no actionable correctness or evidence-tool defect.

After removal, the core and hint-test digests match the previously qualified
leaf-range source. Fresh complete host and sanitizer reruns on that identical
checkout pass 565 tests across 60 suites each. Rebuilt production/probe handler
digests also match the previous qualified binaries. The rejected candidate's
additional oracle survives in the archived patch, not in the retained suite.

An initial host binary was built before the new oracle was exported and
reported 565 tests. After forcing its rebuild, the final host suite includes
all 566. Earlier logs remain separate. The isolated export has no Git metadata,
so its Git-dependent gate script could not run; the same gates pass in the
local repository. Neither is a production-code failure. The intentional
old-order oracle failure is retained as a counter-probe, not relabeled as a
successful candidate run.
An uncached restoration-check build was also stopped and replaced with the
already identified build directories; the partial attempt log is retained.

## Production result and rejection

All 24 normal runs verify: eight fresh runs each for M3, the prior leaf-range
handler and the candidate. All eight pilot runs also verify separately. The
table shows paired BFS elapsed-time ratios; values below one favor the
candidate. Every phase and outlier remains in `normal-only-summary.tsv`.

| Phase | Candidate / previous range median | Pair range | Candidate / M3 median |
| --- | ---: | ---: | ---: |
| ExAll 40 repeated | 1.0035 | 0.8315–1.3350 | 1.0316 |
| ExAll 400 repeated | 1.0701 | 0.8980–1.1679 | 0.9204 |
| ExAll 1,000 repeated | 1.0007 | 0.9560–1.0913 | 0.9191 |
| ExNext 1,000 repeated | 1.0115 | 0.9325–1.2007 | 0.9179 |
| Append 1 MiB in 4 KiB steps | 1.0646 | 0.6726–1.6775 | 1.0799 |
| Append 256 KiB in 1 KiB steps | 0.9735 | 0.7150–1.0319 | 1.0127 |
| Read 8 MiB control | 1.0041 | 0.8703–1.2650 | 0.9925 |
| Write 8 MiB control | 0.9510 | 0.5552–1.1023 | 1.0547 |

The intended warm-40 gain is absent: its paired median is essentially neutral
against the previous range build, and it remains 3.16% slower than M3.
The 400-entry ExAll median is 7.01% slower (five of eight pairs), and 4 KiB
append is 6.46% slower (six of eight). The favorable 1 KiB append and write
means do not justify a universal speedup claim; controls and several mutating
phases vary substantially. Large-listing gains against M3 belong to the
existing leaf-range work, not to this early-return change.

Candidate ExAll 400/1,000 median BFS/PFS3 factors are 18.62/18.65, with all
eight runs over five. ExNext 400/1,000 medians are 5.17/5.36, with six/seven
runs over five. No target closure follows from these normal results.

The remaining series was stopped once the complete normal comparison failed
to support retention. Two durable baseline runs completed; the candidate's
first durable run was interrupted. All three directories are retained, with
no paired durable qualification claim. No final diagnostic run was started.
The planned 50-run inventory is therefore incomplete; the default summary
correctly rejects it. `--normal-only` strictly checks the 24 completed normal
runs and labels its output NOT FULL QUALIFICATION. This is a rejection on
completed normal evidence, not acceptance with reduced qualification criteria.

## Measurement scope

The pinned M3 and previous leaf-range handlers are compared with the candidate
on fresh images. Production qualification requires eight normal and eight
durable runs per handler, plus two candidate diagnostic runs: 50 runs in total.
The eight two-run pilots are separate and cannot qualify retention. Filesystem
order alternates BFS-first/PFS3-first; each repeat uses the same order across
handlers. Every run, phase and outlier is retained.

The schema4 production guest, schema14 diagnostic guest, formatter, PFS3,
Kickstart, Workbench commands and volume geometry are unchanged from the
[leaf-range qualification](bfs-leaf-range-performance-2026-10-08.md).
Cachy KVM runs FS-UAE 3.2.35/Xvfb, A1200/68040 at maximum speed, 2 MiB chip
and 8 MiB fast memory. Fresh 256 MiB RDB images use 4 KiB blocks and 30 BFS
buffers. Heavy tests and other emulators finish before timing starts; host
scheduling remains uncontrolled.

The six real listing fixtures total 2,880 empty files. ExAll requests
ED_COMMENT but the files have no comments; this does not qualify nonempty
comment retrieval. First pass after setup and ten additional passes remain
separate; neither is a handler-cache-cold or device-cold claim. Durable mode
includes flush only within mutating phases. Diagnostic nested sample scopes
are not exclusive CPU times or forecasts of production elapsed improvement.

## Reproduction and identities

The [evidence directory](evidence/bfs-exact-key-fastpath-2026-10-09/) retains
the bounded rejected patch on top of the previous identified source, correctness
logs, interrupted runs and a strict fixed-inventory runner/summary. The summary reuses
the prior immutable parser and verifier without modifying earlier evidence.
Installed-copy digests are recorded after measurement, not extracted from the
embedded RDB module. Licensed assets, HDFs and executable inputs remain private
and outside the repository.

| Artifact | SHA-256 |
| --- | --- |
| Candidate production handler | `27447f991379ccc0eda02254bf5073dc0a11426f0321781cc1eea922f3f04f40` |
| Candidate diagnostic handler | `557c6248e11c1d1985c8335c2d65eb92aa3c32607c021c0d5433d82faeb6719c` |
| Previous leaf-range production handler | `79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4` |
| M3 production handler | `adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27` |

The source and test identities are recorded separately. The bounded patch and
previous integrated patch reproduce the candidate from the stated base. No
native AROS, Linux FUSE, physical-media or real-power-loss claim follows from
these m68k and host tests.

The previous 1,146-, 873- and 103-file evidence manifests verify unchanged.
All 35 retained Format logs are losslessly base64 encoded with verified
original-byte digests. Seven summary counter-probes pass, including a check
that normal-only output cannot report full qualification. No licensed binary
or HDF is part of this evidence. The runner remains stopped.

The next material investigation is directory/inode cache pressure and the
remaining validated-node misses identified by the earlier detailed profile.
A cache-policy or operation-local batching change needs its own bounded design,
memory budget and unchanged corruption, overflow and error-order guarantees;
no such change is implemented here.
