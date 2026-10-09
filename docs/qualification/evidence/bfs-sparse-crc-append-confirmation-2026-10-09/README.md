# BFS sparse CRC append and create confirmation

This bounded confirmation tests the possible append/create regressions of the
provisionally retained sparse directory-key CRC candidate. The source and
executable identities are the same as the preceding
[experiment](../bfs-sparse-key-crc-performance-2026-10-09/README.md).
No core, guest, workload, CRC coverage, durability policy or buffer count is
changed. The immutable preceding bundle remains a separate cohort.

## Fixed measurement plan

`run-confirmation.sh FROZEN_WORKSPACE` refuses a pre-existing result directory
or another FS-UAE instance. Its CI guard queries `snapdog-runner.service`,
which is not present on Cachy; that query is fail-open and cannot establish
CI exclusion. The separately retained service/process inventory checks the
actual `actions.runner.metaneutrons-snapdog-os.cachy.service` and
`gitlab-runner.service`, both loaded/inactive with PID 0; no active GitHub
Runner.Listener/Worker process was found. No CI job is dispatched. The frozen Cachy export is
`/home/fabian/.cache/bfs-performance/sparse-crc-2026-10-09.xN615F`. Before timing,
all 366 frozen candidate-source entries and the M5/candidate/guest/PFS3/ROM/
formatter digests must match. Fresh disk images are used for every run, with
the same A1200/68040-max profile and actual RDB capacity of 30 block buffers.
The unchanged schema-4 guest executes all 23 phases, including data checks;
the confirmation focuses on 4KiB append, 1KiB append and creation of 40 files.

There are eight M5/candidate pairs in normal mode and eight in durable mode
(32 runs). Within each pair the filesystem order is identical. Each of the
four combinations of filesystem order and first handler receives two pairs
per mode. Mode order alternates too. The schedule is deterministic and
balanced, not randomized. Two additional M5/M5 pairs per mode (eight runs)
provide identical-executable controls, one per filesystem order, inserted
after main repeats 4 and 8. Two A/A pairs per mode characterize observed
scatter; they cannot estimate its full distribution or prove the absence of
a regression.

The full predetermined 40-run inventory and exact execution sequence must
pass. Missing, additional, failed, corrupt, identity-mismatched or wrong-order
runs cannot be filtered. The scripts retain every phase and both PFS3
calibrators. No decision is based on a selected subset or fastest runs. The
same-repeat BFS candidate/M5 ratio is the primary measure; per-phase PFS3
ratios and the descriptive ratio of those two ratios are reported separately.
PFS3 normalization is not a replacement for raw results or evidence of a
particular cause. Handler/filesystem-order strata and every individual ratio
remain visible. The previous cohort is compared separately, not silently
pooled with the confirmation.

Consistently slower raw ratios across modes and order strata would strengthen
the regression concern. A change of sign or overlapping A/A scatter would
limit attribution but would not establish that the original disadvantage was
noise or that the candidate never regresses. The existing <=5x PFS3 goal
remains open independently of this bounded confirmation.

## Evidence and reproduction

The runner writes to the frozen export's
`candidate/build/benchmark/sparse-confirmation-20261009`. Collection copies
the top-level schedule/input records and every run's text evidence into this
local evidence directory, excluding executable/assets/image contents.
Run `python3 summarize_confirmation.py` from this directory after that copy.
The summary reuses the sealed preceding strict parser, its binary identities,
the repository verifier and actual RDB inspection. Run its unit oracles with
`python3 -m unittest discover -s . -p test_confirmation.py -v`.
`python3 verify_assets.py` checks all before/after runtime receipts and the
67 non-handler asset hashes shared across the 40 retained Cachy copies.
`python3 -m unittest discover -s . -p 'test_*.py' -v` checks all 24 summary
and asset oracles. The reused old summary has 25 passing oracles as well.
`schedule.tsv` records the required sequence. Each run retains its config,
startup sequence, actual installed-input digests, RDB JSON, raw TSVs, completion
marker and logs. ROMs, Workbench commands, PFS3, guest/handler binaries and disk
images are not included in this text-only bundle. Format control bytes may be
losslessly encoded by the preceding `encode-format-logs.py`; timings remain
unchanged. `SHA256SUMS` seals the final collected text evidence.

No CI, commit, push, PR, issue change or merge is authorized by this experiment.
The runner removes only its newly generated disposable HDFs after successful
data verification and stops only its own emulator. Failed runs retain their
artifacts and stop the series; they are not silently retried.

## Completed outcome

All 40 predefined runs verify. The earlier 5–7% growth disadvantage is not
replicated at that magnitude, but durable 4KiB growth remains 2.5% slower
and durable reading of the grown files is 25% slower in seven of eight pairs.
Identical-M5 controls vary substantially but do not establish noise as the
cause or absence of a regression. The candidate remains only provisionally
retained. Repeated large ExAll gains 11–16.5%, while the <=5x PFS3 goal remains
open. See the [complete report](../../bfs-sparse-crc-append-confirmation-2026-10-09.md).
The initial asset-test import failed because a hyphenated helper filename
could not be imported; it is renamed to verify_assets.py, with the failed log
retained and the corrected 24-test run passing. Neither measurement nor test
expectations changed. The original executed runner and its obsolete CI guard
are retained unchanged; reproduction must separately check the actual units.
