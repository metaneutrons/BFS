#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 10 October listing diagnostic.

Runs the repository's strict verifier on both deep-compare runs, then reports
for the listing phases: BFS and PFS3 elapsed time, exact call counts, and the
mean sampled interval per sampled call of each detail scope. The M5 deep runs
of 9 October are reported beside them as a reference. Sampled intervals are
inclusive and nested; they are never added, subtracted or scaled here.
"""
import json
from pathlib import Path
import subprocess  # nosec B404 - fixed repository verifier, no shell
import sys

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[3]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
REFERENCE = ROOT / "docs/qualification/evidence/bfs-scan-admission-performance-2026-10-09"
RUNS = {
    "main": [BUNDLE / "results/listing-diag-1-bfs-first", BUNDLE / "results/listing-diag-2-pfs3-first"],
    "m5": [REFERENCE / "admission-deep-20261009-m5-1-bfs-first",
           REFERENCE / "admission-deep-20261009-m5-2-pfs3-first"],
}
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
    json.dump(summary, sys.stdout, indent=1, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
