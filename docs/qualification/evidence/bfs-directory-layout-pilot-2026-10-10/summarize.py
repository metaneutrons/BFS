#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 10 October directory layout pilot.

Runs the repository's strict verifier on both deep-compare runs of the branch
and on the two runs of the listing diagnostic on main, reports the listing
phases as that diagnostic does, and evaluates the acceptance rule fixed in
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
REFERENCE = ROOT / "docs/qualification/evidence/bfs-listing-diagnostic-2026-10-10/results"
RUNS = {
    "layout": [BUNDLE / "results/dirlayout-pilot-1-bfs-first",
               BUNDLE / "results/dirlayout-pilot-2-pfs3-first"],
    "main": [REFERENCE / "listing-diag-1-bfs-first", REFERENCE / "listing-diag-2-pfs3-first"],
}
# The bounds as written in PILOT_SCOPE.md (1,171 and 1,153 times 15 / 72).
ACCEPT_PHASE = "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL"
LEAF_VIEW_BOUND = 244
DEVICE_READ_BOUND = 240
CLOCK_HZ = 709379
PHASES = (
    "LIST_EXALL_1000_ENTRIES_FIRST_PASS", "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL",
    "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL",
    "LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL", "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL",
)
COUNTS = (
    "INODE_READ_CALLS", "BTREE_SEARCH_CALLS", "BTREE_INDEX_HINT_HITS", "BTREE_LEAF_HINT_HITS",
    "INODE_TREE_NODE_VIEWS", "INODE_TREE_RESIDENT_VIEWS", "DIR_TREE_NODE_VIEWS",
    "DIR_TREE_RESIDENT_VIEWS", "DIR_TREE_LEAF_NODE_VIEWS", "DIR_TREE_LEAF_RESIDENT_VIEWS",
    "BIO_READS", "NODE_CRC_READ_CALLS", "DETAIL_CACHE_PEEK_CALLS", "DETAIL_EXALL_FILL_CALLS",
)
SCOPES = (
    "EXALL_FILL", "INODE_READ", "INODE_SEARCH", "INODE_VALIDATE", "INODE_NODE_VIEW",
    "DIR_NODE_VIEW", "DIR_BINARY_SEARCH", "NODE_STRUCTURE", "CACHE_PEEK",
)


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


def phase_view(values, phase):
    def number(name):
        return int(values.get(f"{phase}_{name}", "0"))
    view = {"us": number("US"), "counts": {name: number(name) for name in COUNTS}, "scopes": {}}
    for scope in SCOPES:
        calls = number(f"DETAIL_{scope}_CALLS")
        samples = number(f"DETAIL_{scope}_SAMPLES")
        ticks = number(f"DETAIL_{scope}_SAMPLE_TICKS")
        mean = ticks / samples / CLOCK_HZ * 1e6 if samples else None
        view["scopes"][scope] = {"calls": calls, "samples": samples,
                                 "mean_us_per_sampled_call": mean}
    return view


def main():
    summary = {"clock_hz": CLOCK_HZ, "variants": {}}
    for variant, run_dirs in RUNS.items():
        runs = []
        for run_dir in run_dirs:
            verify(run_dir)
            results = run_dir / "system/Results"
            bfs, pfs3 = load(results / "bfs.deep-compare.tsv"), load(results / "pfs3.deep-compare.tsv")
            runs.append({"run": run_dir.name, "phases": {
                phase: {"bfs": phase_view(bfs, phase), "pfs3_us": int(pfs3.get(f"{phase}_US", "0"))}
                for phase in PHASES}})
        summary["variants"][variant] = runs
    checks = []
    for run in summary["variants"]["layout"]:
        counts = run["phases"][ACCEPT_PHASE]["bfs"]["counts"]
        leaf_views = counts["DIR_TREE_LEAF_NODE_VIEWS"]
        device_reads = counts["DIR_TREE_NODE_VIEWS"] - counts["DIR_TREE_RESIDENT_VIEWS"]
        checks.append({"run": run["run"], "leaf_views": leaf_views,
                       "leaf_view_bound": LEAF_VIEW_BOUND,
                       "directory_device_reads": device_reads,
                       "device_read_bound": DEVICE_READ_BOUND,
                       "met": leaf_views <= LEAF_VIEW_BOUND and device_reads <= DEVICE_READ_BOUND})
    summary["acceptance"] = {"phase": ACCEPT_PHASE, "runs": checks,
                             "accepted": len(checks) == 2 and all(c["met"] for c in checks)}
    json.dump(summary, sys.stdout, indent=1, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
