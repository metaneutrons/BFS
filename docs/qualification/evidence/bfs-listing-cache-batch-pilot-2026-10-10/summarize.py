#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 10 October listing cache and inode batch pilot.

Runs the repository's strict verifier on both deep-compare runs of the
candidate and on the two runs of the directory validation pilot (the
reference), reports every phase, and evaluates the retention rule fixed in
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
REFERENCE = ROOT / "docs/qualification/evidence/bfs-directory-validation-pilot-2026-10-10/results"
RUNS = {
    "candidate": [BUNDLE / "results/cachebatch-pilot-1-bfs-first",
                  BUNDLE / "results/cachebatch-pilot-2-pfs3-first"],
    "reference": [REFERENCE / "dirvalid-pilot-1-bfs-first",
                  REFERENCE / "dirvalid-pilot-2-pfs3-first"],
}
CLOCK_HZ = 709379
TARGET = "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL"
DEVICE_READ_BOUND = 75
TIME_BOUND = 0.85
REGRESSION = 1.10
SCOPES = ("INODE_READ", "INODE_SEARCH", "DIR_NODE_VIEW", "EXALL_FILL")


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


def phases(values):
    return sorted(key[:-3] for key in values if key.endswith("_US") and
                  not key.endswith("_SAMPLE_US"))


def device_reads(values, phase):
    def number(name):
        return int(values.get(f"{phase}_{name}", "0"))
    return {"directory": number("DIR_TREE_NODE_VIEWS") - number("DIR_TREE_RESIDENT_VIEWS"),
            "inode": number("INODE_TREE_NODE_VIEWS") - number("INODE_TREE_RESIDENT_VIEWS")}


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
    names = phases(data["reference"][0][1])
    report = {}
    regressions = []
    for phase in names:
        rows = []
        slower_in_both = True
        for (cname, cbfs, cpfs3), (rname, rbfs, rpfs3) in zip(data["candidate"], data["reference"]):
            c_us, r_us = int(cbfs[f"{phase}_US"]), int(rbfs[f"{phase}_US"])
            ratio = c_us / r_us if r_us else None
            if ratio is None or ratio < REGRESSION:
                slower_in_both = False
            row = {"candidate_run": cname, "reference_run": rname, "candidate_us": c_us,
                   "reference_us": r_us, "candidate_over_reference": ratio,
                   "candidate_pfs3_us": int(cpfs3.get(f"{phase}_US", "0")),
                   "reference_pfs3_us": int(rpfs3.get(f"{phase}_US", "0"))}
            if phase.startswith("LIST_"):
                row["candidate_device_reads"] = device_reads(cbfs, phase)
                row["reference_device_reads"] = device_reads(rbfs, phase)
                row["candidate_means_us"] = {s: mean_us(cbfs, phase, s) for s in SCOPES}
                row["reference_means_us"] = {s: mean_us(rbfs, phase, s) for s in SCOPES}
            rows.append(row)
        if slower_in_both:
            regressions.append(phase)
        report[phase] = rows
    target = []
    for row in report[TARGET]:
        reads = sum(row["candidate_device_reads"].values())
        target.append({"run": row["candidate_run"], "device_reads": reads,
                       "device_read_bound": DEVICE_READ_BOUND,
                       "time_ratio": row["candidate_over_reference"],
                       "time_bound": TIME_BOUND,
                       "met": reads <= DEVICE_READ_BOUND and
                              row["candidate_over_reference"] <= TIME_BOUND})
    summary = {"clock_hz": CLOCK_HZ, "phases": report,
               "retention": {"verifier": "passed", "target": target,
                             "regressed_phases": regressions,
                             "retained": all(t["met"] for t in target) and not regressions}}
    json.dump(summary, sys.stdout, indent=1, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
