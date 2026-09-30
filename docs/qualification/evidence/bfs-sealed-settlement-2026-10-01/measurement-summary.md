# Sealed-settlement normal measurement summary

## Scope and acceptance gate

This summary extracts the eight normal compare runs from `build/sf-cachy-runs.log`. Diagnostic blocks are excluded from these normal calculations. Batch `a` is the initial `sf-cachy` set; batch `b` is the `sf-cachy-repeat` set. `B` means BFS-first and `P` means PFS3-first. Each input has one BFS and one PFS3 result, both with `FS_COMPARE_BENCH=1` and `PASS=1`: eight unique normal inputs, sixteen passing filesystem outputs. Timings are microseconds (µs). Tuples are ordered `create40, lookup400, small-read40, seq-write8M, seq-read8M, delete40`.

The acceptance target is at most 5× PFS3 in every checked workload while preserving existing durability, integrity, snapshot, and recovery guarantees. **Not achieved:** candidate runs exceed 5× on create (4/4), 8 MiB write (4/4), and delete (4/4); one candidate small-read run also exceeds 5×. These measurements do not qualify the change against that gate.

## Raw normal timings

| Batch | Run directory | Variant | Order | BFS µs (six workloads) | PFS3 µs (six workloads) |
|---|---|---|---|---|---|
| a | `sf-cachy-baseline-bfs-first` | baseline | BFS-first | 442238, 126207, 71233, 531261, 731004, 406015 | 17216, 66176, 14402, 37183, 294655, 15745 |
| a | `sf-cachy-baseline-pfs3-first` | baseline | PFS3-first | 463740, 173631, 60608, 508477, 595837, 442238 | 18048, 66943, 13760, 36095, 257662, 7807 |
| a | `sf-cachy-candidate-bfs-first` | candidate | BFS-first | 314687, 124671, 88895, 483260, 702460, 260094 | 17792, 71488, 13953, 55872, 304319, 8064 |
| a | `sf-cachy-candidate-pfs3-first` | candidate | PFS3-first | 324285, 136062, 60673, 571645, 599165, 233855 | 16896, 79488, 14975, 35071, 262207, 7936 |
| b | `sf-cachy-repeat-baseline-bfs-first` | baseline | BFS-first | 442192, 121854, 68992, 496829, 638972, 368447 | 16316, 65983, 14209, 36224, 310718, 7872 |
| b | `sf-cachy-repeat-baseline-pfs3-first` | baseline | PFS3-first | 393465, 123774, 54783, 491454, 666555, 366206 | 16575, 72513, 28735, 43840, 292478, 7873 |
| b | `sf-cachy-repeat-candidate-bfs-first` | candidate | BFS-first | 339198, 129663, 54655, 513278, 668476, 241342 | 18560, 69824, 26112, 74624, 336062, 8704 |
| b | `sf-cachy-repeat-candidate-pfs3-first` | candidate | PFS3-first | 362494, 153715, 54143, 591933, 608702, 207422 | 17663, 67137, 18495, 37887, 327933, 8191 |

## BFS means and candidate-versus-baseline deltas

Means average the four runs for each variant. `Δ = candidate − baseline`; `%Δ = Δ / baseline × 100`. Negative deltas mean a lower candidate time.

| Workload | Baseline BFS mean (µs) | Candidate BFS mean (µs) | Δ (µs) | %Δ |
|---|---:|---:|---:|---:|
| Create 40 | 435408.750 | 335166.000 | -100242.750 | -23.023% |
| Lookup 400 | 136366.500 | 136027.750 | -338.750 | -0.248% |
| Small-read 40 | 63904.000 | 64591.500 | +687.500 | +1.076% |
| Sequential write 8 MiB | 507005.250 | 540029.000 | +33023.750 | +6.513% |
| Sequential read 8 MiB | 658092.000 | 644700.750 | -13391.250 | -2.035% |
| Delete 40 | 395726.500 | 235678.250 | -160048.250 | -40.444% |

## Order-matched BFS deltas

Values are candidate minus baseline within the same batch and filesystem order, in µs.

| Workload | a BFS-first | a PFS3-first | b BFS-first | b PFS3-first |
|---|---:|---:|---:|---:|
| Create 40 | -127551 | -139455 | -102994 | -30971 |
| Lookup 400 | -1536 | -37569 | +7809 | +29941 |
| Small-read 40 | +17662 | +65 | -14337 | -640 |
| Sequential write 8 MiB | -48001 | +63168 | +16449 | +100479 |
| Sequential read 8 MiB | -28544 | +3328 | +29504 | -57853 |
| Delete 40 | -145921 | -208383 | -127105 | -158784 |

## Candidate BFS / PFS3 ratios

For each of the four candidate runs, the per-run ratio is BFS time divided by PFS3 time for the same workload and run. The ratio of means is the candidate BFS mean divided by the candidate PFS3 mean; mean ratio is the arithmetic mean of the four per-run ratios. Threshold counts use a strict `>5×` comparison. Run IDs are batch plus order (`aB`, `aP`, `bB`, `bP`).

| Workload | Candidate PFS3 mean (µs) | Ratio of means | Mean per-run ratio | Per-run min–max | >5× / 4 and runs over threshold |
|---|---:|---:|---:|---:|---|
| Create 40 | 17727.750 | 18.906× | 18.920× | 17.687–20.523× | 4/4: aB=17.687×, aP=19.193×, bB=18.276×, bP=20.523× |
| Lookup 400 | 71984.250 | 1.890× | 1.901× | 1.712–2.290× | 0/4: — |
| Small-read 40 | 18383.750 | 3.514× | 3.861× | 2.093–6.371× | 1/4: aB=6.371× |
| Sequential write 8 MiB | 50863.500 | 10.617× | 11.863× | 6.878–16.300× | 4/4: aB=8.649×, aP=16.300×, bB=6.878×, bP=15.624× |
| Sequential read 8 MiB | 307630.250 | 2.096× | 2.110× | 1.856–2.308× | 0/4: — |
| Delete 40 | 8223.750 | 28.658× | 28.693× | 25.323–32.254× | 4/4: aB=32.254×, aP=29.468×, bB=27.728×, bP=25.323× |

## Outliers and interpretation limits

The PFS3 sequential-write samples 55872 µs (`aB`, candidate) and 74624 µs (`bB`, repeat-candidate) are slow relative to the other candidate samples, 35071 and 37887 µs. The baseline repeat PFS3-first sample, 43840 µs, is also its slowest PFS3 sequential-write observation. Every sample remains in the raw table, means, and ratio calculations; no outlier was discarded. The high PFS3 write times in `aB` and `bB` lower those same-run BFS/PFS3 ratios, which nevertheless remain above 5×.

The small-read and sequential-read paths are unchanged. Their measured mean differences (+1.076% and -2.035%) are descriptive only; these four runs per variant establish neither statistical precision nor causal attribution. No significance or confidence-interval claim is made for any workload.

## Reproduction

The eight uniquely named normal input blocks in `build/sf-cachy-runs.log` were identified by their `sf-cachy-{baseline,candidate}` and `sf-cachy-repeat-{baseline,candidate}` run names and `bfs-first` / `pfs3-first` order. Each block contains exactly two `FS_COMPARE_BENCH=1` outputs, one per filesystem, and two `PASS=1` markers. Means use arithmetic averages; matched deltas pair batch and order; same-run ratios use only the candidate BFS and PFS3 values from each run. All diagnostic blocks were omitted from these calculations. The primary agent independently reproduced all normal means, deltas, ratio ranges and threshold counts from the eight raw TSV pairs.

## Qualified deep-probe comparison

The baseline deep input uses handler ABI 9 and `FS_DEEP_COMPARE=8`; the selected candidate input, `sf-cachy-candidate-deep-v9`, uses ABI 10 and schema 9. Schema 9 appends `SEALED_COMMITS` and `SEALED_METADATA_FENCES`; fields shared by schemas 8 and 9 are compared below. This adds proof counters without weakening the strict verifier. The earlier `sf-cachy-candidate-deep` attempt emitted guest `PASS=1` with schema 8, but the strict verifier rejected its BFS output as invalid or incomplete. The baseline deep and schema-9 candidate deep runs both passed strict qualification. The inventory is eight qualified normal runs plus two qualified deep runs (10 qualified), with the old schema-8 candidate as one rejected attempt.

### Deep probe elapsed times

These are single-run instrumented probe times in µs. They are diagnostic observations, not normal-time acceptance measurements.

| Workload | Baseline BFS | Baseline PFS3 | Candidate BFS | Candidate PFS3 |
|---|---:|---:|---:|---:|
| Create 40 | 478652 | 16705 | 307582 | 22655 |
| Lookup 400 | 126654 | 66431 | 166079 | 100736 |
| Small-read 40 | 58943 | 14656 | 58688 | 14144 |
| Sequential write 8 MiB | 506467 | 36480 | 494974 | 37952 |
| Sequential read 8 MiB | 586428 | 279358 | 597821 | 256062 |
| Delete 40 | 409150 | 8320 | 211262 | 8256 |

PFS3 deep outputs contain elapsed times only; no BFS-style counter comparison is available for PFS3.

### Common BFS counters on mutating phases

Values are baseline schema 8 → candidate schema 9. The five Free-Tree buckets are allocation-body / reserve-refill / reserve-return / post-publish pending-reclaim / other node writes. BIO writes and updates are handler block-I/O request counts, not physical-media write counts.

| Workload | BIO writes | BIO updates | Data reads / writes | Metadata node writes | Free-Tree node writes | Free-Tree buckets |
|---|---:|---:|---:|---:|---:|---|
| Create 40 | 1033 → 775 | 160 → 120 | 0 / 40 → 0 / 40 | 873 → 695 | 632 → 451 | 279 / 40 / 80 / 147 / 86 → 282 / 40 / 0 / 0 / 129 |
| Sequential write 8 MiB | 2482 → 2475 | 4 → 3 | 0 / 2048 → 0 / 2048 | 431 → 426 | 146 → 141 | 136 / 1 / 2 / 4 / 3 → 136 / 1 / 0 / 0 / 4 |
| Delete 40 | 725 → 353 | 160 → 120 | 0 / 0 → 0 / 0 | 605 → 313 | 489 → 197 | 116 / 40 / 80 / 252 / 1 → 116 / 40 / 0 / 0 / 41 |

### Mutating-phase common totals and sealed-path proof

Totals sum create, sequential write, and delete. Counter names shared by schema 8 and 9 are directly comparable. Schema 8 has no sealed-commit or metadata-fence fields.

| Counter | Baseline | Candidate |
|---|---:|---:|
| BIO reads / writes / updates | 53 / 4240 / 324 | 53 / 3603 / 243 |
| Data reads / writes | 0 / 2088 | 0 / 2088 |
| Metadata node writes | 1909 | 1434 |
| Transaction commits | 81 | 81 |
| Free-space allocations / extent maps | 531 / 168 | 534 / 168 |
| Free-Tree node writes | 1267 | 789 |
| Post-publish reclaim passes | 162 | 0 |
| Superblock publications | 243 | 81 |
| Sealed commits / metadata fences | Not in schema 8 | 81 / 81 |
| READ / WRITE / UPDATE ticks | 4513 / 172934 / 7876 | 2952 / 104279 / 5047 |
| Data-write ticks | 42135 | 38279 |
| CRC-write calls | 1909 | 1434 |
| CRC-write samples / sampled ticks | 28 / 1135 | 20 / 777 |

The candidate sealed counts are create `40/40` commits/fences, sequential write `1/1`, and delete `40/40`; corresponding post-publish reclaim passes and superblock publications are `0/40`, `0/1`, and `0/40`. The baseline has 80/120 for create, 2/3 for sequential write, and 80/120 for delete. CRC-write ticks are sampled values, not total CRC time, and must not be extrapolated to all CRC calls.

These counter deltas describe the two measured probes. They do not establish a causal elapsed-time improvement or a performance promise. The normal acceptance gate remains the eight uninstrumented runs above; its four-run means and ratios are descriptive and do not establish statistical precision or causal attribution.

### Deep input SHA-256

Measured handler and guest-tool identities are recorded in [`sf-qualified-input-identities.log`](sf-qualified-input-identities.log). The four selected raw deep TSVs have these SHA-256 values:

| Input | BFS deep TSV SHA-256 | PFS3 deep TSV SHA-256 |
|---|---|---|
| `sf-cachy-baseline-deep` (ABI 9 / schema 8) | `e7c3bab126f236c9fab0ebf93f81c6b4cfe38a902fb8e0a3275aebcb0b881416` | `4fa45bafa5e0dc62a5436d5f1900b8b3e60ebb86f418da476b0b05e1d69e1a89` |
| `sf-cachy-candidate-deep-v9` (ABI 10 / schema 9) | `0d7d68296778d5386cdecf74e0e91a482a24d12a16375d17e125c1676574e30a` | `97525a27e8c84dd84459183331bbc8acb9be143a64a266e84361af914a4bbb3a` |
