#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Derived medians from the complete strict inventory; retain every ratio."""

import contextlib
import json
from pathlib import Path
import statistics
import sys

import summarize

with contextlib.redirect_stdout(sys.stderr):
    rows = summarize.verify_inventory(Path(__file__).resolve().parent,
                                      summarize.ALL_RUNS)
result = {}
for family in ("normal", "durable"):
    groups = {variant: summarize.group_rows(rows, family, variant)
              for variant in ("m5", "sparse")}
    by_repeat = {variant: {row.run.repeat: row for row in selected}
                 for variant, selected in groups.items()}
    assert all(set(group) == set(range(1, 9)) for group in by_repeat.values())
    result[family] = {}
    for phase in summarize.phase_map(groups["m5"][0].bfs, "m5"):
        values = {}
        for variant, selected in groups.items():
            ratios = [int(row.bfs[phase]) / int(row.pfs3[phase])
                      for row in selected]
            values[variant] = {
                "bfs_over_pfs3": ratios,
                "median": statistics.median(ratios),
                "minimum": min(ratios), "maximum": max(ratios),
                "over5": sum(ratio > 5 for ratio in ratios),
            }
        for filesystem in ("bfs", "pfs3"):
            ratios = []
            for repeat in range(1, 9):
                baseline = by_repeat["m5"][repeat]
                candidate = by_repeat["sparse"][repeat]
                assert baseline.run.order == candidate.run.order
                ratios.append(int(getattr(candidate, filesystem)[phase]) /
                              int(getattr(baseline, filesystem)[phase]))
            values[f"paired_{filesystem}"] = {
                "ratios": ratios, "median": statistics.median(ratios),
                "minimum": min(ratios), "maximum": max(ratios),
                "slower": sum(ratio > 1 for ratio in ratios),
            }
        result[family][phase[:-3]] = values
json.dump(result, sys.stdout, indent=2)
sys.stdout.write("\n")
