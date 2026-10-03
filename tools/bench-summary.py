#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Summarize verified compare runs from emulator-test/bench-series.sh.

Usage: bench-summary.py [--suffix tsv] SERIES-NAME [SERIES-NAME...]
Each argument selects build/benchmark/SERIES-NAME-* run directories. Only
runs whose BFS and PFS3 outputs both end in PASS count. Per phase the table
gives the mean BFS and PFS3 times in microseconds, their coefficients of
variation, the ratio of means, the per-run ratio range and median, and how
many runs exceed five times PFS3.
"""

import argparse
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parent.parent
LIMIT = 5.0


def load(path):
    values = {}
    for line in path.read_text(encoding="ascii").splitlines():
        name, _, value = line.partition("\t")
        values[name] = value
    return values


def runs(prefix, suffix):
    found = []
    for run_dir in sorted((ROOT / "build/benchmark").glob(f"{prefix}-*")):
        results = run_dir / "system/Results"
        bfs, pfs3 = results / f"bfs.{suffix}", results / f"pfs3.{suffix}"
        if not (bfs.is_file() and pfs3.is_file()):
            continue
        bfs_values, pfs3_values = load(bfs), load(pfs3)
        if bfs_values.get("PASS") == "1" and pfs3_values.get("PASS") == "1":
            found.append((bfs_values, pfs3_values))
    return found


def variation(values):
    return statistics.stdev(values) / statistics.mean(values) * 100 if len(values) > 1 else 0.0


def summarize(prefix, suffix):
    rows = runs(prefix, suffix)
    print(f"== {prefix}: {len(rows)} valid runs")
    if not rows:
        return
    print(f"{'phase':20} {'BFS mean':>10} {'cv%':>5} {'PFS3 mean':>10} {'cv%':>5} "
          f"{'ratio':>6} {'range':>12} {'median':>6} {'>5x':>4}")
    phases = [name for name in rows[0][0] if name.endswith("_US")]
    for phase in phases:
        bfs = [int(b[phase]) for b, _ in rows]
        pfs3 = [int(p[phase]) for _, p in rows]
        ratios = [b / p for b, p in zip(bfs, pfs3)]
        over = sum(ratio > LIMIT for ratio in ratios)
        print(f"{phase[:-3]:20} {statistics.mean(bfs):10.0f} {variation(bfs):5.1f} "
              f"{statistics.mean(pfs3):10.0f} {variation(pfs3):5.1f} "
              f"{statistics.mean(bfs) / statistics.mean(pfs3):6.2f} "
              f"{min(ratios):5.2f}-{max(ratios):<6.2f} {statistics.median(ratios):6.2f} "
              f"{over:>2}/{len(rows)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--suffix", default="tsv", help="output suffix, e.g. durable.tsv")
    parser.add_argument("series", nargs="+")
    args = parser.parse_args()
    for prefix in args.series:
        summarize(prefix, args.suffix)


if __name__ == "__main__":
    main()
