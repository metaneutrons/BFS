# Exact zero-run node CRC acceleration — 2026-09-30

## Result

The agreed acceptance target remains at most 5× PFS3 in each checked AmigaDOS
phase, without weaker durability, integrity, snapshot or recovery guarantees.
It is **not achieved**. Against the `a47d74d` handler on the same Cachy host,
the candidate reduces mean create, write and delete times by 30.4%, 33.9% and
26.7%. The remaining ratios are 32.5×, 19.8× and 41.6× respectively.

Normal handlers; fresh images; sequential interleaved runs in both orders:

| Handler / order | Create 40 | Lookup 400 | Read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline, BFS first | 889,660 | 136,064 | 62,143 | 1,134,394 | 777,148 | 618,364 |
| Candidate, PFS3 first | 664,840 | 144,690 | 64,191 | 754,429 | 768,955 | 431,870 |
| Baseline, PFS3 first | 769,404 | 136,127 | 70,912 | 1,061,690 | 713,276 | 593,980 |
| Candidate, BFS first | 489,917 | 135,360 | 62,719 | 698,237 | 705,853 | 456,894 |
| Baseline mean | 829,532 | 136,095.5 | 66,527.5 | 1,098,042 | 745,212 | 606,172 |
| Candidate mean | 577,378.5 | 140,025 | 63,455 | 726,333 | 737,404 | 444,382 |
| PFS3 mean in candidate runs | 17,758.5 | 72,761.5 | 14,783.5 | 36,608 | 312,541.5 | 10,688.5 |
| Candidate / PFS3 | 32.5× | 1.92× | 4.29× | 19.8× | 2.36× | 41.6× |
| 5× ceiling | 88,792.5 | 363,807.5 | 73,917.5 | 183,040 | 1,562,707.5 | 53,442.5 |

Times are microseconds. Two samples are not a confidence interval. Candidate
create samples differ substantially, and one PFS3 delete sample is higher
than the other. Both orders show mutating-phase improvements, but the precise
gain and ratio require more samples. Lookup mean is 2.9% above baseline;
there is no demonstrated lookup improvement. Do not compare these absolute
times with the preceding Mac-hosted measurements or claim hardware parity.

## Mechanism and format preservation

The writer clears unused key slots and unused value/child slots in its private
node buffer. It hashes the header and live keys, advances the CRC through the
zero key gap, hashes live values/children, then advances through the zero tail.
The resulting stored CRC is exactly the existing full-block IEEE CRC-32 with
the header CRC field zeroed. No checksum coverage or on-disk field changes.

`bfs_crc32_zeros` uses immutable GF(2) powers of the reflected polynomial's
zero-byte transform. All 1,024 constants were independently regenerated;
runtime tests build a separate one-bit transform and square it into the byte
transform, including every basis state and all 32 length powers. The helper
accepts every uint32_t byte length; it performs no allocation or lazy mutable
initialization. The table costs 4 KiB.

Layout offsets and capacities come from the shared B-tree accessors. Internal
nodes retain all `num_keys + 1` live child pointers. Reads still hash the entire
block and accept arbitrary CRC-valid legacy unused bytes, as required by
[the B-tree format](../on-disk-format/btree.md). COW still writes a different
block; old roots and leaves remain byte-for-byte unchanged. Flush, reclamation,
snapshot, data checksum and transaction rules are untouched.

## Verification

The full `make check` run and complete ASan/UBSan host suite passed. CRC tests
are 11/11, B-tree tests 30/30, hardware-failure tests 47/47. New tests cover
materialized zero runs through 65,536 bytes, seeds, chaining, uint32_t high
length boundaries, adjacent-power doubling, and independent runtime matrices.
Legacy-padding fixtures cover 1024-, 4096- and 65536-byte leaves, a full leaf
with no key gap, internal nodes and unchanged live keys/values/children.
Independent bitwise full-block CRC checks verify every rewritten fixture.
Existing corruption, crash cuts, snapshots, low-space, remount and checker
tests passed. Independent Luna review checked the helper and format/layout
integration separately from the authors.

The initial parallel `make -j4 check` failed a runner test with a 200-ms child
exit timeout under compiler load. The unchanged test passed in the subsequent
complete `make check`; the original failure log is retained, not discarded.

A local deep prototype passed the strict v8 workload/counter verifier. It ran
during host compilation and is **not elapsed-time evidence**. Node-write and
publication counts remain unchanged from `a47d74d`; this increment changes
CRC computation, not the amount of COW work. Its raw records remain separately
labelled as contended diagnostic evidence.

## Environment and identities

Cachy: Linux 7.1.3-2-cachyos, Intel Core i5-1235U, 12 logical CPUs, FS-UAE
3.2.35 under Xvfb; A1200/68040 maximum speed, 2 MiB chip / 8 MiB fast RAM,
Kickstart 47.102, Workbench 47.2. The unrelated `coffin.service` emulator was
stopped for measurement. No compiler or competing emulator ran on Cachy.
The Mac's concurrent AROS build was not on the measurement host. Both handler
versions used fresh equal-size 256 MiB HDFs produced by the same fixed host
formatter. PFS3 and the neutral comparison binary are unchanged.

SHA-256 identities:

- Baseline normal: `5426e843bae1b46b969c9b72d686b52d21fdd13247f1e3b8e976d3c404843891`.
- Candidate normal: `220735ac6a66daef704f43cf1d024262af8ab5f4077001d91c7b4f306372c777`.
- Candidate probe: `be8d3628289141d7204083b816815ea7de91dd2a0746e99562b3cc50a773eada`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.
- Fixed formatter: `af1d79b46693de7948f5efbb58d8a5b783e0df5450666d687c7af584183da653`.

[Raw evidence](evidence/bfs-crc-zero-2026-09-30/) includes four accepted normal
runs, the contended diagnostic, final passing host logs and the rejected
parallel runner timeout. SHA256SUMS covers every raw file. Log copies have
trailing whitespace removed. No ROM, licensed binary or HDF is committed.
No remote CI, physical-media, power-cut or controller-lie qualification ran.
