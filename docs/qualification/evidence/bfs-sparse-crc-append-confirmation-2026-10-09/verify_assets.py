#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify post-run asset receipts without distributing licensed files."""

from pathlib import Path
import re

import summarize_confirmation as summary


def read_receipt(path):
    values = {}
    for line in path.read_text(encoding="ascii").splitlines():
        fields = line.split("  ", 1)
        if (len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0])
                or not fields[1] or fields[1] in values):
            raise ValueError(f"Malformed or duplicate receipt row: {path}")
        values[fields[1]] = fields[0]
    if not values:
        raise ValueError(f"Empty receipt: {path}")
    return values


def main():
    root = Path(__file__).resolve().parent
    fixed_assets = None
    for entry in summary.EXPECTED_SCHEDULE:
        run_dir = root / entry.run_name
        before = read_receipt(run_dir / "runtime-inputs.sha256")
        after = read_receipt(run_dir / "runtime-inputs.post.sha256")
        expected = summary.PARSER.expected_runtime_inputs(summary.parser_run_for(entry))
        if before != expected or after != expected:
            raise ValueError(f"Runtime input changed or mismatched: {entry.run_name}")
        assets = read_receipt(run_dir / "installed-assets.sha256")
        for runtime_path, digest in expected.items():
            if assets.get(runtime_path.removeprefix("system/")) != digest:
                raise ValueError(f"Asset receipt contradicts runtime input: {entry.run_name}")
        assets.pop("L/bfshandler")
        if fixed_assets is None:
            fixed_assets = assets
        if assets != fixed_assets:
            raise ValueError(f"Non-handler assets differ: {entry.run_name}")
    print("PASS: all 40 before/after runtime receipts match pinned identities")
    print(f"PASS: all {len(fixed_assets)} non-handler asset receipts match across all 40 runs")
    print("Receipts describe actual Cachy copies; licensed files/binaries are not in this bundle")


if __name__ == "__main__":
    main()
