#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 22 October ExAll result cache bound.

Runs the repository's strict verifier on all 16 compare starts, reports the
BFS / PFS3 ratio of both handlers per phase and the paired ratio of the bound
over the reference in the same round, derives the figures the plan needs
(geometric mean of the unchanged phases, per-entry costs, the lock-owned
estimate) and evaluates the decision rule of docs/plans/bfs-exall-result-cache-v1.md
on them. Writes summary.json and prints the same figures as text. Nothing is
retained or reverted.
"""
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess  # nosec B404 - fixed repository verifier, no shell
import sys

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[3]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
RESULTS = BUNDLE / "results"
MEMORY = BUNDLE / "memory-probe.txt"
VARIANTS = ("reference", "bound")
STARTS = 16

# Entry visits per phase: directory size times passes.
EXALL = {
    "LIST_EXALL_400": 40 * 10,
    "LIST_EXALL_40_ENTRIES_FIRST_PASS": 40,
    "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL": 40 * 10,
    "LIST_EXALL_400_ENTRIES_FIRST_PASS": 400,
    "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL": 400 * 10,
    "LIST_EXALL_1000_ENTRIES_FIRST_PASS": 1000,
    "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL": 1000 * 10,
}
FIRST_PASS = ("LIST_EXALL_40_ENTRIES_FIRST_PASS", "LIST_EXALL_400_ENTRIES_FIRST_PASS",
              "LIST_EXALL_1000_ENTRIES_FIRST_PASS")
# (repeat phase, first-pass phase, limit of r, limit of the lock-owned estimate)
GAIN = {"1000": ("LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_1000_ENTRIES_FIRST_PASS", 0.50, 0.55),
        "400": ("LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_400_ENTRIES_FIRST_PASS", 0.55, 0.60)}
FIRST_PASS_LIMIT = 1.05
OTHER_LIMIT = 1.05
OTHER_GEOMEAN_LIMIT = 1.02
RECORD_LIMIT = 32
RECORD_NAME_BYTES = 12  # AROS system median
RECORD_TOTAL_LIMIT = 64 * 1024


def verify(run_dir):
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    result = subprocess.run([str(VERIFIER), str(run_dir), "compare"],  # nosec B603
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"verifier failed for {run_dir}:\n{result.stdout}{result.stderr}")


def load(path):
    values = {}
    for line in path.read_text(encoding="ascii").splitlines():
        key, _, value = line.partition("\t")
        values[key] = value
    return values


def geomean(values):
    return math.exp(sum(math.log(v) for v in values) / len(values))


def memory_sizes():
    sizes = {}
    for line in MEMORY.read_text(encoding="ascii").splitlines()[1:]:
        name, _, value = line.partition("\t")
        sizes[name] = int(value)
    return sizes


def main():
    with (RESULTS / "schedule.tsv").open(encoding="ascii") as stream:
        schedule = list(csv.DictReader(stream, delimiter="\t"))
    if len(schedule) != STARTS:
        raise SystemExit(f"expected {STARTS} starts, found {len(schedule)}")
    runs = {}
    for row in schedule:
        run_dir = RESULTS / row["run_name"]
        verify(run_dir)
        results = run_dir / "system/Results"
        runs[(int(row["round"]), row["variant"])] = {
            "order": row["order"], "run": row["run_name"],
            "bfs": load(results / "bfs.tsv"), "pfs3": load(results / "pfs3.tsv")}
    rounds = sorted({key[0] for key in runs})
    if any((r, v) not in runs for r in rounds for v in VARIANTS):
        raise SystemExit("a round lacks a handler")
    phases = [key[:-3] for key in runs[(rounds[0], "reference")]["bfs"] if key.endswith("_US")]
    if set(EXALL) - set(phases):
        raise SystemExit("an expected ExAll phase is missing")

    ratios = {}
    for variant in VARIANTS:
        ratios[variant] = {}
        for phase in phases:
            per_start = []
            for r in rounds:
                run = runs[(r, variant)]
                per_start.append({"round": r, "order": run["order"],
                                  "bfs_us": int(run["bfs"][f"{phase}_US"]),
                                  "pfs3_us": int(run["pfs3"][f"{phase}_US"])})
            for item in per_start:
                item["ratio"] = item["bfs_us"] / item["pfs3_us"]
            by_order = {order: statistics.median(i["ratio"] for i in per_start
                                                 if i["order"] == order)
                        for order in ("bfs-first", "pfs3-first")}
            ratios[variant][phase] = {"starts": per_start,
                                      "median": statistics.median(i["ratio"] for i in per_start),
                                      "median_by_order": by_order}
        ratios[variant]["_geomean_of_medians"] = geomean(
            [ratios[variant][p]["median"] for p in phases])

    paired = {}
    for phase in phases:
        values = [int(runs[(r, "bound")]["bfs"][f"{phase}_US"]) /
                  int(runs[(r, "reference")]["bfs"][f"{phase}_US"]) for r in rounds]
        paired[phase] = {"ratios": values, "median": statistics.median(values),
                         "min": min(values), "max": max(values)}
    other = [p for p in phases if p not in EXALL]
    paired_geomean = {"all_phases": geomean([paired[p]["median"] for p in phases]),
                      "unchanged_phases": geomean([paired[p]["median"] for p in other]),
                      "unchanged_phase_count": len(other)}

    # Per-entry cost in microseconds: median over the starts of a handler (PFS3: over all starts).
    per_entry = {}
    for phase, visits in EXALL.items():
        per_entry[phase] = {
            "visits": visits,
            "reference_us": statistics.median(
                int(runs[(r, "reference")]["bfs"][f"{phase}_US"]) for r in rounds) / visits,
            "bound_us": statistics.median(
                int(runs[(r, "bound")]["bfs"][f"{phase}_US"]) for r in rounds) / visits,
            "pfs3_us": statistics.median(
                int(runs[key]["pfs3"][f"{phase}_US"]) for key in runs) / visits}

    # Lock-owned estimate: one pass fills at the cost of a first pass, nine hit.
    estimate = {}
    for size, (repeat, first, _, _) in GAIN.items():
        r, f = paired[repeat]["median"], paired[first]["median"]
        estimate[size] = {"r": r, "f": f, "lock_owned": 0.1 * f + 0.9 * r}

    sizes = memory_sizes()
    record = sizes["candidate_record_bytes"]
    total = 1000 * (record + RECORD_NAME_BYTES)
    criteria = {}
    for size, (repeat, first, r_limit, est_limit) in GAIN.items():
        criteria[f"gain_{size}"] = {
            "r": estimate[size]["r"], "r_limit": r_limit,
            "lock_owned": estimate[size]["lock_owned"], "lock_owned_limit": est_limit,
            "met": estimate[size]["r"] <= r_limit and estimate[size]["lock_owned"] <= est_limit}
    criteria["first_listing"] = {
        "ratios": {p: paired[p]["median"] for p in FIRST_PASS}, "limit": FIRST_PASS_LIMIT,
        "met": all(paired[p]["median"] <= FIRST_PASS_LIMIT for p in FIRST_PASS)}
    criteria["no_regression"] = {
        "worst_phase": max(other, key=lambda p: paired[p]["median"]),
        "worst_ratio": max(paired[p]["median"] for p in other), "limit": OTHER_LIMIT,
        "geomean": paired_geomean["unchanged_phases"], "geomean_limit": OTHER_GEOMEAN_LIMIT,
        "misses": [p for p in other if paired[p]["median"] > OTHER_LIMIT],
        "met": all(paired[p]["median"] <= OTHER_LIMIT for p in other) and
               paired_geomean["unchanged_phases"] <= OTHER_GEOMEAN_LIMIT}
    criteria["memory"] = {
        "record_bytes": record, "record_limit": RECORD_LIMIT,
        "bytes_for_1000_entries": total, "total_limit": RECORD_TOTAL_LIMIT,
        "name_bytes_assumed": RECORD_NAME_BYTES,
        "met": record <= RECORD_LIMIT and total < RECORD_TOTAL_LIMIT}
    criteria["valid_runs"] = {"starts": len(runs), "met": len(runs) == STARTS}
    criteria["all_met"] = all(c["met"] for c in criteria.values())

    summary = {"phases": phases, "bfs_over_pfs3": ratios, "paired_bound_over_reference": paired,
               "paired_geomean": paired_geomean, "per_entry_us": per_entry,
               "lock_owned_estimate": estimate, "memory_bytes": sizes,
               "decision_rule": criteria, "verifier": "passed", "starts": len(runs)}
    (BUNDLE / "summary.json").write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n",
                                         encoding="ascii")
    print_report(summary, phases)


def print_report(summary, phases):
    ratios = summary["bfs_over_pfs3"]
    paired = summary["paired_bound_over_reference"]
    print(f"{'phase':42} {'ref':>6} {'bound':>6} {'b/ref':>6} {'min':>6} {'max':>6}")
    for phase in phases:
        print(f"{phase:42} {ratios['reference'][phase]['median']:6.2f} "
              f"{ratios['bound'][phase]['median']:6.2f} {paired[phase]['median']:6.3f} "
              f"{paired[phase]['min']:6.3f} {paired[phase]['max']:6.3f}")
    print(f"geomean BFS/PFS3 of medians: reference {ratios['reference']['_geomean_of_medians']:.3f}, "
          f"bound {ratios['bound']['_geomean_of_medians']:.3f}")
    print(f"geomean of paired medians: all {summary['paired_geomean']['all_phases']:.3f}, "
          f"{summary['paired_geomean']['unchanged_phase_count']} unchanged phases "
          f"{summary['paired_geomean']['unchanged_phases']:.3f}")
    print("per entry (us): phase reference bound pfs3")
    for phase, item in summary["per_entry_us"].items():
        print(f"  {phase:42} {item['reference_us']:7.1f} {item['bound_us']:7.1f} {item['pfs3_us']:7.1f}")
    print("lock-owned estimate:", json.dumps(summary["lock_owned_estimate"], sort_keys=True))
    print("memory bytes:", json.dumps(summary["memory_bytes"], sort_keys=True))
    print("decision rule:")
    for name, item in summary["decision_rule"].items():
        if name == "all_met":
            print(f"  all criteria met: {item}")
        else:
            print(f"  {name}: {'met' if item['met'] else 'NOT met'} {json.dumps(item, sort_keys=True)}")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
