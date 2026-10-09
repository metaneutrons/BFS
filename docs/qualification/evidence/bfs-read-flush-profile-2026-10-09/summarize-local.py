#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Validate and summarize two local protocol checks, not a speed comparison."""
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
BUNDLE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("split_verifier", ROOT / "tools/verify-split-bench.py")
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

RUNS = (("normal-m5", "split-compare"), ("durable-sparse", "split-durable-compare"))
summary = {"scope": "Two local diagnostic protocol checks; not paired production evidence", "runs": {}}
for name, mode in RUNS:
    results = BUNDLE / name / "system/Results"
    verifier.verify(results, mode)
    filesystems = {}
    for filesystem in ("bfs", "pfs3"):
        values = verifier._read_tsv(results / f"{filesystem}.split.tsv", verifier.build_schema(filesystem, mode))
        hz = values["CLOCK_HZ"]
        phases = {}
        phase_names = sorted(verifier._inventories()["phases"], key=len, reverse=True)
        for phase in verifier._inventories()["phases"]:
            # Legacy LIST_EXNEXT_400 is also a prefix of scaled fixture names.
            rows = {key[len(phase) + 1:]: value for key, value in values.items()
                    if next((p for p in phase_names if key.startswith(p + "_")), None) == phase}
            # Do not add nested intervals or estimate exclusive CPU time.
            tick_ms = {key: value * 1000 / hz for key, value in rows.items() if key.endswith("_TICKS")} if hz else {}
            phases[phase] = {"raw": rows, "inclusive_tick_ms": tick_ms,
                             "verify_fraction_of_work": rows["GUEST_VERIFY_US"] / rows["WORK_US"]}
        filesystems[filesystem] = {"clock_hz": hz, "phases": phases}
    summary["runs"][name] = {"mode": mode, "filesystems": filesystems}
print(json.dumps(summary, indent=2, sort_keys=True))
