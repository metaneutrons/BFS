# BFS performance after buffer reuse

Two fresh checked diagnostic runs locate remaining work after the retained
temporary-buffer recycler and exact-height path allocation. They do not
establish a new normal-handler speedup or complete the five-times PFS3 goal.
Production is unchanged from `8b35eec`; `7c4bc99` adds tests and records the
rejected geometry experiment only.

## Gross timed scopes

Means are microseconds across BFS-first and PFS3-first schema-10 probes.
Direct heap counts cover only the remaining malloc/free call sites in
btree.c, not BIO pool-hook or other translation-unit allocations. Allocation
scopes and device scopes overlap and must not be added together or subtracted
from elapsed time to construct a residual.

| Workload | Probe elapsed | Direct heap | Interface allocation | Free-space allocation | Device read write update |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 281342 | 1972.85 | 65621.48 | 71662.68 | 42430.07 |
| Lookup 400 | 106558.5 | 0 | 0 | 0 | 159.29 |
| Small read 40 | 45439.5 | 0 | 0 | 0 | 4362.27 |
| Write 8 MiB | 421021.5 | 7798.37 | 66374.96 | 50699.98 | 80120.78 |
| Read 8 MiB | 508317.5 | 0 | 0 | 0 | 63806.51 |
| Delete 40 | 236989.5 | 2737.61 | 51953.89 | 50707.03 | 32903.43 |

Create records 128 direct malloc and 128 free calls per run; lookup and both
read phases record none. Write records 680/680 and 682/682; delete records
161/201 in both runs. These counts are not complete filesystem heap counts.
The remaining directly timed heap work is small; another direct-malloc tweak
alone is not a plausible solution to the present gap. That is a prioritization
judgment, not an elapsed-time forecast.

## Metadata traffic and preserved publication

Counts below give BFS-first / PFS3-first. Node-count differences and elapsed
variation are retained, including 182846 versus 291133 µs for deletion despite
fewer node writes in the slower run.

| Workload | Free-Tree body refill other | Directory inode extent or other nodes | Commits sealed publications |
| --- | --- | --- | --- |
| Create | 281/281; 40/40; 126/127 | 75/75; 128/128; 40/40 | 40/40; 40/40; 40/40 |
| Write | 136/136; 1/1; 4/4 | 2/2; 150/151; 128/128 | 1/1; 1/1; 1/1 |
| Delete | 116/116; 80/41; 41/41 | 76/76; 40/40; 0/0 | 40/40; 40/40; 40/40 |

Reserve-return and post-publication pending-reclaim Free-Tree node writes are
zero in these phases. BIO updates remain 120/3/120 for create/write/delete;
no fence or publication was removed. Allocation-side work remains material,
but lower node counts alone do not prove lower elapsed time.

Generic current-transaction node overwrites are excluded: the pre-operation
root must remain readable when any COW path write fails. Transaction IDs do
not establish exclusive per-operation ownership. The qualified owned-root
API still writes a different block. Earlier recycling and contiguous-write
experiments were rejected; their
[record](bfs-next-quickwin-experiments-2026-09-29.md) is not superseded by this
diagnostic. Broader batching needs a separate ownership/failure-atomicity
design, not a shortcut around the retained fault oracles.

## Evidence and limits

All four filesystem outputs pass the strict schema-10 data, completion and
counter verifier. The existing ABI-11 probe uses CPU stride 1 and CRC stride
64; clock frequency is 709379 Hz in both BFS outputs. Clock-call calibration
is too coarse to subtract exact instrumentation overhead. Sampled CRC is not
an unbiased total. Two runs are not statistical precision, hardware throughput
or normal-handler acceptance.

Handler probe SHA-256 is
`be4162868ebfb7a67ff3f44203aec7777edc579887a0f27ca487e4ee9d3f9683`;
the unchanged normal guest tool is
`1fccf207c96144f81c64a8a5dd2e350b7f32787402eedad6c102b34ad0a6ec80`.
Formatter, ROM, Workbench, PFS3, options=0, 4 KiB BFS geometry, 255.5 MiB
partitions and isolated Cachy A1200/68040 FS-UAE configuration match the
preceding normal measurements. Both images were prepared before the serial
guest runs. No other guest emulator or compiler ran; physical-host scheduling
is uncontrolled.

[Raw evidence](evidence/bfs-after-buffer-profile-2026-10-01/) preserves both
orders, every TSV, machine and completion file, configurations, builder/runner
and emulator logs, input identities and SHA256SUMS. Primary calculations and
independent Luna extraction agree on the recorded means and traffic counts.
Timing TSVs are unmodified; only copied log trailing horizontal whitespace is
removed. No binary, licensed asset or HDF is committed. No production change,
CI, push, PR, merge, release or device-bound hardware qualification ran.
