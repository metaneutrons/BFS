#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the repeat of the ExAll result cache main series.

Runs the repository's strict verifier on all 16 compare starts of the repeat
(reference against bound, as in the first main series of 22 October) and
evaluates, with the limits of docs/plans/bfs-exall-result-cache-v1.md:

  repeat   the decision rule on the repeat alone,
  pooled   the same rule on both main series together (16 paired rounds),
  compare  the repeat against the first series per phase, in particular the two
           phases that missed in the first series.

It also estimates the position and name effect and the sampling spread of the
statistics by a bootstrap over the rounds, and the round-to-round scatter, for
the repeat and the pooled data, as the null series did. Writes summary.json and
prints the same figures as text.

    summarize.py [--repeat DIR] [--first DIR] [--out FILE] [--no-verify-first]
"""
import argparse
import csv
import json
import math
from pathlib import Path
import random
import statistics
import subprocess  # nosec B404 - fixed repository verifier, no shell
import sys

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[4]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
MEMORY = BUNDLE.parent / "memory-probe.txt"
STARTS = 16

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
GAIN = {"1000": ("LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_1000_ENTRIES_FIRST_PASS", 0.50, 0.55),
        "400": ("LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_400_ENTRIES_FIRST_PASS", 0.55, 0.60)}
FIRST_PASS_LIMIT = 1.05
OTHER_LIMIT = 1.05
OTHER_GEOMEAN_LIMIT = 1.02
RECORD_LIMIT = 32
RECORD_NAME_BYTES = 12
RECORD_TOTAL_LIMIT = 64 * 1024
# The two phases that missed in the first series: a miss reproduces when the
# repeat's median paired ratio of the phase exceeds the limit again.
FIRST_SERIES_MISSES = ("LIST_EXALL_40_ENTRIES_FIRST_PASS", "LIST_EXNEXT_400")
VARIANTS = ("reference", "bound")


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


def load_series(results, label, run_verifier):
    with (results / "schedule.tsv").open(encoding="ascii") as stream:
        schedule = list(csv.DictReader(stream, delimiter="\t"))
    if len(schedule) != STARTS:
        raise SystemExit(f"{label}: expected {STARTS} starts, found {len(schedule)}")
    starts = {}
    for row in schedule:
        run_dir = results / row["run_name"]
        if run_verifier:
            verify(run_dir)
        files = run_dir / "system/Results"
        starts[(int(row["round"]), row["variant"])] = {
            "order": row["order"], "bfs": load(files / "bfs.tsv"), "pfs3": load(files / "pfs3.tsv")}
    rounds = []
    for number in sorted({key[0] for key in starts}):
        if any((number, v) not in starts for v in VARIANTS):
            raise SystemExit(f"{label}: round {number} lacks a handler")
        rounds.append({"series": label, "round": number,
                       "reference": starts[(number, "reference")],
                       "bound": starts[(number, "bound")]})
    return rounds


def memory_sizes():
    sizes = {}
    for line in MEMORY.read_text(encoding="ascii").splitlines()[1:]:
        name, _, value = line.partition("\t")
        sizes[name] = int(value)
    return sizes


def analyse(rounds, phases, sizes):
    """Ratios, paired ratios, per-entry costs and the decision rule for a list of rounds."""
    ratios = {}
    for variant in VARIANTS:
        ratios[variant] = {}
        for phase in phases:
            values = [int(r[variant]["bfs"][f"{phase}_US"]) / int(r[variant]["pfs3"][f"{phase}_US"])
                      for r in rounds]
            ratios[variant][phase] = {"median": statistics.median(values), "values": values}
        ratios[variant]["_geomean_of_medians"] = geomean(
            [ratios[variant][p]["median"] for p in phases])
    paired = {}
    for phase in phases:
        values = [int(r["bound"]["bfs"][f"{phase}_US"]) / int(r["reference"]["bfs"][f"{phase}_US"])
                  for r in rounds]
        paired[phase] = {"ratios": values, "median": statistics.median(values),
                         "min": min(values), "max": max(values)}
    unchanged = [p for p in phases if p not in EXALL]
    paired_geomean = {"all_phases": geomean([paired[p]["median"] for p in phases]),
                      "unchanged_phases": geomean([paired[p]["median"] for p in unchanged]),
                      "unchanged_phase_count": len(unchanged)}
    per_entry = {}
    for phase, visits in EXALL.items():
        per_entry[phase] = {
            "visits": visits,
            "reference_us": statistics.median(int(r["reference"]["bfs"][f"{phase}_US"]) for r in rounds) / visits,
            "bound_us": statistics.median(int(r["bound"]["bfs"][f"{phase}_US"]) for r in rounds) / visits,
            "pfs3_us": statistics.median(int(r[v]["pfs3"][f"{phase}_US"])
                                         for r in rounds for v in VARIANTS) / visits}
    estimate = {}
    for size, (repeat, first, _, _) in GAIN.items():
        r, f = paired[repeat]["median"], paired[first]["median"]
        estimate[size] = {"r": r, "f": f, "lock_owned": 0.1 * f + 0.9 * r}
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
        "worst_phase": max(unchanged, key=lambda p: paired[p]["median"]),
        "worst_ratio": max(paired[p]["median"] for p in unchanged), "limit": OTHER_LIMIT,
        "geomean": paired_geomean["unchanged_phases"], "geomean_limit": OTHER_GEOMEAN_LIMIT,
        "misses": [p for p in unchanged if paired[p]["median"] > OTHER_LIMIT],
        "met": all(paired[p]["median"] <= OTHER_LIMIT for p in unchanged) and
               paired_geomean["unchanged_phases"] <= OTHER_GEOMEAN_LIMIT}
    criteria["memory"] = {
        "record_bytes": record, "record_limit": RECORD_LIMIT,
        "bytes_for_1000_entries": total, "total_limit": RECORD_TOTAL_LIMIT,
        "name_bytes_assumed": RECORD_NAME_BYTES,
        "met": record <= RECORD_LIMIT and total < RECORD_TOTAL_LIMIT}
    criteria["valid_runs"] = {"starts_per_handler": len(rounds), "met": True}
    criteria["all_met"] = all(c["met"] for c in criteria.values())
    return {"rounds": len(rounds), "bfs_over_pfs3": ratios, "paired": paired,
            "paired_geomean": paired_geomean, "per_entry_us": per_entry,
            "lock_owned_estimate": estimate, "decision_rule": criteria}


def effects(rounds, paired, phases):
    """Position and name effect from the alternation, the round-to-round scatter and a bootstrap."""
    unchanged = [p for p in phases if p not in EXALL]
    odd = [i for i, r in enumerate(rounds) if r["round"] % 2 == 1]
    even = [i for i, r in enumerate(rounds) if r["round"] % 2 == 0]
    per_phase = {}
    for phase in phases:
        r_odd = statistics.median(paired[phase]["ratios"][i] for i in odd)
        r_even = statistics.median(paired[phase]["ratios"][i] for i in even)
        per_phase[phase] = (math.sqrt(r_odd / r_even), math.sqrt(r_odd * r_even))
    by_round = [geomean(paired[p]["ratios"][i] for p in unchanged) for i in range(len(rounds))]
    rng = random.Random(20261023)
    keep = {"g16": [], "w16": [], "first_pass_max": [], "position": [], "name": []}
    draws = 5000
    for _ in range(draws):
        odd_pick = [rng.choice(odd) for _ in odd]
        even_pick = [rng.choice(even) for _ in even]
        picked = odd_pick + even_pick
        medians = {p: statistics.median(paired[p]["ratios"][i] for i in picked) for p in phases}
        keep["g16"].append(geomean(medians[p] for p in unchanged))
        keep["w16"].append(max(medians[p] for p in unchanged))
        keep["first_pass_max"].append(max(medians[p] for p in FIRST_PASS))
        pos, name = [], []
        for p in unchanged:
            r_odd = statistics.median(paired[p]["ratios"][i] for i in odd_pick)
            r_even = statistics.median(paired[p]["ratios"][i] for i in even_pick)
            pos.append(math.sqrt(r_odd / r_even))
            name.append(math.sqrt(r_odd * r_even))
        keep["position"].append(geomean(pos))
        keep["name"].append(geomean(name))

    def interval(values):
        values = sorted(values)
        return {"low": values[int(0.025 * len(values))], "median": values[len(values) // 2],
                "high": values[int(0.975 * len(values))]}

    return {
        "position_effect_unchanged_16": geomean(per_phase[p][0] for p in unchanged),
        "name_effect_unchanged_16": geomean(per_phase[p][1] for p in unchanged),
        "per_round_geomean_unchanged_16": by_round,
        "round_to_round_sd_of_log": statistics.stdev(math.log(v) for v in by_round),
        "per_round_min": min(by_round), "per_round_max": max(by_round),
        "bootstrap": {"draws": draws, "g16": interval(keep["g16"]), "w16": interval(keep["w16"]),
                      "first_pass_max": interval(keep["first_pass_max"]),
                      "position_effect_16": interval(keep["position"]),
                      "name_effect_16": interval(keep["name"]),
                      "share_g16_above_limit": sum(v > OTHER_GEOMEAN_LIMIT for v in keep["g16"]) / draws,
                      "share_w16_above_limit": sum(v > OTHER_LIMIT for v in keep["w16"]) / draws,
                      "share_first_pass_above_limit":
                          sum(v > FIRST_PASS_LIMIT for v in keep["first_pass_max"]) / draws}}


def compare(first, repeat, phases):
    table = {}
    for phase in phases:
        a, b = first["paired"][phase], repeat["paired"][phase]
        table[phase] = {
            "first_median": a["median"], "repeat_median": b["median"],
            "difference": b["median"] - a["median"],
            "first_range": [a["min"], a["max"]], "repeat_range": [b["min"], b["max"]],
            "repeat_inside_first_range": a["min"] <= b["median"] <= a["max"],
            "first_bfs_over_pfs3": {v: first["bfs_over_pfs3"][v][phase]["median"] for v in VARIANTS},
            "repeat_bfs_over_pfs3": {v: repeat["bfs_over_pfs3"][v][phase]["median"] for v in VARIANTS}}
    misses = {p: {"first": table[p]["first_median"], "repeat": table[p]["repeat_median"],
                  "limit": OTHER_LIMIT if p not in FIRST_PASS else FIRST_PASS_LIMIT,
                  "reproduces": table[p]["repeat_median"] > OTHER_LIMIT}
              for p in FIRST_SERIES_MISSES}
    return {"per_phase": table, "first_series_misses": misses}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repeat", type=Path, default=BUNDLE / "results")
    parser.add_argument("--first", type=Path, default=BUNDLE.parent / "results")
    parser.add_argument("--out", type=Path, default=BUNDLE / "summary.json")
    parser.add_argument("--no-verify-first", action="store_true")
    arguments = parser.parse_args()
    repeat_rounds = load_series(arguments.repeat, "repeat", True)
    first_rounds = load_series(arguments.first, "first", not arguments.no_verify_first)
    phases = [k[:-3] for k in repeat_rounds[0]["reference"]["bfs"] if k.endswith("_US")]
    if set(EXALL) - set(phases) or set(FIRST_SERIES_MISSES) - set(phases):
        raise SystemExit("an expected phase is missing")
    sizes = memory_sizes()
    sections = {}
    for name, rounds in (("first", first_rounds), ("repeat", repeat_rounds),
                         ("pooled", first_rounds + repeat_rounds)):
        sections[name] = analyse(rounds, phases, sizes)
        sections[name]["effects"] = effects(rounds, sections[name]["paired"], phases)
    sections["repeat"]["decision_rule"]["valid_runs"]["met"] = True
    sections["pooled"]["decision_rule"]["valid_runs"]["starts_per_handler"] = len(first_rounds) + len(repeat_rounds)
    summary = {"phases": phases, "sections": sections,
               "compare": compare(sections["first"], sections["repeat"], phases),
               "verifier": "passed", "memory_bytes": sizes}
    arguments.out.write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n", encoding="ascii")
    print_report(summary, phases)


def print_report(summary, phases):
    sections = summary["sections"]
    for name in ("repeat", "pooled"):
        section = sections[name]
        print(f"== {name} ({section['rounds']} rounds) ==")
        print(f"{'phase':42} {'ref':>6} {'bound':>6} {'b/ref':>6} {'min':>6} {'max':>6}")
        for phase in phases:
            print(f"{phase:42} {section['bfs_over_pfs3']['reference'][phase]['median']:6.2f} "
                  f"{section['bfs_over_pfs3']['bound'][phase]['median']:6.2f} "
                  f"{section['paired'][phase]['median']:6.3f} {section['paired'][phase]['min']:6.3f} "
                  f"{section['paired'][phase]['max']:6.3f}")
        print("geomean BFS/PFS3 of medians:",
              {v: round(section["bfs_over_pfs3"][v]["_geomean_of_medians"], 3) for v in VARIANTS})
        print("geomean of paired medians:", {k: round(v, 3) if isinstance(v, float) else v
                                             for k, v in section["paired_geomean"].items()})
        print("per entry (us): phase reference bound pfs3")
        for phase, item in section["per_entry_us"].items():
            print(f"  {phase:42} {item['reference_us']:7.1f} {item['bound_us']:7.1f} {item['pfs3_us']:7.1f}")
        print("lock-owned estimate:", json.dumps(section["lock_owned_estimate"], sort_keys=True))
        print("decision rule:")
        for key, item in section["decision_rule"].items():
            if key == "all_met":
                print(f"  all criteria met: {item}")
            else:
                print(f"  {key}: {'met' if item['met'] else 'NOT met'} {json.dumps(item, sort_keys=True)}")
        e = section["effects"]
        print(f"effects over the 16 unchanged phases: position {e['position_effect_unchanged_16']:.4f}, "
              f"name {e['name_effect_unchanged_16']:.4f}, per-round geomean {e['per_round_min']:.3f}-"
              f"{e['per_round_max']:.3f}, sd of log {e['round_to_round_sd_of_log']:.4f}")
        print("  per round:", [round(v, 3) for v in e["per_round_geomean_unchanged_16"]])
        b = e["bootstrap"]
        for key in ("g16", "w16", "first_pass_max", "position_effect_16", "name_effect_16"):
            print(f"  bootstrap {key}: {b[key]['low']:.3f} / {b[key]['median']:.3f} / {b[key]['high']:.3f}")
        print(f"  share above the limit: geomean {b['share_g16_above_limit']:.3f}, "
              f"worst of 16 {b['share_w16_above_limit']:.3f}, first-pass {b['share_first_pass_above_limit']:.3f}")
    print("== first series, same analysis (the numbers of 22 October) ==")
    first = sections["first"]
    print("  decision rule met:", {k: v["met"] for k, v in first["decision_rule"].items() if k != "all_met"})
    print("== repeat against first series ==")
    print(f"{'phase':42} {'first':>6} {'repeat':>6} {'diff':>7} {'inside first range':>19}")
    for phase in phases:
        c = summary["compare"]["per_phase"][phase]
        print(f"{phase:42} {c['first_median']:6.3f} {c['repeat_median']:6.3f} {c['difference']:+7.3f} "
              f"{str(c['repeat_inside_first_range']):>19}")
    print("misses of the first series:", json.dumps(summary["compare"]["first_series_misses"], sort_keys=True))
    sys.stdout.flush()


if __name__ == "__main__":
    main()
