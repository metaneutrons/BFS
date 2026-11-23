#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 22 October ExAll result cache null series.

Runs the repository's strict verifier on all 16 compare starts of one handler
under two names (reference-a, reference-b; identical bytes), reports the
BFS / PFS3 ratio of both names per phase and the paired ratio b over a in the
same round, and evaluates the limits of criteria 3 and 4 of
docs/plans/bfs-exall-result-cache-v1.md on those ratios, which all belong to
unchanged code. It also estimates the position effect (second start of a round
over the first) from the alternation of the order, and the sampling spread of
the statistics of criteria 3 and 4 by a bootstrap over the rounds. Writes
summary.json and prints the same figures as text.

    summarize.py [--results DIR] [--names A B] [--out FILE]

The defaults are this series. The same analysis can be run on another series
with two names whose rounds alternate the order in the same way.
"""
import argparse
import random
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess  # nosec B404 - fixed repository verifier, no shell
import sys

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[4]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
RESULTS = BUNDLE / "results"
VARIANTS = ("reference-a", "reference-b")
STARTS = 16

EXALL = ("LIST_EXALL_400", "LIST_EXALL_40_ENTRIES_FIRST_PASS",
         "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_400_ENTRIES_FIRST_PASS",
         "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_1000_ENTRIES_FIRST_PASS",
         "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL")
FIRST_PASS = ("LIST_EXALL_40_ENTRIES_FIRST_PASS", "LIST_EXALL_400_ENTRIES_FIRST_PASS",
              "LIST_EXALL_1000_ENTRIES_FIRST_PASS")
LIMIT = 1.05
GEOMEAN_LIMIT = 1.02


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
    values = list(values)
    return math.exp(sum(math.log(v) for v in values) / len(values))


def median_effects(ratio_lists, odd, even):
    """Per-phase name and position effect from the medians of the odd and even rounds."""
    out = {}
    for phase, values in ratio_lists.items():
        r_odd = statistics.median(values[i] for i in odd)
        r_even = statistics.median(values[i] for i in even)
        out[phase] = {"odd_rounds": r_odd, "even_rounds": r_even,
                      "position_effect": math.sqrt(r_odd / r_even),
                      "name_effect": math.sqrt(r_odd * r_even)}
    return out


def bootstrap(paired, phases, unchanged, odd, even, draws=5000):
    """Resample the rounds, four odd and four even each time, as the design has them."""
    rng = random.Random(20261022)
    keep = {"g16": [], "w16": [], "first_pass_max": [], "position": [], "name": []}
    for _ in range(draws):
        odd_pick = [rng.choice(odd) for _ in odd]
        even_pick = [rng.choice(even) for _ in even]
        picked = odd_pick + even_pick
        medians = {p: statistics.median(paired[p]["ratios"][i] for i in picked) for p in phases}
        keep["g16"].append(geomean(medians[p] for p in unchanged))
        keep["w16"].append(max(medians[p] for p in unchanged))
        keep["first_pass_max"].append(max(medians[p] for p in FIRST_PASS))
        effects = []
        for p in unchanged:
            r_odd = statistics.median(paired[p]["ratios"][i] for i in odd_pick)
            r_even = statistics.median(paired[p]["ratios"][i] for i in even_pick)
            effects.append((math.sqrt(r_odd / r_even), math.sqrt(r_odd * r_even)))
        keep["position"].append(geomean(e[0] for e in effects))
        keep["name"].append(geomean(e[1] for e in effects))

    def interval(values):
        values = sorted(values)
        return {"low": values[int(0.025 * len(values))], "median": values[len(values) // 2],
                "high": values[int(0.975 * len(values))]}

    return {"draws": draws, "g16": interval(keep["g16"]), "w16": interval(keep["w16"]),
            "first_pass_max": interval(keep["first_pass_max"]),
            "position_effect_16": interval(keep["position"]),
            "name_effect_16": interval(keep["name"]),
            "share_g16_above_limit": sum(v > GEOMEAN_LIMIT for v in keep["g16"]) / draws,
            "share_w16_above_limit": sum(v > LIMIT for v in keep["w16"]) / draws,
            "share_first_pass_above_limit": sum(v > LIMIT for v in keep["first_pass_max"]) / draws}


def main():
    global RESULTS, VARIANTS
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, default=RESULTS)
    parser.add_argument("--names", nargs=2, default=list(VARIANTS))
    parser.add_argument("--out", type=Path, default=BUNDLE / "summary.json")
    arguments = parser.parse_args()
    RESULTS, VARIANTS = arguments.results, tuple(arguments.names)
    first, second = VARIANTS
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
    phases = [key[:-3] for key in runs[(rounds[0], first)]["bfs"] if key.endswith("_US")]
    if set(EXALL) - set(phases):
        raise SystemExit("an expected ExAll phase is missing")
    unchanged = [p for p in phases if p not in EXALL]

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
            ratios[variant][phase] = {"starts": per_start,
                                      "median": statistics.median(i["ratio"] for i in per_start)}
        ratios[variant]["_geomean_of_medians"] = geomean(
            [ratios[variant][p]["median"] for p in phases])

    # b over a in the same round. Odd rounds run a first, even rounds b first.
    paired = {}
    for phase in phases:
        values = [int(runs[(r, second)]["bfs"][f"{phase}_US"]) /
                  int(runs[(r, first)]["bfs"][f"{phase}_US"]) for r in rounds]
        paired[phase] = {"ratios": values, "median": statistics.median(values),
                         "min": min(values), "max": max(values)}

    def summary_of(names):
        medians = [paired[p]["median"] for p in names]
        worst = max(names, key=lambda p: paired[p]["median"])
        lowest = min(names, key=lambda p: paired[p]["median"])
        return {"count": len(names), "geomean": geomean(medians),
                "worst_phase": worst, "worst": paired[worst]["median"],
                "lowest_phase": lowest, "lowest": paired[lowest]["median"],
                "above_limit": [p for p in names if paired[p]["median"] > LIMIT],
                "below_inverse_limit": [p for p in names if paired[p]["median"] < 1 / LIMIT]}

    # Position effect. In odd rounds b is the second start, in even rounds the
    # first, so r_odd = H * P and r_even = H / P for the name effect H and the
    # position effect P (second over first). The order of the filesystems is
    # the same for both starts of a round and cancels. Medians, as the rule uses
    # them; the means of the logarithms are kept for comparison.
    odd = [i for i, r in enumerate(rounds) if r % 2 == 1]
    even = [i for i, r in enumerate(rounds) if r % 2 == 0]

    def per_round_geomean(names):
        return {r: geomean(paired[p]["ratios"][i] for p in names) for i, r in enumerate(rounds)}

    def log_mean_effects(names):
        by_round = per_round_geomean(names)
        logs_odd = [math.log(by_round[rounds[i]]) for i in odd]
        logs_even = [math.log(by_round[rounds[i]]) for i in even]
        mean_odd, mean_even = statistics.mean(logs_odd), statistics.mean(logs_even)
        return {"per_round_geomean": by_round,
                "position_effect_second_over_first": math.exp((mean_odd - mean_even) / 2),
                "name_effect_b_over_a": math.exp((mean_odd + mean_even) / 2),
                "round_to_round_sd_of_log": statistics.stdev(math.log(v) for v in by_round.values()),
                "min_round": min(by_round.values()), "max_round": max(by_round.values())}

    per_phase = median_effects({p: paired[p]["ratios"] for p in phases}, odd, even)
    position = {
        "median_based_unchanged_16": {
            "position_effect_second_over_first": geomean(per_phase[p]["position_effect"] for p in unchanged),
            "name_effect_b_over_a": geomean(per_phase[p]["name_effect"] for p in unchanged),
            "phases_with_second_slower": sum(per_phase[p]["position_effect"] > 1 for p in unchanged)},
        "median_based_all_23": {
            "position_effect_second_over_first": geomean(per_phase[p]["position_effect"] for p in phases),
            "name_effect_b_over_a": geomean(per_phase[p]["name_effect"] for p in phases),
            "phases_with_second_slower": sum(per_phase[p]["position_effect"] > 1 for p in phases)},
        "log_mean_unchanged_16": log_mean_effects(unchanged),
        "log_mean_all_23": log_mean_effects(phases),
        "per_phase": per_phase}
    sampling = bootstrap(paired, phases, unchanged, odd, even)

    criteria = {
        "criterion_3_first_pass_each": {
            "ratios": {p: paired[p]["median"] for p in FIRST_PASS}, "limit": LIMIT,
            "met": all(paired[p]["median"] <= LIMIT for p in FIRST_PASS)},
        "criterion_4_unchanged_each_and_geomean": {
            "worst_phase": summary_of(unchanged)["worst_phase"],
            "worst": summary_of(unchanged)["worst"], "limit": LIMIT,
            "geomean": summary_of(unchanged)["geomean"], "geomean_limit": GEOMEAN_LIMIT,
            "met": summary_of(unchanged)["worst"] <= LIMIT and
                   summary_of(unchanged)["geomean"] <= GEOMEAN_LIMIT},
        "every_phase_each": {"limit": LIMIT, "met": not summary_of(phases)["above_limit"]}}
    criteria["both_met"] = (criteria["criterion_3_first_pass_each"]["met"] and
                            criteria["criterion_4_unchanged_each_and_geomean"]["met"])

    summary = {"phases": phases, "bfs_over_pfs3": ratios, "paired_b_over_a": paired,
               "unchanged_16": summary_of(unchanged), "all_23": summary_of(phases),
               "names": [first, second], "position": position, "bootstrap": sampling,
               "criteria": criteria, "verifier": "passed",
               "starts": len(runs)}
    arguments.out.write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n",
                             encoding="ascii")
    print_report(summary, phases)


def print_report(summary, phases):
    ratios = summary["bfs_over_pfs3"]
    paired = summary["paired_b_over_a"]
    first, second = summary["names"]
    print(f"names: a = {first}, b = {second}")
    print(f"{'phase':42} {'a':>6} {'b':>6} {'b/a':>6} {'min':>6} {'max':>6}")
    for phase in phases:
        print(f"{phase:42} {ratios[first][phase]['median']:6.2f} "
              f"{ratios[second][phase]['median']:6.2f} {paired[phase]['median']:6.3f} "
              f"{paired[phase]['min']:6.3f} {paired[phase]['max']:6.3f}")
    for name in ("unchanged_16", "all_23"):
        item = summary[name]
        print(f"{name}: geomean {item['geomean']:.3f}, worst {item['worst_phase']} {item['worst']:.3f}, "
              f"lowest {item['lowest_phase']} {item['lowest']:.3f}, "
              f"above 1.05: {item['above_limit']}, below 1/1.05: {item['below_inverse_limit']}")
    for name in ("median_based_unchanged_16", "median_based_all_23"):
        item = summary["position"][name]
        print(f"position ({name}): second over first {item['position_effect_second_over_first']:.4f}, "
              f"name b over a {item['name_effect_b_over_a']:.4f}, "
              f"phases with the second start slower {item['phases_with_second_slower']}")
    for name in ("log_mean_unchanged_16", "log_mean_all_23"):
        item = summary["position"][name]
        print(f"position ({name}): second over first {item['position_effect_second_over_first']:.4f}, "
              f"name b over a {item['name_effect_b_over_a']:.4f}, "
              f"round-to-round sd of log {item['round_to_round_sd_of_log']:.4f}, "
              f"per-round geomean {item['min_round']:.3f}-{item['max_round']:.3f}")
        print("  per round:", {r: round(v, 3) for r, v in item["per_round_geomean"].items()})
    boot = summary["bootstrap"]
    print(f"bootstrap over rounds ({boot['draws']} draws, 4 odd and 4 even rounds each):")
    for key in ("g16", "w16", "first_pass_max", "position_effect_16", "name_effect_16"):
        item = boot[key]
        print(f"  {key}: {item['low']:.3f} / {item['median']:.3f} / {item['high']:.3f}")
    print(f"  share above the limit: geomean {boot['share_g16_above_limit']:.3f}, "
          f"worst of 16 {boot['share_w16_above_limit']:.3f}, "
          f"first-pass ExAll {boot['share_first_pass_above_limit']:.3f}")
    print("criteria:", json.dumps(summary["criteria"], sort_keys=True))
    sys.stdout.flush()


if __name__ == "__main__":
    main()
