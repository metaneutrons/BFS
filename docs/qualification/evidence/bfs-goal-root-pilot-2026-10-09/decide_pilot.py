#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Apply the unchanged, predeclared local-retention rule to this pilot."""
import importlib.util
import json
from pathlib import Path

BUNDLE = Path(__file__).resolve().parent
POLICY = BUNDLE.parent / "bfs-floor-resident-pilot-2026-10-09/decide_pilot.py"
spec = importlib.util.spec_from_file_location("bfs_goal_root_retention_policy", POLICY)
if spec is None or spec.loader is None:
    raise RuntimeError(f"could not load retention policy: {POLICY}")
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)
decide = policy.decide

if __name__ == "__main__":
    summary = json.loads((BUNDLE / "summary.json").read_text())
    print(json.dumps(decide(summary), indent=2, sort_keys=True))
