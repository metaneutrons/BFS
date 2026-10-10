#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 10 October release flags pilot.

Runs the repository's strict verifier on all 24 compare starts, reports the
BFS / PFS3 ratio of every handler and phase, the paired ratios of -O2 over -Os
on the 68040 and of the 68040 over the 68020 flag at -O2, and evaluates the
decision rule fixed in PILOT_SCOPE.md.
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
VARIANTS = ("o2-020", "o2-040", "os-040")
STARTS = 24
GEOMEAN_BOUND = 0.97
PHASE_BOUND = 1.10


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
    phases = [key[:-3] for key in runs[(rounds[0], "os-040")]["bfs"] if key.endswith("_US")]

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

    paired = {}
    for name, variant, base in (("o2_over_os_040", "o2-040", "os-040"),
                                ("m68040_over_m68020_o2", "o2-040", "o2-020")):
        paired[name] = {}
        for phase in phases:
            values = [int(runs[(r, variant)]["bfs"][f"{phase}_US"]) /
                      int(runs[(r, base)]["bfs"][f"{phase}_US"]) for r in rounds]
            paired[name][phase] = {"ratios": values, "median": statistics.median(values),
                                   "min": min(values), "max": max(values)}
        medians = [paired[name][p]["median"] for p in phases]
        paired[name]["_geomean_of_medians"] = geomean(medians)

    cand = paired["o2_over_os_040"]
    slow = [p for p in phases if cand[p]["median"] >= PHASE_BOUND]
    retention = {"verifier": "passed", "starts": len(runs),
                 "geomean_of_medians": cand["_geomean_of_medians"],
                 "geomean_bound": GEOMEAN_BOUND, "phases_at_or_above_bound": slow,
                 "phase_bound": PHASE_BOUND,
                 "switch_release_to_o2": cand["_geomean_of_medians"] <= GEOMEAN_BOUND and not slow}
    summary = {"phases": phases, "bfs_over_pfs3": ratios, "paired": paired,
               "decision": retention}
    json.dump(summary, sys.stdout, indent=1, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
