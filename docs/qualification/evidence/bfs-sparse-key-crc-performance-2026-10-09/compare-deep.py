#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare strict, matched deep counters; sampled ticks are not CPU totals."""

from pathlib import Path

import summarize

root = Path(__file__).resolve().parent
rows = summarize.verify_inventory(root, summarize.DEEP_RUNS)
pairs = {(row.run.variant, row.run.repeat): row for row in rows}
print("phase\trepeat\torder\treads_m5\treads_sparse\tcrc_calls_m5\tcrc_calls_sparse"
      "\tsamples_m5\tsamples_sparse\tticks_m5\tticks_sparse\tsampled_tick_ratio"
      "\tnontiming_counters_equal")
for repeat in (1, 2):
    baseline, candidate = pairs[("m5", repeat)], pairs[("sparse", repeat)]
    assert baseline.run.order == candidate.run.order
    phases = [key[:-3] for key in summarize.phase_map(baseline.bfs, baseline.run.name)
              if key.startswith(("LIST_EXNEXT_", "LIST_EXALL_"))]
    for phase in phases:
        def get(row, metric):
            return int(row.bfs[f"{phase}_{metric}"])

        non_timing = [name for name in summarize.DEEP_RAW_COUNTERS
                      if not name.endswith("SAMPLE_TICKS")]
        equal = all(get(baseline, name) == get(candidate, name)
                    for name in non_timing)
        metrics = ("BIO_READS", "NODE_CRC_READ_CALLS", "NODE_CRC_READ_SAMPLES",
                   "NODE_CRC_READ_SAMPLE_TICKS")
        numbers = [str(get(row, metric)) for metric in metrics
                   for row in (baseline, candidate)]
        ticks = get(baseline, "NODE_CRC_READ_SAMPLE_TICKS")
        ratio = f"{get(candidate, 'NODE_CRC_READ_SAMPLE_TICKS') / ticks:.4f}" if ticks else "n/a"
        print("\t".join((phase, str(repeat), baseline.run.order, *numbers,
                         ratio, str(equal))))
