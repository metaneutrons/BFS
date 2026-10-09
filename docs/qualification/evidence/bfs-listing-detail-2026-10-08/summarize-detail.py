#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify both exact runs; report sampled inclusive intervals, never totals."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[3]
LABEL = "metadata-deep-v14-20261008-m3"
EXPECTED = {f"{LABEL}-1-bfs-first", f"{LABEL}-2-pfs3-first"}


def load(path):
    pairs = [line.split("\t", 1) for line in path.read_text().splitlines()]
    rows = dict(pairs)
    if len(rows) != len(pairs) or rows.get("PASS") != "1":
        raise ValueError(f"duplicate rows or missing PASS: {path}")
    if rows.get("FS_DEEP_COMPARE") != "14" or rows.get("DRIVE") != "DH1:":
        raise ValueError(f"unexpected schema or drive: {path}")
    values = {key: int(value) for key, value in pairs if key != "DRIVE"}
    if values["CLOCK_HZ"] <= 0 or values["DETAIL_SAMPLE_STRIDE"] != 17:
        raise ValueError("invalid clock or sampling stride")
    return values


if __name__ == "__main__":
    actual = {path.name for path in ROOT.glob(f"{LABEL}-*") if path.is_dir()}
    if actual != EXPECTED:
        raise ValueError(f"unexpected run inventory: {actual ^ EXPECTED}")
    header = (REPO / "src/amiga/perf_probe.h").read_text()
    macro = header.split("#define BFS_PERF_DETAIL_SCOPES(X)", 1)[1].split("\n\n", 1)[0]
    scopes = re.findall(r"X\((DETAIL_[A-Z_]+),", macro)
    if len(scopes) != 12 or len(set(scopes)) != len(scopes):
        raise ValueError("unexpected diagnostic scope ABI")
    print("run\tphase\tphase_us\tscope\tcalls\tsamples\tsampled_us\tmean_sample_us")
    for name in sorted(EXPECTED):
        subprocess.run([str(REPO / "emulator-test/verify-bench-results.sh"),
                        str(ROOT / name), "deep-compare"], check=True,
                       capture_output=True, text=True)
        rows = load(ROOT / name / "system/Results/bfs.deep-compare.tsv")
        phases = [key[:-3] for key in rows if key.endswith("_US")]
        for phase in phases:
            for scope in scopes:
                calls = rows[f"{phase}_{scope}_CALLS"]
                samples = rows[f"{phase}_{scope}_SAMPLES"]
                ticks = rows[f"{phase}_{scope}_SAMPLE_TICKS"]
                if samples != calls // 17 or (samples == 0 and ticks != 0):
                    raise ValueError(f"invalid sample accounting: {name}/{phase}/{scope}")
                sampled_us = ticks * 1_000_000 / rows["CLOCK_HZ"]
                mean_us = f"{sampled_us / samples:.3f}" if samples else "NA"
                print("\t".join(map(str, [name, phase, rows[f"{phase}_US"],
                                          scope, calls, samples,
                                          f"{sampled_us:.3f}", mean_us])))
