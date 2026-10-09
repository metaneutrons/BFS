# Sparse directory-key CRC evidence

This experiment is relative to retained M5 on
`fix/metadata-listing-performance`, not a clean main-only build. Source base
commit is `07216b7f17912c9f28b9b4a920caaa6828b00ccb`; the preceding dirty
implementation and diagnostics are preserved. `baseline-source.sha256` and
`candidate-source.sha256` describe the isolated exports. `candidate.patch`
contains only this experiment's five existing files and three new test/probe
files, relative to a byte-for-byte snapshot made before the experiment.

Production uses the same m68k CRC kernel, immutable zero-transform table,
4KiB format-v3 nodes, commit policy and 30 block buffers. The candidate
inspects complete 16-byte units in the used directory-leaf key prefix. Only
zero runs at least 64 bytes long advance the exact CRC through the existing
zero transform; every other byte is hashed normally. Full block coverage,
legacy nonzero key/padding bytes and all validation remain required. Values,
internal nodes, node-write CRCs and file data retain their previous paths.

## Reproduction

The isolated Cachy workspace is
`/home/fabian/.cache/bfs-performance/sparse-crc-2026-10-09.xN615F`.
`run-series.sh WORKSPACE pilot|deep|normal|durable` selects one family. It
interleaves fresh M5 and candidate images with alternating filesystem order,
and records digests of the three installed guest binaries. The strict
`summarize.py` requires 40 runs by default; `--pilot` and `--deep` explicitly
select four-run exploratory inventories. It checks schemas 4/15, actual RDB
capacity 30, input identities, every data-checked phase, both PFS3 calibrators
and every individual >5x result. No failed/missing run or outlier is filtered.
`summarize-family.py normal|durable` is an explicitly incomplete 16-run interim
summary; it cannot replace the default 40-run verification.

For the corrected standalone probe, `micro-typed/run-microprobe.sh` starts two sequential fresh
system directories and refuses existing directories or another emulator.
Only its own emulator is stopped on completion/timeout. Its configs retain
the same A1200/68040-max, chip 2MiB/fast 8MiB profile. Symmetric exactly typed
adapters explicitly convert the sparse length; `size_t` and `uint32_t` are
different C types on this compiler. The fail-fast source rebuild has the
exact same protocol2 probe binary digest as its two fresh timed boots.
`bash build-probe.sh` rebuilds the probe in the repository's ignored build
directory with the recorded compiler/linker flags and requires the timed
binary digest. It does not install a toolchain or system component.

Run `python3 summarize_micro.py micro-typed/run-1 micro-typed/run-2` from this directory.
The parser independently regenerates 1297 oracle vectors and their FNV digest,
checks 2594 positive ABI witnesses and two negative controls per boot, hashes
the retained configs, and requires all 30 alternating timing samples per boot.
The 264-byte key-density fixture is synthetic: 13 nonzero bytes followed by 251
zeros, not a saved or structurally valid directory node. Dense 4096/64-byte and
128-nonzero/384-zero controls remain visible. These CPU timings cannot predict
whole-filesystem speed, native AROS, physical hardware or Linux FUSE timing.
The initial protocol1 direct-cast runs in `micro/` and consumer in
`micro-initial-parser/` are not qualifying. The failed type-guard compiler log
and stale-executable root-command error are retained and explained in
`review.log`. Only protocol2 data support the final micro conclusion.

The tests are `python3 -m unittest discover -s . -p 'test_summarize*.py' -v`.
`summary-oracles.log` records the primary-agent rerun, including missing,
duplicate, corrupt, wrong-identity, wrong-capacity and wrong-order rejection.

## Inputs and immutability

ROM, Workbench commands/libraries, PFS3, test images and guest binaries stay
outside this text-only bundle. Their digests, configs, startup scripts, raw
TSVs and logs are retained; `binary-identities.sha256` is an identity record,
not a list of binaries included here. `encode-format-logs.py`, when needed,
losslessly base64-encodes Format control bytes with original SHA256; timing
and guest test TSVs remain untouched. `SHA256SUMS` seals the final text bundle.
All preceding evidence bundles remain immutable. No CI runner is restarted,
and no commit, push, issue, PR or merge is part of this local experiment.

The complete 40-run inventory passes data/identity checks; the <=5x performance
goal does not. The core candidate is only provisionally retained. The complete
report records slower append/create medians and requires focused confirmation
before wider acceptance. All 52 Amiga runtime and nine Kickstart compatibility
checks pass, alongside the host/sanitizer and 21/30 conformance/fault contracts.
An initial Linux contract attempt lacked Git metadata; the failed log remains.
The rerun uses a separate bare metadata directory populated from the actual
base-commit bundle through GIT_DIR, without modifying source or oracles. That
binary bundle and bare repository are local Cachy inputs, not evidence assets.
