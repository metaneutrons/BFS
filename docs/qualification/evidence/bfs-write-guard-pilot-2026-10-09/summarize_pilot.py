#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the fixed write protection check pilot."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys
from typing import Any


BUNDLE = Path(__file__).resolve().parent
FLOOR_SUMMARY_PATH = (
    BUNDLE.parent / "bfs-floor-resident-pilot-2026-10-09" / "summarize_pilot.py"
)

PRODUCTION_CANDIDATE_HANDLER_SHA256 = (
    "e9b6ce33f1531b96f11a6fe0168b54acf444594d2bbb6104c8fd34007cfc87ec"
)
DIAGNOSTIC_CANDIDATE_HANDLER_SHA256 = (
    "54a17d69333fd7a134e447f8ddde3f8d8663b6dace63fd4d86f8bcc86ebae570"
)

_SHARED_HELPER_MODULES = (
    "bfs_floor_profile_helpers",
    "bfs_floor_sealed_helpers",
)
_MISSING = object()


def _load_floor_summary():
    """Load the maintained verifier without replacing its shared module names."""
    name = f"{__name__}_floor_helpers"
    spec = importlib.util.spec_from_file_location(name, FLOOR_SUMMARY_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load shared evidence helper: {FLOOR_SUMMARY_PATH}")

    previous = {key: sys.modules.get(key, _MISSING) for key in _SHARED_HELPER_MODULES}
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(spec.name, None)
        raise
    finally:
        for key, prior in previous.items():
            if prior is _MISSING:
                sys.modules.pop(key, None)
            else:
                sys.modules[key] = prior
    return module


_FLOOR = _load_floor_summary()
ROOT = _FLOOR.ROOT

# These pins belong to this pilot. The guest, base-handler and PFS3 pins remain
# those maintained by the shared verifier.
PRODUCTION_BASE_HANDLER_SHA256 = _FLOOR.PRODUCTION_BASE_HANDLER_SHA256
DIAGNOSTIC_BASE_HANDLER_SHA256 = _FLOOR.DIAGNOSTIC_BASE_HANDLER_SHA256
PRODUCTION_GUEST_SHA256 = _FLOOR.PRODUCTION_GUEST_SHA256
DIAGNOSTIC_GUEST_SHA256 = _FLOOR.DIAGNOSTIC_GUEST_SHA256
PFS3_SHA256 = _FLOOR.PFS3_SHA256
ScheduleEntry = _FLOOR.ScheduleEntry
QualificationError = _FLOOR.QualificationError
SCHEDULE_HEADER = _FLOOR.SCHEDULE_HEADER

_FLOOR.BUNDLE = BUNDLE
_FLOOR.PRODUCTION_CANDIDATE_HANDLER_SHA256 = PRODUCTION_CANDIDATE_HANDLER_SHA256
_FLOOR.DIAGNOSTIC_CANDIDATE_HANDLER_SHA256 = DIAGNOSTIC_CANDIDATE_HANDLER_SHA256


def expected_schedule() -> list[ScheduleEntry]:
    """Return this pilot's fresh schedule, preserving the sealed run design."""
    entries = []
    for entry in _FLOOR.expected_schedule():
        entries.append(ScheduleEntry(
            entry.sequence,
            entry.family,
            entry.repeat,
            entry.order,
            entry.variant,
            "write-guard-" + entry.run_name.removeprefix("floor-"),
        ))
    if len(entries) != 14 or len({entry.run_name for entry in entries}) != 14:
        raise RuntimeError("internal error: the fixed write-guard pilot must contain 14 unique runs")
    return entries


EXPECTED_SCHEDULE = expected_schedule()
_FLOOR.EXPECTED_SCHEDULE = EXPECTED_SCHEDULE


def __getattr__(name: str) -> Any:
    """Expose the shared strict validators and protocol metadata."""
    return getattr(_FLOOR, name)


def summarize(results_root: Path) -> dict[str, Any]:
    # Keep the shared verifier bound to this adapter's schedule and bundle even
    # if a caller changes an adapter-level setting while testing the CLI.
    _FLOOR.BUNDLE = BUNDLE
    _FLOOR.PRODUCTION_CANDIDATE_HANDLER_SHA256 = PRODUCTION_CANDIDATE_HANDLER_SHA256
    _FLOOR.DIAGNOSTIC_CANDIDATE_HANDLER_SHA256 = DIAGNOSTIC_CANDIDATE_HANDLER_SHA256
    _FLOOR.EXPECTED_SCHEDULE = EXPECTED_SCHEDULE
    result = _FLOOR.summarize(Path(results_root))
    result["scope"] = (
        "Fixed 14-run write protection check pilot: ACTION_WRITE checks FIBF_WRITE in the "
        "core's locked write instead of a separate handler inode read. Every scheduled run and both BFS/PFS3 "
        "outputs per run (28 outputs total) are retained without filtering; all 23 phases "
        "and raw protocol fields are preserved. Ratios are paired only within the "
        "scheduled repeat/order. No pooling, outlier exclusion, exclusive-time calculation, "
        "or performance-clearance judgment is made."
    )
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify and summarize the fixed 14-run write protection check pilot."
    )
    parser.add_argument(
        "results_root", nargs="?", type=Path, default=BUNDLE / "results",
        help="the evidence results directory (default: this bundle's results/)",
    )
    args = parser.parse_args(argv)
    try:
        result = summarize(args.results_root)
    except (OSError, QualificationError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
