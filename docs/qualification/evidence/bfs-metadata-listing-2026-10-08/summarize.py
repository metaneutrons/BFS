#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Recompute all-run medians and adjacent-run comparisons, without filtering.

Strict guest/data verification is separately performed by the repository's
verify-bench-results.sh. Adjacent same-order runs are matched, not simultaneous;
their ratios do not remove scheduling noise on the uncontrolled KVM host.
"""

from pathlib import Path
import statistics
import sys


def load(path):
    pairs = [line.split("\t", 1) for line in path.read_text(encoding="ascii").splitlines()]
    rows = dict(pairs)
    if len(rows) != len(pairs):
        raise ValueError(f"duplicate rows: {path}")
    if rows.get("PASS") != "1":
        raise ValueError(f"missing PASS: {path}")
    values = {key: int(value) for key, value in rows.items() if key.endswith("_US")}
    if not values or any(value <= 0 for value in values.values()):
        raise ValueError(f"missing/nonpositive phases: {path}")
    return values


def summarize(root, label, suffix, variants=("base", "m1")):
    runs = {variant: [] for variant in variants}
    for variant in runs:
        expected = set()
        for index in range(1, 9):
            order = "bfs-first" if index % 2 else "pfs3-first"
            directory = root / f"{label}-{variant}-{index}-{order}"
            expected.add(directory.name)
            results = directory / "system/Results"
            runs[variant].append((load(results / f"bfs.{suffix}"),
                                  load(results / f"pfs3.{suffix}")))
        actual = {path.name for path in root.glob(f"{label}-{variant}-*") if path.is_dir()}
        if actual != expected:
            raise ValueError(f"unexpected run inventory: {variant}: {actual ^ expected}")
    phases = runs["base"][0][0].keys()
    if any(set(bfs) != set(phases) or set(pfs) != set(phases)
           for rows in runs.values() for bfs, pfs in rows):
        raise ValueError("inconsistent phase inventory")
    print(f"## {label}: all {len(variants) * 8} expected runs present and PASS")
    print("variant\tphase\tbase_median_us\tcandidate_median_us\t"
          "base_pfs3_median_ratio\tcandidate_pfs3_median_ratio\t"
          "matched_candidate_base_median\tmatched_candidate_base_range\t"
          "base_over_5\tcandidate_over_5")
    for variant in variants[1:]:
        for phase in phases:
            base = [bfs[phase] for bfs, _ in runs["base"]]
            candidate = [bfs[phase] for bfs, _ in runs[variant]]
            base_ratios = [bfs[phase] / pfs[phase] for bfs, pfs in runs["base"]]
            ratios = [bfs[phase] / pfs[phase] for bfs, pfs in runs[variant]]
            matched = [new / old for new, old in zip(candidate, base)]
            print(f"{variant}\t{phase}\t{statistics.median(base):.1f}\t"
                  f"{statistics.median(candidate):.1f}\t"
                  f"{statistics.median(base_ratios):.3f}\t"
                  f"{statistics.median(ratios):.3f}\t"
                  f"{statistics.median(matched):.3f}\t"
                  f"{min(matched):.3f}-{max(matched):.3f}\t"
                  f"{sum(ratio > 5 for ratio in base_ratios)}/8\t"
                  f"{sum(ratio > 5 for ratio in ratios)}/8")


if __name__ == "__main__":
    evidence_root = Path(__file__).resolve().parent
    selected = sys.argv[1:] or ["compare", "durable-compare"]
    for mode in selected:
        if mode == "compare":
            summarize(evidence_root, "metadata-m1-c8-20261008", "tsv")
        elif mode == "durable-compare":
            summarize(evidence_root, "metadata-m1-d8-20261008", "durable.tsv")
        elif mode == "listing-compare":
            summarize(evidence_root, "metadata-v4-c8-20261008", "tsv",
                      ("base", "m1b", "m2"))
        elif mode == "exnext-compare":
            summarize(evidence_root, "metadata-m3-c8-20261008", "tsv",
                      ("base", "m3"))
        else:
            raise ValueError(f"unsupported mode: {mode}")
