#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the 2026-10-09 cursor-path run inventory."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import importlib.util
import json
import re
from pathlib import Path
import statistics
import subprocess  # nosec B404 - fixed repository verifier, without a shell
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[3]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
PREVIOUS_SUMMARY = (
    ROOT / "docs/qualification/evidence/bfs-leaf-range-performance-2026-10-08/summarize.py"
)
OVER5_LIMIT = 5.0
EXPECTED_BUFFER_COUNT = 30

RANGE_PRODUCTION = "79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4"
RANGE_PROBE = "a46a153a387de6cac773aee8bc6b757ce0bccf7a6dbc03b2c6ec8f8f8cc7981c"
PATH_PRODUCTION = "60c3203d11221aff46fc6e536578a4c644ab37c0106aa28cd44e01db06e0984e"
PATH_PROBE = "f09d043afda2e4fcb33e827984ae355224245c28ad18e819b26dc4d4583aaadc"
GUEST_PRODUCTION = "798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52"
# Exact deep benchmark input identified for the ABI 15 diagnostic guest.
GUEST_DEEP_ABI15 = "7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661"
PFS3_HANDLER = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"


def import_previous_parser():
    spec = importlib.util.spec_from_file_location("leaf_range_summary", PREVIOUS_SUMMARY)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load shared parser: {PREVIOUS_SUMMARY}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


PARSER = import_previous_parser()
QualificationError = PARSER.QualificationError


@dataclass(frozen=True)
class Run:
    name: str
    mode: str
    buffers: int
    family: str
    variant: str
    repeat: int
    order: str


@dataclass(frozen=True)
class VerifiedRun:
    run: Run
    bfs: dict[str, str]
    pfs3: dict[str, str]


def make_run(name: str, mode: str, family: str, variant: str,
             repeat: int, order: str) -> Run:
    return Run(name, mode, EXPECTED_BUFFER_COUNT, family, variant, repeat, order)


def expected_runs() -> list[Run]:
    runs: list[Run] = []
    orders = ((1, "bfs-first"), (2, "pfs3-first"))

    for variant in ("range", "path"):
        for repeat, order in orders:
            runs.append(make_run(
                f"cursor-pilot-normal-20261009-{variant}-{repeat}-{order}",
                "compare", "pilot", variant, repeat, order,
            ))

    for variant in ("range", "path"):
        for repeat, order in orders:
            runs.append(make_run(
                f"cursor-deep-20261009-{variant}-{repeat}-{order}",
                "deep-compare", "deep", variant, repeat, order,
            ))

    for family, mode in (("normal", "compare"), ("durable", "durable-compare")):
        for variant in ("range", "path"):
            for repeat in range(1, 9):
                order = "bfs-first" if repeat % 2 else "pfs3-first"
                runs.append(make_run(
                    f"cursor-{family}-20261009-{variant}-{repeat}-{order}",
                    mode, family, variant, repeat, order,
                ))
    return runs


ALL_RUNS = expected_runs()
PILOT_RUNS = [run for run in ALL_RUNS if run.family == "pilot"]
DEEP_RUNS = [run for run in ALL_RUNS if run.family == "deep"]
PRODUCTION_RUNS = [run for run in ALL_RUNS if run.family in {"normal", "durable"}]
PREFIX_BY_FAMILY = {
    "pilot": "cursor-pilot-normal-20261009-",
    "deep": "cursor-deep-20261009-",
    "normal": "cursor-normal-20261009-",
    "durable": "cursor-durable-20261009-",
}
if (len(ALL_RUNS), len(PILOT_RUNS), len(DEEP_RUNS), len(PRODUCTION_RUNS)) != (40, 4, 4, 32):
    raise RuntimeError("internal error: cursor-path run inventory count changed")
if any(
    PARSER.expected_schema(mode)[2] != schema
    for mode, schema in (("compare", 4), ("durable-compare", 4), ("deep-compare", 14))
):
    raise RuntimeError("shared parser schema assumptions changed")


def inventory_errors(root: Path, expected: list[Run]) -> list[str]:
    expected_names = {run.name for run in expected}
    missing = sorted(name for name in expected_names if not (root / name).is_dir())
    relevant_families = {run.family for run in expected}
    relevant_prefixes = tuple(
        PREFIX_BY_FAMILY[family] for family in sorted(relevant_families)
    )
    unexpected = sorted(
        path.name
        for path in root.iterdir()
        if path.is_dir()
        and path.name.startswith(relevant_prefixes)
        and path.name not in expected_names
    ) if root.is_dir() else []
    errors = []
    if missing:
        errors.append("missing expected run directories:\n  " + "\n  ".join(missing))
    if unexpected:
        errors.append("unexpected run directories:\n  " + "\n  ".join(unexpected))
    if len(expected_names) != len(expected):
        errors.append("internal error: expected inventory contains duplicate names")
    return errors


def expected_runtime_inputs(run: Run) -> dict[str, str]:
    if run.variant not in {"range", "path"}:
        raise QualificationError(f"unsupported cursor-path variant: {run.variant}")
    is_probe = run.family == "deep"
    handler = (
        RANGE_PROBE if run.variant == "range" else PATH_PROBE
    ) if is_probe else (
        RANGE_PRODUCTION if run.variant == "range" else PATH_PRODUCTION
    )
    guest = GUEST_DEEP_ABI15 if run.family == "deep" else GUEST_PRODUCTION
    return {
        "system/L/bfshandler": handler,
        "system/L/pfs3aio": PFS3_HANDLER,
        "system/C/fs-compare-bench": guest,
    }


def check_runtime_identities(run: Run, run_dir: Path) -> None:
    """Require exact installed handler, benchmark, and PFS3 identities."""
    path = run_dir / "runtime-inputs.sha256"
    if not path.is_file():
        raise QualificationError(f"{run.name}: missing installed-input digests")
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(f"{run.name}: could not read installed-input digests: {error}") from error
    observed: dict[str, str] = {}
    for line in lines:
        fields = line.split("  ", 1)
        if (len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0])
                or not fields[1] or fields[1] in observed):
            raise QualificationError(f"{run.name}: malformed or duplicate installed-input digest")
        observed[fields[1]] = fields[0]
    if observed != expected_runtime_inputs(run):
        raise QualificationError(f"{run.name}: installed input identity mismatch")


def check_phase_inventories(bfs: dict[str, str], pfs3: dict[str, str], run_name: str) -> None:
    bfs_phases = PARSER.phase_metrics(bfs)
    pfs3_phases = PARSER.phase_metrics(pfs3)
    if not bfs_phases:
        raise QualificationError(f"{run_name}: no phase timing metrics")
    if set(bfs_phases) != set(pfs3_phases):
        raise QualificationError(f"{run_name}: BFS/PFS3 phase inventory differs")


def check_rdb(run: Run, run_dir: Path) -> None:
    """Reuse the prior BFS0 validator and also require DH1's name to be unique."""
    PARSER.check_rdb(run, run_dir)
    path = run_dir / "bfs-rdb.json"
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise QualificationError(f"{run.name}: invalid BFS RDB JSON: {error}") from error
    partitions = document["rdb"]["partitions"]
    dh1_partitions = [
        partition for partition in partitions
        if isinstance(partition, dict) and partition.get("name") == "DH1"
    ]
    if len(dh1_partitions) != 1:
        raise QualificationError(
            f"{run.name}: expected exactly one DH1 RDB partition, found {len(dh1_partitions)}"
        )


def verify_run(run: Run, root: Path) -> VerifiedRun:
    run_dir = root / run.name
    errors = []
    try:
        check_runtime_identities(run, run_dir)
    except QualificationError as error:
        errors.append(str(error))
    try:
        check_rdb(run, run_dir)
    except QualificationError as error:
        errors.append(str(error))

    try:
        result = subprocess.run(
            ["/bin/bash", str(VERIFIER), str(run_dir), run.mode],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            errors.append(f"verifier failed with exit {result.returncode}: {detail}")
    except OSError as error:
        errors.append(f"could not run verifier: {error}")

    suffix, header, schema = PARSER.expected_schema(run.mode)
    results_dir = run_dir / "system/Results"
    values: dict[str, dict[str, str]] = {}
    for filesystem, drive in (("bfs", "DH1:"), ("pfs3", "DH2:")):
        try:
            values[filesystem] = PARSER.load_tsv(
                results_dir / f"{filesystem}.{suffix}", header, schema, drive
            )
        except QualificationError as error:
            errors.append(str(error))
    if "bfs" in values and "pfs3" in values:
        try:
            check_phase_inventories(values["bfs"], values["pfs3"], run.name)
        except QualificationError as error:
            errors.append(str(error))
    if errors:
        raise QualificationError(f"{run.name}: " + "; ".join(errors))
    return VerifiedRun(run, values["bfs"], values["pfs3"])


def verify_inventory(root: Path, runs: list[Run]) -> list[VerifiedRun]:
    errors = inventory_errors(root, runs)
    if errors:
        raise QualificationError("\n".join(errors))
    if not VERIFIER.is_file():
        raise QualificationError(f"repository verifier not found: {VERIFIER}")
    verified = []
    failures = []
    for run in runs:
        try:
            result = verify_run(run, root)
        except QualificationError as error:
            failures.append(str(error))
            continue
        verified.append(result)
        schema = PARSER.expected_schema(run.mode)[2]
        print(
            f"VERIFIED\t{run.name}\tmode={run.mode}\tschema={schema}\t"
            f"de_NumBuffers={run.buffers}"
        )
    if failures:
        raise QualificationError(
            "verification failures (no expected runs are omitted):\n  "
            + "\n  ".join(failures)
        )
    return verified


def group_rows(rows: list[VerifiedRun], family: str, variant: str) -> list[VerifiedRun]:
    return [row for row in rows if row.run.family == family and row.run.variant == variant]


def summarize_series(title: str, rows: list[VerifiedRun]) -> None:
    if not rows:
        raise QualificationError(f"no verified runs for summary {title}")
    phase_map = PARSER.phase_metrics(rows[0].bfs)
    phases = list(phase_map)
    for row in rows:
        check_phase_inventories(row.bfs, row.pfs3, row.run.name)
        if set(PARSER.phase_metrics(row.bfs)) != set(phases):
            raise QualificationError(f"{row.run.name}: phase inventory differs within {title}")
    print(f"\n== {title} ({len(rows)} required runs; no run filtering or outlier selection)")
    print(
        "phase\tBFS_mean_us\tBFS_CV_pct\tPFS3_mean_us\tPFS3_CV_pct\t"
        "BFS_over_PFS3_mean_factor\trun_factor_min\trun_factor_median\t"
        "run_factor_max\tfails_over_5x"
    )
    for phase in phases:
        bfs_values = [PARSER.phase_metrics(row.bfs)[phase] for row in rows]
        pfs_values = [PARSER.phase_metrics(row.pfs3)[phase] for row in rows]
        if any(value <= 0 for value in bfs_values + pfs_values):
            raise QualificationError(f"{title}: non-positive timing value for {phase}")
        bfs_mean, bfs_cv = PARSER.statistics_for(bfs_values)
        pfs_mean, pfs_cv = PARSER.statistics_for(pfs_values)
        factors = [bfs / pfs for bfs, pfs in zip(bfs_values, pfs_values)]
        print(
            f"{phase[:-3]}\t{bfs_mean:.2f}\t{bfs_cv:.2f}\t"
            f"{pfs_mean:.2f}\t{pfs_cv:.2f}\t{bfs_mean / pfs_mean:.4f}\t"
            f"{min(factors):.4f}\t{statistics.median(factors):.4f}\t"
            f"{max(factors):.4f}\t{sum(factor > OVER5_LIMIT for factor in factors)}/{len(factors)}"
        )


def summarize_path_range_pairs(family: str, rows: list[VerifiedRun]) -> None:
    range_by_repeat = {row.run.repeat: row for row in group_rows(rows, family, "range")}
    path_by_repeat = {row.run.repeat: row for row in group_rows(rows, family, "path")}
    if not range_by_repeat or range_by_repeat.keys() != path_by_repeat.keys():
        raise QualificationError(f"unpaired range/path repeat inventory for {family}")
    phases = list(PARSER.phase_metrics(next(iter(range_by_repeat.values())).bfs))
    for repeat in range_by_repeat:
        if range_by_repeat[repeat].run.order != path_by_repeat[repeat].run.order:
            raise QualificationError(f"paired order differs for {family} repeat {repeat}")
    print(f"\n== Paired adjacent BFS timings, family={family}; ratio=path/range")
    print("Every same-repeat, same-order path/range pair is included; slow pair means path BFS is slower.")
    print(
        "phase\trange_us_min/median/max\tpath_us_min/median/max\t"
        "path_over_range_min/median/max\tslow_pairs"
    )
    for phase in phases:
        range_values = []
        path_values = []
        factors = []
        for repeat in sorted(range_by_repeat):
            range_time = PARSER.phase_metrics(range_by_repeat[repeat].bfs)[phase]
            path_time = PARSER.phase_metrics(path_by_repeat[repeat].bfs)[phase]
            if range_time <= 0 or path_time <= 0:
                raise QualificationError(f"{family}: non-positive paired timing for {phase}")
            range_values.append(range_time)
            path_values.append(path_time)
            factors.append(path_time / range_time)
        range_summary = (min(range_values), statistics.median(range_values), max(range_values))
        path_summary = (min(path_values), statistics.median(path_values), max(path_values))
        factor_summary = (min(factors), statistics.median(factors), max(factors))
        slow_pairs = sum(path_time > range_time for path_time, range_time in zip(path_values, range_values))
        print(
            f"{phase[:-3]}\t{range_summary[0]:.0f}/{range_summary[1]:.0f}/{range_summary[2]:.0f}\t"
            f"{path_summary[0]:.0f}/{path_summary[1]:.0f}/{path_summary[2]:.0f}\t"
            f"{factor_summary[0]:.4f}/{factor_summary[1]:.4f}/{factor_summary[2]:.4f}\t"
            f"{slow_pairs}/{len(factors)}"
        )


DIAGNOSTIC_COUNTERS = (
    "BIO_READS",
    "NODE_CRC_READ_CALLS",
    "NODE_CRC_READ_SAMPLES",
    "NODE_CRC_READ_SAMPLE_TICKS",
    "DETAIL_INODE_READ_CALLS",
    "DETAIL_INODE_READ_SAMPLES",
    "DETAIL_INODE_READ_SAMPLE_TICKS",
    "DETAIL_INODE_SEARCH_CALLS",
    "DETAIL_INODE_SEARCH_SAMPLES",
    "DETAIL_INODE_SEARCH_SAMPLE_TICKS",
    "INODE_READ_CALLS",
    "BTREE_SEARCH_CALLS",
    "BTREE_INDEX_HINT_HITS",
    "BTREE_LEAF_HINT_HITS",
    "DIR_TREE_NODE_VIEWS",
    "DIR_TREE_RESIDENT_VIEWS",
    "INODE_TREE_NODE_VIEWS",
    "INODE_TREE_RESIDENT_VIEWS",
)


def summarize_deep_diagnostic_counts(rows: list[VerifiedRun]) -> None:
    if not rows:
        raise QualificationError("no verified deep runs for diagnostic counters")
    listing_phases = [
        name for name in PARSER.phase_metrics(rows[0].bfs)
        if name.startswith(("LIST_EXNEXT_", "LIST_EXALL_"))
    ]
    if len(listing_phases) != 12:
        raise QualificationError(
            f"deep schema 14 should contain 12 listing phases, got {len(listing_phases)}"
        )
    print("\n== Deep diagnostic counters (schema 14 BFS output; raw counters only)")
    print("No overlapping counter-time extrapolation is made. Sample ticks are shown as recorded counters.")
    print("variant\trepeat\torder\tphase\t" + "\t".join(DIAGNOSTIC_COUNTERS))
    for row in rows:
        for phase in listing_phases:
            prefix = phase[:-3]
            try:
                counters = [int(row.bfs[f"{prefix}_{counter}"]) for counter in DIAGNOSTIC_COUNTERS]
            except (KeyError, ValueError) as error:
                raise QualificationError(
                    f"{row.run.name}: missing or invalid diagnostic counter for {phase}"
                ) from error
            print(
                f"{row.run.variant}\t{row.run.repeat}\t{row.run.order}\t{prefix}\t"
                + "\t".join(str(value) for value in counters)
            )


def print_summary(rows: list[VerifiedRun], selection: str) -> None:
    families = [family for family in ("pilot", "deep", "normal", "durable")
                if any(row.run.family == family for row in rows)]
    print("\nNo expected run is filtered by PASS status; all runs must pass both filesystems.")
    print("No outlier selection is applied.")
    for family in families:
        for variant in ("range", "path"):
            selected = group_rows(rows, family, variant)
            if selected:
                summarize_series(
                    f"{family.capitalize()} {selected[0].run.mode}, variant={variant}", selected
                )
        summarize_path_range_pairs(family, rows)
    deep = [row for row in rows if row.run.family == "deep"]
    if deep:
        summarize_deep_diagnostic_counts(deep)

    if selection == "pilot":
        print("\nPILOT-ONLY RESULT: all 4 required pilot runs verified; NOT FULL QUALIFICATION.")
    elif selection == "deep":
        print("\nDEEP-ONLY RESULT: all 4 required deep runs verified; NOT FULL QUALIFICATION.")
    elif selection == "production-only":
        print("\nQUALIFICATION INVENTORY: all 32 required normal and durable runs verified.")
    else:
        print("\nQUALIFICATION INVENTORY: all 40 required runs verified.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--pilot", action="store_true", help="verify only the four normal-schema pilot runs")
    selection.add_argument("--deep", action="store_true", help="verify only the four deep schema 14 runs")
    selection.add_argument(
        "--production-only", action="store_true",
        help="verify only the 32 normal and durable production runs",
    )
    args = parser.parse_args()
    if args.pilot:
        selected, mode = PILOT_RUNS, "pilot only"
    elif args.deep:
        selected, mode = DEEP_RUNS, "deep only"
    elif args.production_only:
        selected, mode = PRODUCTION_RUNS, "production only"
    else:
        selected, mode = ALL_RUNS, "full strict inventory"
    print(
        f"Inventory mode: {mode}; required={len(selected)}. "
        "No valid-run filtering or outlier selection is applied."
    )
    try:
        rows = verify_inventory(SCRIPT_DIR, selected)
        print_summary(rows, "pilot" if args.pilot else "deep" if args.deep
                      else "production-only" if args.production_only else "full")
    except (QualificationError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
