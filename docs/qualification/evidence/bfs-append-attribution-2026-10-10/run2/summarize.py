#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify and summarize the 10 October append 4 KiB attribution, run 2.

Part A: 24 compare starts of three production handlers; reports the BFS / PFS3
ratios and the paired ratios mid / ref, cand / mid and cand / ref per phase.
Part B: 6 split-write-compare starts of their write probes; reports, for
APPEND_4K_1M, every count field and the sampled microseconds per call of the
write-path scopes. Probe intervals are inclusive and nested; nothing here adds,
subtracts or extrapolates them. Applies the reading fixed in PILOT_SCOPE.md.
"""
import csv
import json
import math
from pathlib import Path
import re
import statistics
import subprocess  # nosec B404 - fixed repository verifier, no shell
import sys

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[4]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
RESULTS = BUNDLE / "results"
VARIANTS = ("ref", "mid", "cand")
PAIRS = (("mid", "ref"), ("cand", "mid"), ("cand", "ref"))
TARGET = "APPEND_4K_1M"
SOURCE_BOUND = 1.03
CLOCK_HZ = 709379
COUNT = re.compile(r"_(CALLS|VIEWS|READS|WRITES|UPDATES|MAPS|ALLOCS|COMMITS|HITS|MISSES)$")
SCOPES = ("WORK_PACKET_WRITE", "WORK_CORE_FILE_WRITE", "WORK_WRITE_DETAIL_INODE_READ",
          "WORK_WRITE_DETAIL_INODE_WRITE", "WORK_WRITE_DETAIL_EXTENT_MAP",
          "WORK_WRITE_DETAIL_FREESPACE_GOAL", "WORK_FREESPACE_ALLOC",
          "WORK_DETAIL_INODE_READ", "WORK_DETAIL_INODE_SEARCH",
          "WORK_DETAIL_CACHE_PEEK", "WORK_DETAIL_BUFFER_FREE")


def verify(run_dir, mode):
    """True if the strict verifier accepts the start."""
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    result = subprocess.run([str(VERIFIER), str(run_dir), mode],  # nosec B603
                            capture_output=True, text=True, check=False)
    return result.returncode == 0


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
    prod, probe = {}, {}
    valid = {"prod": True, "probe": True}
    expected = {"prod": 24, "probe": 6}
    for row in schedule:
        run_dir = RESULTS / row["run_name"]
        results = run_dir / "system/Results"
        key = (int(row["round"]), row["variant"])
        mode = "compare" if row["part"] == "prod" else "split-write-compare"
        if not verify(run_dir, mode):
            valid[row["part"]] = False
            continue
        if row["part"] == "prod":
            prod[key] = {"order": row["order"], "bfs": load(results / "bfs.tsv"),
                         "pfs3": load(results / "pfs3.tsv")}
        else:
            probe[key] = {"order": row["order"], "bfs": load(results / "bfs.split.tsv")}
    for part, count in expected.items():
        if len(prod if part == "prod" else probe) != count:
            valid[part] = False
    summary = {"valid_parts": valid}
    if not valid["prod"]:
        json.dump(summary, sys.stdout, indent=1, sort_keys=True)
        print()
        return
    rounds = sorted({r for r, _ in prod})
    phases = [k[:-3] for k in prod[(rounds[0], "ref")]["bfs"] if k.endswith("_US")]

    ratios = {v: {p: statistics.median(int(prod[(r, v)]["bfs"][f"{p}_US"]) /
                                       int(prod[(r, v)]["pfs3"][f"{p}_US"]) for r in rounds)
                  for p in phases} for v in VARIANTS}
    paired = {}
    for top, bottom in PAIRS:
        name = f"{top}_over_{bottom}"
        paired[name] = {}
        for p in phases:
            values = [int(prod[(r, top)]["bfs"][f"{p}_US"]) /
                      int(prod[(r, bottom)]["bfs"][f"{p}_US"]) for r in rounds]
            paired[name][p] = {"ratios": values, "median": statistics.median(values),
                               "min": min(values), "max": max(values)}
        paired[name]["_geomean_of_medians"] = geomean([paired[name][p]["median"]
                                                       for p in phases])
    sources = [name for name in ("mid_over_ref", "cand_over_mid")
               if paired[name][TARGET]["median"] >= SOURCE_BOUND]
    reading = {"target": TARGET, "source_bound": SOURCE_BOUND,
               "medians": {name: paired[name][TARGET]["median"] for name in paired},
               "sources": sources,
               "reproduced": paired["cand_over_ref"][TARGET]["median"] >= SOURCE_BOUND}

    probe_rounds = sorted({r for r, _ in probe}) if valid["probe"] else []
    part_b = {}
    for r in probe_rounds:
        part_b[r] = {"order": probe[(r, "ref")]["order"]}
        for v in VARIANTS:
            values = probe[(r, v)]["bfs"]
            prefix = f"{TARGET}_"
            counts = {k[len(prefix):]: int(x) for k, x in values.items()
                      if k.startswith(prefix) and COUNT.search(k)}
            scopes = {}
            for scope in SCOPES:
                samples = int(values.get(f"{prefix}{scope}_SAMPLES", "0"))
                ticks = int(values.get(f"{prefix}{scope}_SAMPLE_TICKS", "0"))
                scopes[scope] = {"calls": int(values.get(f"{prefix}{scope}_CALLS", "0")),
                                 "samples": samples,
                                 "us_per_sample": ticks / samples / CLOCK_HZ * 1e6
                                 if samples else None}
            part_b[r][v] = {"work_us": int(values[f"{prefix}WORK_US"]), "counts": counts,
                            "scopes": scopes}
        part_b[r]["count_differences"] = sorted(
            k for k in part_b[r]["ref"]["counts"]
            if len({part_b[r][v]["counts"].get(k) for v in VARIANTS}) > 1)
    summary.update({"phases": phases, "bfs_over_pfs3": ratios, "paired": paired,
                    "reading": reading, "probe": part_b if valid["probe"] else None})
    json.dump(summary, sys.stdout, indent=1, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
