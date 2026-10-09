#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Apply the retention rule of PILOT_SCOPE.md to a fully validated summary.

Rule 1 (strict correctness and identity) is established by summarize_pilot.py,
which refuses to produce a summary otherwise. This script checks rules 2-4 and
reports, without deciding on them, the paired 4KiB growth medians.
"""
import json
from pathlib import Path
from statistics import median

BUNDLE = Path(__file__).resolve().parent
COUNT_SUFFIXES = (
    "_CALLS", "_READS", "_WRITES", "_UPDATES", "_COMMITS",
    "_VIEWS", "_HITS", "_MISSES", "_MAPS", "_ALLOCS",
)
GROWTH = "APPEND_4K_1M"
WRITE_PACKETS = 256


def is_count_field(name):
    return name.endswith(COUNT_SUFFIXES)


def _diagnostic(summary):
    diag = summary["comparisons"]["diagnostic_candidate_over_base"]
    if len(diag) != 1:
        raise ValueError("expected one diagnostic pair")
    phases = diag[0]["bfs_ratios"]
    growth = phases[GROWTH]

    def pair(name):
        return growth[name]["denominator"], growth[name]["numerator"]

    base_packets, cand_packets = pair("WORK_PACKET_WRITE_CALLS")
    reads = {name: pair(name) for name in
             ("WORK_INODE_READ_CALLS", "WORK_WRITE_DETAIL_INODE_READ_CALLS")}
    unchanged = {name: pair(name) for name in
                 ("WORK_CORE_FILE_WRITE_CALLS", "WORK_WRITE_DETAIL_INODE_WRITE_CALLS")}
    growth_ok = (base_packets == WRITE_PACKETS and cand_packets == WRITE_PACKETS and
                 all(cand == base - WRITE_PACKETS for base, cand in reads.values()) and
                 all(cand == base for base, cand in unchanged.values()))

    increases = []
    for phase, fields in sorted(phases.items()):
        for name, value in sorted(fields.items()):
            if is_count_field(name) and value["numerator"] > value["denominator"]:
                increases.append({"phase": phase, "field": name,
                                  "base": value["denominator"],
                                  "candidate": value["numerator"]})
    return growth_ok, increases, {
        "packet_writes": {"base": base_packets, "candidate": cand_packets},
        "inode_reads": {name: {"base": base, "candidate": cand}
                        for name, (base, cand) in reads.items()},
        "unchanged": {name: {"base": base, "candidate": cand}
                      for name, (base, cand) in unchanged.items()},
    }


def _production(summary):
    pairs = summary["comparisons"]["production_candidate_over_base"]
    if len(pairs) != 4:
        raise ValueError("expected four verified production pairs")
    growth = {}
    repeated_slowdowns = []
    for mode in ("normal", "durable"):
        rows = [row for row in pairs if row["family"] == mode]
        if len(rows) != 2 or {row["repeat"] for row in rows} != {1, 2}:
            raise ValueError("expected two fixed pairs per mode")
        ratios = [row["bfs_ratios"][GROWTH]["US"]["ratio"] for row in rows]
        growth[mode] = {"ratios": ratios, "median": median(ratios)}
        for phase in rows[0]["bfs_ratios"]:
            phase_ratios = [row["bfs_ratios"][phase]["US"]["ratio"] for row in rows]
            if all(ratio is not None and ratio >= 1.10 for ratio in phase_ratios):
                repeated_slowdowns.append({"mode": mode, "phase": phase,
                                           "ratios": phase_ratios})
    not_slower = sum(ratio <= 1.0 for row in growth.values() for ratio in row["ratios"])
    return growth, not_slower, repeated_slowdowns


def decide(summary):
    growth_ok, increases, diagnostic = _diagnostic(summary)
    growth, not_slower, repeated_slowdowns = _production(summary)
    checks = {
        "diagnostic_4k_growth_saves_one_inode_read_per_write": growth_ok,
        "diagnostic_no_count_field_increases": not increases,
        "no_repeated_ten_percent_phase_slowdown": not repeated_slowdowns,
    }
    return {
        "scope": ("Pre-run retention rule of PILOT_SCOPE.md, not production performance "
                  "clearance. Strict correctness and identity validation precede this "
                  "calculation. Medians and pair counts are reported, not decisive."),
        "retain_locally": all(checks.values()),
        "checks": checks,
        "diagnostic_4k_growth": diagnostic,
        "diagnostic_count_increases": increases,
        "repeated_slowdowns": repeated_slowdowns,
        "reported_4k_growth": growth,
        "reported_4k_pairs_not_slower": not_slower,
    }


if __name__ == "__main__":
    print(json.dumps(decide(json.loads((BUNDLE / "summary.json").read_text())),
                     indent=2, sort_keys=True))
