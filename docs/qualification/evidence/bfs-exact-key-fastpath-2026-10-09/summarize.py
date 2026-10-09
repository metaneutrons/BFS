#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the 2026-10-09 exact-key fast-path runs."""

from __future__ import annotations

import argparse
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import statistics
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
BASE_SUMMARY_PATH = (
    SCRIPT_DIR.parent
    / "bfs-leaf-range-performance-2026-10-08"
    / "summarize.py"
)
BASE_SPEC = spec_from_file_location("bfs_leaf_range_base_summary", BASE_SUMMARY_PATH)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError(f"could not load prior summary module: {BASE_SUMMARY_PATH}")
BASE = module_from_spec(BASE_SPEC)
sys.modules[BASE_SPEC.name] = BASE
BASE_SPEC.loader.exec_module(BASE)
# Reuse the immutable prior parser, RDB checks, verifier, and statistic helpers.
BASE.SCRIPT_DIR = SCRIPT_DIR

Run = BASE.Run
VerifiedRun = BASE.VerifiedRun
QualificationError = BASE.QualificationError

M3_HANDLER_SHA256 = "adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27"
RANGE_HANDLER_SHA256 = "79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4"
FAST_HANDLER_SHA256 = "27447f991379ccc0eda02254bf5073dc0a11426f0321781cc1eea922f3f04f40"
FAST_PROBE_SHA256 = "557c6248e11c1d1985c8335c2d65eb92aa3c32607c021c0d5433d82faeb6719c"
NORMAL_GUEST_SHA256 = "798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52"
DEEP_GUEST_SHA256 = "7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661"
PFS3_HANDLER_SHA256 = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"

HANDLER_SHA256_BY_VARIANT = {
    "m3": M3_HANDLER_SHA256,
    "range": RANGE_HANDLER_SHA256,
    "fast": FAST_HANDLER_SHA256,
}


def _make_run(name: str, mode: str, family: str, variant: str,
              repeat: int, order: str) -> Run:
    return BASE.make_run(name, mode, 30, family, variant, repeat, order)


def expected_full_runs() -> list[Run]:
    runs: list[Run] = []
    for family, mode in (("normal", "compare"), ("durable", "durable-compare")):
        for variant in ("m3", "range", "fast"):
            for repeat in range(1, 9):
                order = "bfs-first" if repeat % 2 else "pfs3-first"
                name = f"exact-key-{family}-20261009-{variant}-{repeat}-{order}"
                runs.append(_make_run(name, mode, family, variant, repeat, order))

    for repeat in range(1, 3):
        order = "bfs-first" if repeat % 2 else "pfs3-first"
        name = f"exact-key-deep-20261009-fast-{repeat}-{order}"
        runs.append(_make_run(name, "deep-compare", "deep", "fast", repeat, order))
    return runs


def expected_pilot_runs() -> list[Run]:
    runs: list[Run] = []
    for family, mode in (
        ("pilot-normal", "compare"),
        ("pilot-durable", "durable-compare"),
    ):
        for variant in ("range", "fast"):
            for repeat in range(1, 3):
                order = "bfs-first" if repeat % 2 else "pfs3-first"
                name = f"exact-key-{family}-20261009-{variant}-{repeat}-{order}"
                runs.append(_make_run(name, mode, family, variant, repeat, order))
    return runs


ALL_RUNS = expected_full_runs()
PILOT_RUNS = expected_pilot_runs()
NORMAL_RUNS = [run for run in ALL_RUNS if run.family == "normal"]
if (len(ALL_RUNS), len(PILOT_RUNS), len(NORMAL_RUNS)) != (50, 8, 24):
    raise RuntimeError("internal error: exact-key run inventory count changed")


def inventory_errors(expected: list[Run]) -> list[str]:
    expected_names = {run.name for run in expected}
    missing = sorted(
        name for name in expected_names if not (SCRIPT_DIR / name).is_dir()
    )
    relevant_families = {run.family for run in expected}
    relevant_prefixes = tuple(
        f"exact-key-{family}-" for family in sorted(relevant_families)
    )
    unexpected = sorted(
        path.name
        for path in SCRIPT_DIR.iterdir()
        if path.is_dir()
        and path.name.startswith(relevant_prefixes)
        and path.name not in expected_names
    )
    errors = []
    if missing:
        errors.append("missing expected run directories:\n  " + "\n  ".join(missing))
    if unexpected:
        errors.append("unexpected run directories:\n  " + "\n  ".join(unexpected))
    if len(expected_names) != len(expected):
        errors.append("internal error: expected inventory contains duplicate names")
    return errors


def check_runtime_identities(run: Run, run_dir: Path) -> None:
    """Check the recorded installed files for the selected exact-key run."""
    path = run_dir / "runtime-inputs.sha256"
    if not path.is_file():
        raise QualificationError(f"{run.name}: missing installed-input digests")
    observed: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(
            f"{run.name}: could not read installed-input digests: {error}"
        ) from error
    for line in lines:
        fields = line.split("  ", 1)
        if (len(fields) != 2 or len(fields[0]) != 64
                or any(char not in "0123456789abcdef" for char in fields[0])
                or not fields[1] or fields[1] in observed):
            raise QualificationError(
                f"{run.name}: malformed or duplicate installed-input digest"
            )
        observed[fields[1]] = fields[0]

    if run.family == "deep":
        handler = FAST_PROBE_SHA256
        guest = DEEP_GUEST_SHA256
    else:
        try:
            handler = HANDLER_SHA256_BY_VARIANT[run.variant]
        except KeyError as error:
            raise QualificationError(
                f"{run.name}: unsupported handler variant {run.variant!r}"
            ) from error
        guest = NORMAL_GUEST_SHA256
    expected = {
        "system/L/bfshandler": handler,
        "system/L/pfs3aio": PFS3_HANDLER_SHA256,
        "system/C/fs-compare-bench": guest,
    }
    if observed != expected:
        raise QualificationError(f"{run.name}: installed input identity mismatch")


# BASE.verify_run resolves this name at call time. Keep its parser, RDB, TSV,
# verifier, and phase validation functions unchanged.
BASE.check_runtime_identities = check_runtime_identities


def verify_inventory(runs: list[Run]) -> list[VerifiedRun]:
    errors = inventory_errors(runs)
    if errors:
        raise QualificationError("\n".join(errors))
    if not BASE.VERIFIER.is_file():
        raise QualificationError(f"repository verifier not found: {BASE.VERIFIER}")

    verified: list[VerifiedRun] = []
    failures = []
    for run in runs:
        try:
            result = BASE.verify_run(run)
        except QualificationError as error:
            failures.append(str(error))
            continue
        verified.append(result)
        schema = BASE.expected_schema(run.mode)[2]
        print(
            f"VERIFIED\t{run.name}\tmode={run.mode}\tschema={schema}\t"
            f"de_NumBuffers={run.buffers}"
        )
    if failures:
        raise QualificationError(
            "verification failures (no runs are omitted):\n  "
            + "\n  ".join(failures)
        )
    return verified


def _phase_inventory(rows: list[VerifiedRun], title: str) -> list[str]:
    if not rows:
        raise QualificationError(f"no verified runs for summary {title}")
    phases = list(BASE.phase_metrics(rows[0].bfs))
    if not phases:
        raise QualificationError(f"no phase timing metrics for {title}")
    expected = set(phases)
    for row in rows[1:]:
        if set(BASE.phase_metrics(row.bfs)) != expected:
            raise QualificationError(f"BFS phase inventory differs within {title}")
    return phases


def summarize_pairs(family: str, mode: str, baseline_variant: str,
                    candidate_variant: str, rows: list[VerifiedRun]) -> None:
    baseline_rows = BASE.group_rows(rows, family, baseline_variant)
    candidate_rows = BASE.group_rows(rows, family, candidate_variant)
    baseline_by_repeat = {row.run.repeat: row for row in baseline_rows}
    candidate_by_repeat = {row.run.repeat: row for row in candidate_rows}
    if not baseline_by_repeat or baseline_by_repeat.keys() != candidate_by_repeat.keys():
        raise QualificationError(
            f"unpaired repeat inventory for {family} {candidate_variant}/{baseline_variant}"
        )
    phases = _phase_inventory(
        baseline_rows + candidate_rows,
        f"{family} paired {candidate_variant}/{baseline_variant}",
    )
    print(
        f"\n== Paired BFS timing, family={family}, mode={mode}; "
        f"baseline={baseline_variant}, candidate={candidate_variant}"
    )
    print("Every compare phase, including controls, is shown.")
    print(
        "phase\tcandidate_over_baseline_by_repeat_and_order\t"
        "ratio_min\tratio_max\tratio_median\tphase_mean_reduction_pct"
    )
    for phase in phases:
        ratios: list[float] = []
        ratios_display: list[str] = []
        baseline_values: list[int] = []
        candidate_values: list[int] = []
        for repeat in sorted(baseline_by_repeat):
            baseline = baseline_by_repeat[repeat]
            candidate = candidate_by_repeat[repeat]
            if baseline.run.order != candidate.run.order:
                raise QualificationError(
                    f"paired order differs for {family} repeat {repeat}"
                )
            baseline_time = BASE.phase_metrics(baseline.bfs)[phase]
            candidate_time = BASE.phase_metrics(candidate.bfs)[phase]
            if baseline_time == 0:
                raise QualificationError(
                    f"zero baseline timing for {family} repeat {repeat}, phase {phase}"
                )
            ratio = candidate_time / baseline_time
            ratios.append(ratio)
            ratios_display.append(
                f"{repeat}/{baseline.run.order}={ratio:.4f}"
            )
            baseline_values.append(baseline_time)
            candidate_values.append(candidate_time)
        baseline_mean = statistics.mean(baseline_values)
        reduction = (
            (baseline_mean - statistics.mean(candidate_values))
            / baseline_mean * 100.0
        )
        print(
            f"{phase[:-3]}\t{';'.join(ratios_display)}\t"
            f"{min(ratios):.4f}\t{max(ratios):.4f}\t"
            f"{statistics.median(ratios):.4f}\t{reduction:.2f}"
        )


def print_pilot_summary(rows: list[VerifiedRun]) -> None:
    for family, mode in (
        ("pilot-normal", "compare"),
        ("pilot-durable", "durable-compare"),
    ):
        for variant in ("range", "fast"):
            BASE.summarize_series(
                f"Pilot {family}, variant={variant}",
                BASE.group_rows(rows, family, variant),
            )
        summarize_pairs(family, mode, "range", "fast", rows)


def print_production_family(rows: list[VerifiedRun], family: str, mode: str) -> None:
    for variant in ("m3", "range", "fast"):
        BASE.summarize_series(
            f"Production {family}, variant={variant}",
            BASE.group_rows(rows, family, variant),
        )
    summarize_pairs(family, mode, "range", "fast", rows)
    summarize_pairs(family, mode, "m3", "fast", rows)


def print_full_summary(rows: list[VerifiedRun]) -> None:
    for family, mode in (("normal", "compare"), ("durable", "durable-compare")):
        print_production_family(rows, family, mode)

    deep = BASE.group_rows(rows, "deep", "fast")
    BASE.summarize_series("Production deep-compare, variant=fast", deep)
    BASE.summarize_deep_listing(deep)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument(
        "--pilot",
        action="store_true",
        help="verify only the eight pilot runs; diagnostic runs are excluded",
    )
    scope.add_argument(
        "--normal-only",
        action="store_true",
        help="verify the 24 completed normal runs; NOT full qualification",
    )
    args = parser.parse_args(argv)
    if args.pilot:
        selected, inventory_label = PILOT_RUNS, "pilot"
    elif args.normal_only:
        selected, inventory_label = NORMAL_RUNS, "normal only, NOT full qualification"
    else:
        selected, inventory_label = ALL_RUNS, "full qualification"
    print(
        f"Inventory mode: {inventory_label}; required={len(selected)}. "
        "No PASS-based run filtering is applied."
    )
    try:
        rows = verify_inventory(selected)
        if args.pilot:
            print_pilot_summary(rows)
            print("\nPILOT-ONLY RESULT: all 8 required pilots verified; NOT QUALIFICATION.")
        elif args.normal_only:
            print_production_family(rows, "normal", "compare")
            print("\nNORMAL-ONLY RESULT: all 24 required normal runs verified; NOT FULL QUALIFICATION.")
        else:
            print_full_summary(rows)
            print("\nQUALIFICATION INVENTORY: all 50 required runs verified.")
    except (QualificationError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
