# BFS sparse CRC append and create confirmation

The balanced confirmation does not reproduce the earlier 5–7% write-append
disadvantage at the same magnitude. Normal 4KiB growth is essentially even;
durable growth remains 2.5% slower in the paired median. Create and 1KiB growth
do not have slower durable medians. The regression concern is **not resolved**:
durable reading of the grown files is 25% slower in the median, in seven of
eight pairs, and normal sequential reading is also slower. The exact sparse
directory-key CRC candidate remains only provisionally retained locally,
without broader performance approval or publication.

The unchanged candidate reproduces 11–16.5% faster repeated large ExAll
listings. The <=5x PFS3 goal remains open: these ExAll medians are still
14–16x PFS3. The full 40-run inventory passes data, identity and structural
checks; it does not pass the overall performance goal.

## Fixed scope and inputs

The [preceding sparse CRC experiment](bfs-sparse-key-crc-performance-2026-10-09.md)
defines the candidate and its initial 32-run production cohort. This is a
separate confirmation, not a silently pooled or replacement cohort. Source
base is `07216b7f17912c9f28b9b4a920caaa6828b00ccb` on
`fix/metadata-listing-performance`, including the retained dirty M1–M5 work.
No filesystem, guest, workload, durability or CRC implementation is changed.
All 366 frozen source entries match before and after timing; the eight
candidate files also match the current local source byte for byte.

There are eight production M5/sparse pairs per normal/durable mode, plus two
identical-M5 pairs per mode: 40 fresh emulator starts. Each primary pair uses
the same filesystem order. Both BFS/PFS3 order and first BFS handler are
balanced, with two pairs per combination per mode. Mode order alternates;
the schedule is fixed, not randomized. Identical-handler controls are inserted
after primary repeats 4 and 8. The complete predefined inventory is retained
without retries, failed-run exclusion or outlier filtering.

The inputs are the same A1200/68040-max profile, FS-UAE 3.2.35, licensed
Kickstart/Workbench/PFS3 files and schema-4 guest as before, with actual RDB
capacity 30. Production handler SHA256 identities remain:

- M5: `79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4`.
- Sparse: `486e3bbbf5c09a2b7d5cae0e3afd02a3a0a57d76a892d0aca6dfec31dbdb21f4`.

All three installed runtime digests match their pins before and after every
run. The 67 non-handler installed asset receipts are identical across all
40 runs. Only their hashes are distributed, not licensed files or binaries.
The [runner console](evidence/bfs-sparse-crc-append-confirmation-2026-10-09/runner-console.log)
records execution from 14:19:15 to 14:45:05 UTC on 9 October 2026.

The append-labeled phases use `Open(..., MODE_NEWFILE)` and grow a newly
created file through one handle. They include initial path lookup/creation
and growth, not append-to-existing-file semantics. Sparse read CRC can be
reached during directory lookup/creation; write CRC and ordinary file-data
paths are unchanged. These whole-phase timings do not attribute CPU time to
CRC calls, I/O or flushes.

## Focused results

Ratios below are medians of the eight same-repeat sparse/M5 BFS ratios,
not ratios of mean times. Values above 1 mean slower. Every phase, raw pair,
PFS3 calibrator and balanced-order stratum is in the strictly verified
[summary](evidence/bfs-sparse-crc-append-confirmation-2026-10-09/summary.json).

| Phase | Normal sparse/M5 | Durable sparse/M5 | Slower normal pairs | Slower durable pairs |
| --- | ---: | ---: | ---: | ---: |
| Create 40 | 1.0043 | 0.9714 | 4/8 | 2/8 |
| Grow 1MiB in 4KiB steps | 1.0004 | 1.0248 | 4/8 | 6/8 |
| Grow 256KiB in 1KiB steps | 0.9366 | 0.9874 | 1/8 | 3/8 |

The initial cohort had normal/durable 4KiB medians 1.0465/1.0695, 1KiB
1.0262/1.0181 and create 0.9783/1.0604. The confirmation weakens a fixed
5–7% write-growth penalty hypothesis, but does not establish absence of a
penalty. Durable 4KiB growth remains slower in six pairs and in all four
balanced-order stratum medians. Its raw range is 0.9665–1.2054; normal range
is 0.7109–1.2468. Two pairs per stratum are descriptive, not a confidence bound.

## Read controls prevent general acceptance

| Phase | Normal sparse/M5 | Durable sparse/M5 | Slower normal pairs | Slower durable pairs |
| --- | ---: | ---: | ---: | ---: |
| Read the two grown files, 1.25MiB | 1.0000 | 1.2503 | 4/8 | 7/8 |
| Sequential read 8MiB | 1.0530 | 1.0141 | 5/8 | 5/8 |

Durable reading of the grown files has raw range 0.4616–1.9521 and all four
balanced-order stratum medians exceed 1. PFS3's paired median is 1.0023;
the descriptive median of per-pair BFS/PFS3 interaction ratios is 1.1255.
That normalization retains a slower signal but neither replaces the raw
1.2503 result nor identifies its cause. Normal sequential read ranges
0.9360–1.4022, with a descriptive normalized median 1.0568. These values cannot
be omitted because they were controls rather than the original focus.

Identical-M5 A/A ratios compare the second to the first execution. They are
kept within each mode; mixed-mode statistics are explicitly marked and not
used as a mode-specific variability estimate.

| A/A phase | Normal range, two pairs | Durable range, two pairs |
| --- | ---: | ---: |
| Create 40 | 0.5297–1.7164 | 1.0383–1.1215 |
| 4KiB growth | 1.0427–1.7903 | 0.9556–1.0448 |
| 1KiB growth | 1.0557–1.1745 | 0.9673–1.0627 |
| Read grown files | 0.8517–1.0102 | 0.9657–1.5058 |
| Sequential read 8MiB | 0.9924–1.1240 | 1.2089–1.2434 |

These controls demonstrate substantial observed variation with identical
code, including a 50.6% slower durable grown-file read. They do not prove that
the candidate's disadvantages are noise: two A/A pairs per mode do not
estimate the full distribution, and the candidate has repeated slower durable
read results. No universal improvement or regression-free approval follows.

## Listing gains and the remaining performance gap

Repeated ExAll 400/1000 medians are 0.8607/0.8353 in normal mode and
0.8860/0.8711 in durable mode: 11–16.5% faster than M5. Slower pairs are
0/8 and 0/8 normal, 0/8 and 1/8 durable. All four balanced-order stratum
medians improve for both sizes and modes. Repeated ExNext medians are
0.9598/0.9380 normal and 0.8466/0.9532 durable; its strata are less consistent.
Warm 40-entry repeated listings are separately retained and not mislabeled
as cold scans. Legacy `LIST_*_400` still means ten scans of 40 entries.

Candidate ExAll 400/1000 BFS/PFS3 medians are 15.6552/14.2127 normal and
15.2244/15.9774 durable. Across all 23 phases and eight candidate runs per
mode, 52/184 normal and 41/184 durable ratios exceed **5 times PFS3**, not a
5% improvement threshold. All individual violations remain in the summary.

## Validation and decision

All 40 runs pass the unchanged strict repository verifier, actual 30-buffer
RDB check, exact schema/phase inventory, binary identities, retained startup
order and complete predefined schedule. The new summary/asset oracles pass
24 tests, and the reused sealed parser's 25 oracles pass again. ShellCheck
and `git diff --check` pass. An initial asset-test import failed because the
helper's filename used a hyphen; renaming it to the importable
`verify_assets.py` fixes the setup without changing measurements or test
expectations. The failed log is retained separately. The previous 784-entry
sparse evidence manifest still verifies unchanged.

The preceding full host/sanitizer/Amiga correctness qualification applies
to the unchanged source and exact binaries; those full suites were not
rerun for this measurement-only confirmation. The shared core, format-v3
coverage, validation and durability guarantees are unchanged. Luna
independently reviewed the measurement/summary logic and recomputed the
reported focused medians and read controls from the raw TSVs.

The runner's obsolete `snapdog-runner.service` guard is fail-open: the named
unit is absent on Cachy, so its initial `inactive` response did not prove CI
exclusion. A subsequent inventory checks the actual GitHub and GitLab
runner units: both loaded/inactive with PID 0, no active GitHub runner
Listener/Worker found. The post-run check confirms the same states and no
remaining emulator. The original executed script is preserved; reproduction
requires checking the actual CI units, not relying on the obsolete guard.
No CI, commit, push, PR, issue change or merge was dispatched.

**Decision:** retain only the existing provisional local candidate; do not
grant broader performance acceptance. Listing gains are replicated, but
durable growth and especially the grown-file read remain open signals.
The next bounded investigation should separate read/flush timing and I/O/CRC
work in those phases before another implementation or full timing series.
Fresh-leaf I/O remains a later optimization question, not an authorized
adjacency/read-ahead assumption or a demonstrated speedup.

The [evidence directory](evidence/bfs-sparse-crc-append-confirmation-2026-10-09/README.md)
contains reproduction scripts, all 40 raw comparisons, source/asset receipts,
test and review logs, and a SHA256 manifest. ROM, Workbench, PFS3, guest/handler
binaries and disk images remain outside the text-only bundle.
