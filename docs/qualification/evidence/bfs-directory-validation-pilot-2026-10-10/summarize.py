#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 10 October directory validation pilot.

Runs the repository's strict verifier on both deep-compare runs of the
candidate and on the two runs of the directory layout pilot (the reference),
reports the listing phases, and evaluates the retention rule fixed in
PILOT_SCOPE.md. Sampled intervals are inclusive and nested; they are never
added, subtracted or scaled here.
"""
import json
from pathlib import Path
import subprocess  # nosec B404 - fixed repository verifier, no shell
import sys

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[3]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
REFERENCE = ROOT / "docs/qualification/evidence/bfs-directory-layout-pilot-2026-10-10/results"
RUNS = {
    "candidate": [BUNDLE / "results/dirvalid-pilot-1-bfs-first",
                  BUNDLE / "results/dirvalid-pilot-2-pfs3-first"],
    "reference": [REFERENCE / "dirlayout-pilot-1-bfs-first",
                  REFERENCE / "dirlayout-pilot-2-pfs3-first"],
}
CLOCK_HZ = 709379
LISTING_PHASES = (
    "LIST_EXALL_1000_ENTRIES_FIRST_PASS", "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL",
    "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL",
    "LIST_EXNEXT_1000_ENTRIES_FIRST_PASS", "LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL",
    "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL",
)
COUNT_SUFFIXES = ("_CALLS", "_READS", "_WRITES", "_UPDATES", "_COMMITS", "_VIEWS",
                  "_HITS", "_MISSES", "_MAPS", "_ALLOCS")
SCOPES = ("DIR_NODE_VIEW", "NODE_STRUCTURE", "DIR_BINARY_SEARCH", "EXALL_FILL", "INODE_READ")
REGRESSION = 1.10


def verify(run_dir):
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    result = subprocess.run([str(VERIFIER), str(run_dir), "deep-compare"],  # nosec B603
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"verifier failed for {run_dir}:\n{result.stdout}{result.stderr}")


def load(path):
    values = {}
    for line in path.read_text(encoding="ascii").splitlines():
        key, _, value = line.partition("\t")
        values[key] = value
    return values


def counts(values):
    return {key: value for key, value in values.items() if key.endswith(COUNT_SUFFIXES)}


def mean_us(values, phase, scope):
    samples = int(values.get(f"{phase}_DETAIL_{scope}_SAMPLES", "0"))
    ticks = int(values.get(f"{phase}_DETAIL_{scope}_SAMPLE_TICKS", "0"))
    return ticks / samples / CLOCK_HZ * 1e6 if samples else None


def main():
    data = {}
    for variant, run_dirs in RUNS.items():
        data[variant] = []
        for run_dir in run_dirs:
            verify(run_dir)
            results = run_dir / "system/Results"
            data[variant].append((run_dir.name, load(results / "bfs.deep-compare.tsv"),
                                  load(results / "pfs3.deep-compare.tsv")))
    # Amended rule 2: compare the fields on which the reference runs agree.
    reference_counts = [counts(bfs) for _, bfs, _ in data["reference"]]
    keys = sorted(set(reference_counts[0]) | set(reference_counts[1]))
    unstable = [key for key in keys if reference_counts[0].get(key) != reference_counts[1].get(key)]
    count_mismatches = []
    unstable_values = []
    for name, bfs, _ in data["candidate"]:
        candidate_counts = counts(bfs)
        for key in sorted(set(candidate_counts) | set(keys)):
            if key in unstable:
                unstable_values.append({"run": name, "field": key,
                                        "reference": [reference_counts[0].get(key),
                                                      reference_counts[1].get(key)],
                                        "candidate": candidate_counts.get(key)})
            elif candidate_counts.get(key) != reference_counts[0].get(key):
                count_mismatches.append({"run": name, "field": key,
                                         "reference": reference_counts[0].get(key),
                                         "candidate": candidate_counts.get(key)})
    phases = {}
    regressions = []
    for phase in LISTING_PHASES:
        rows = []
        slower_in_both = True
        for (cname, cbfs, cpfs3), (rname, rbfs, rpfs3) in zip(data["candidate"], data["reference"]):
            c_us, r_us = int(cbfs[f"{phase}_US"]), int(rbfs[f"{phase}_US"])
            ratio = c_us / r_us
            if ratio < REGRESSION:
                slower_in_both = False
            rows.append({"candidate_run": cname, "reference_run": rname,
                         "candidate_us": c_us, "reference_us": r_us,
                         "candidate_over_reference": ratio,
                         "candidate_pfs3_us": int(cpfs3[f"{phase}_US"]),
                         "reference_pfs3_us": int(rpfs3[f"{phase}_US"]),
                         "candidate_means_us": {s: mean_us(cbfs, phase, s) for s in SCOPES},
                         "reference_means_us": {s: mean_us(rbfs, phase, s) for s in SCOPES}})
        if slower_in_both:
            regressions.append(phase)
        phases[phase] = rows
    summary = {"clock_hz": CLOCK_HZ, "phases": phases,
               "retention": {"verifier": "passed", "compared_count_fields": len(keys) - len(unstable),
                             "unstable_reference_fields": unstable_values,
                             "count_mismatches": count_mismatches,
                             "regressed_listing_phases": regressions,
                             "retained": not count_mismatches and not regressions}}
    json.dump(summary, sys.stdout, indent=1, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
