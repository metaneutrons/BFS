#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Apply the pre-run local-retention rule to a fully validated pilot summary."""
import json
from pathlib import Path
from statistics import median

BUNDLE = Path(__file__).resolve().parent


def decide(summary):
    pairs = summary["comparisons"]["production_candidate_over_base"]
    if len(pairs) != 4:
        raise ValueError("expected four verified production pairs")
    primary = {}
    repeated_slowdowns = []
    for mode in ("normal", "durable"):
        rows = [row for row in pairs if row["family"] == mode]
        if len(rows) != 2 or {row["repeat"] for row in rows} != {1, 2}:
            raise ValueError("expected two fixed pairs per mode")
        ratios = [row["bfs_ratios"]["APPEND_4K_1M"]["US"]["ratio"] for row in rows]
        primary[mode] = {"ratios": ratios, "median": median(ratios)}
        for phase in rows[0]["bfs_ratios"]:
            phase_ratios = [row["bfs_ratios"][phase]["US"]["ratio"] for row in rows]
            if all(ratio >= 1.10 for ratio in phase_ratios):
                repeated_slowdowns.append({"mode": mode, "phase": phase, "ratios": phase_ratios})
    diag = summary["comparisons"]["diagnostic_candidate_over_base"]
    if len(diag) != 1:
        raise ValueError("expected one diagnostic pair")
    requests = diag[0]["bfs_ratios"]["APPEND_4K_1M"]["WORK_DETAIL_BUFFER_ALLOC_CALLS"]
    not_slower = sum(ratio <= 1.0 for row in primary.values() for ratio in row["ratios"])
    checks = {
        "diagnostic_buffer_requests_lower": requests["numerator"] < requests["denominator"],
        "both_primary_mode_medians_lower": all(row["median"] < 1.0 for row in primary.values()),
        "at_least_three_primary_pairs_not_slower": not_slower >= 3,
        "no_repeated_ten_percent_phase_slowdown": not repeated_slowdowns,
    }
    return {
        "scope": "Pre-run local-retention rule, not production performance clearance. Correctness and identity validation must precede this calculation.",
        "retain_locally": all(checks.values()),
        "checks": checks,
        "primary_4k_growth": primary,
        "primary_pairs_not_slower": not_slower,
        "diagnostic_4k_buffer_requests": requests,
        "repeated_slowdowns": repeated_slowdowns,
    }


if __name__ == "__main__":
    print(json.dumps(decide(json.loads((BUNDLE / "summary.json").read_text())),
                     indent=2, sort_keys=True))
