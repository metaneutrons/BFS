# Independent audit record

Luna reviewed the candidate independently of its implementation and test author.
The initial review identified missing hot-hit node-view detail accounting and a
duplicate cache peek on misses. Both were corrected before the measured starts;
the revised candidate was approved for the bounded experiment.

After all 14 starts completed, the reviewer independently regenerated the strict
summary and predeclared decision. Both outputs match the retained JSON byte for
byte. The audit checked all 28 BFS/PFS3 outputs, all 23 phases, installed pre/post
runtime receipts, actual RDB capacity 30, startup/configuration records and the
67 asset mappings within each separately pinned guest cohort.

All four source-check receipts contain 229 successful checks: remote pre/post,
measurement-time local checkout, and the frozen protocol snapshot. Runtime input
checks contain nine successful entries both before and after execution.

The reviewer confirmed the paired 4KiB growth medians (0.9952956923 normal,
0.9501062960 durable), three of four target pairs not slower, and the reduction
of diagnostic buffer requests from 517 to 260. Normal delete-40 ratios
1.1427917358/1.3655149241 and durable first-pass ExNext-40 ratios
1.3563218391/1.2584269663 breach the predeclared repeated-regression guard.
The negative decision `retain_locally=false` is consistent with the rule fixed
before execution. Same-baseline controls do not establish a noise bound or
justify removing observations. The audit does not establish slowdown causes.

Root additionally checked report values against the validated results and
restored only the experiment-owned code/test delta. Current source intentionally
differs from the candidate archive. Rebuilt production and sidecar handlers match
their original baseline binaries byte for byte. No further experiment was run.
Root rebuilt the restored host probe separately; all eight baseline all-call
CRC/write-sidecar oracles pass. This is not the archived nine-oracle candidate
binary, nor a further emulator measurement.
