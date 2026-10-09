#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the 2026-10-09 scan-admission run inventory."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import importlib.util
import json
from pathlib import Path
import re
import statistics
import subprocess  # nosec B404 - fixed repository verifier, without a shell
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[3]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
PREVIOUS_SUMMARY = (
    ROOT / "docs/qualification/evidence/bfs-leaf-range-performance-2026-10-08/summarize.py"
)
EXPECTED_BUFFER_COUNT = 30
OVER5_LIMIT = 5.0

M5_PRODUCTION = "79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4"
M5_ABI16_PROBE = "18f5415afa658dd570fbefe9baa2e9e72ba12c3a36d15eeca3570756998dbf43"
ADMISSION_PRODUCTION = "3e646ffb66c0b4dc15d3322c723579da88636ea056cb8836456a348a3852f12a"
ADMISSION_ABI16_PROBE = "2ae0f0894f18c2ce8cbec5cc0d88ecbb177b1cb919e6f5874967568945455eb6"
GUEST = "eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d"
PFS3_HANDLER = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"


def import_leaf_range_parser():
    """Reuse the immutable leaf-range parser without changing its assumptions."""
    spec = importlib.util.spec_from_file_location("leaf_range_summary", PREVIOUS_SUMMARY)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load shared parser: {PREVIOUS_SUMMARY}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


PARSER = import_leaf_range_parser()
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
    for family, prefix, mode in (
        ("pilot", "admission-pilot-normal-20261009", "compare"),
        ("deep", "admission-deep-20261009", "deep-compare"),
    ):
        for variant in ("m5", "admission"):
            for repeat, order in orders:
                runs.append(make_run(
                    f"{prefix}-{variant}-{repeat}-{order}",
                    mode, family, variant, repeat, order,
                ))

    for family, mode in (("normal", "compare"), ("durable", "durable-compare")):
        for variant in ("m5", "admission"):
            for repeat in range(1, 9):
                order = "bfs-first" if repeat % 2 else "pfs3-first"
                runs.append(make_run(
                    f"admission-{family}-20261009-{variant}-{repeat}-{order}",
                    mode, family, variant, repeat, order,
                ))
    return runs


ALL_RUNS = expected_runs()
PILOT_RUNS = [run for run in ALL_RUNS if run.family == "pilot"]
DEEP_RUNS = [run for run in ALL_RUNS if run.family == "deep"]
PRODUCTION_RUNS = [run for run in ALL_RUNS if run.family in {"normal", "durable"}]
PREFIX_BY_FAMILY = {
    "pilot": "admission-pilot-normal-20261009-",
    "deep": "admission-deep-20261009-",
    "normal": "admission-normal-20261009-",
    "durable": "admission-durable-20261009-",
}
if (len(ALL_RUNS), len(PILOT_RUNS), len(DEEP_RUNS), len(PRODUCTION_RUNS)) != (40, 4, 4, 32):
    raise RuntimeError("internal error: scan-admission run inventory count changed")


def inventory_errors(root: Path, expected: list[Run]) -> list[str]:
    expected_names = {run.name for run in expected}
    missing = sorted(name for name in expected_names if not (root / name).is_dir())
    relevant_families = {run.family for run in expected}
    prefixes = tuple(
        PREFIX_BY_FAMILY[family] for family in sorted(relevant_families)
    )
    unexpected = sorted(
        path.name for path in root.iterdir()
        if path.is_dir() and path.name.startswith(prefixes) and path.name not in expected_names
    ) if root.is_dir() else []
    errors = []
    if missing:
        errors.append("missing expected run directories:\n  " + "\n  ".join(missing))
    if unexpected:
        errors.append("unexpected run directories:\n  " + "\n  ".join(unexpected))
    if len(expected_names) != len(expected):
        errors.append("internal error: expected inventory contains duplicate names")
    return errors


def expected_schema(mode: str) -> tuple[str, str, int]:
    """Local schema override; the imported historical parser remains immutable."""
    if mode == "compare":
        return "tsv", "FS_COMPARE_BENCH", 4
    if mode == "durable-compare":
        return "durable.tsv", "FS_DURABLE_COMPARE", 4
    if mode == "deep-compare":
        return "deep-compare.tsv", "FS_DEEP_COMPARE", 15
    raise QualificationError(f"unsupported run mode: {mode}")


def expected_runtime_inputs(run: Run) -> dict[str, str]:
    if run.variant not in {"m5", "admission"}:
        raise QualificationError(f"{run.name}: unsupported variant {run.variant!r}")
    if run.family == "deep":
        handler = M5_ABI16_PROBE if run.variant == "m5" else ADMISSION_ABI16_PROBE
    else:
        handler = M5_PRODUCTION if run.variant == "m5" else ADMISSION_PRODUCTION
    return {
        "system/L/bfshandler": handler,
        "system/L/pfs3aio": PFS3_HANDLER,
        "system/C/fs-compare-bench": GUEST,
    }


def check_runtime_identities(run: Run, run_dir: Path) -> None:
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


def check_rdb(run: Run, run_dir: Path) -> None:
    """Use the previous strict RDB parser with this run's required 30 buffers."""
    PARSER.check_rdb(run, run_dir)


def check_phase_inventories(bfs: dict[str, str], pfs3: dict[str, str], run_name: str) -> None:
    bfs_phases = PARSER.phase_metrics(bfs)
    pfs3_phases = PARSER.phase_metrics(pfs3)
    if not bfs_phases:
        raise QualificationError(f"{run_name}: no phase timing rows")
    if set(bfs_phases) != set(pfs3_phases):
        raise QualificationError(f"{run_name}: BFS/PFS3 phase inventory differs")
    if any(value <= 0 for value in (*bfs_phases.values(), *pfs3_phases.values())):
        raise QualificationError(f"{run_name}: phase timings must be positive")


def verify_run(run: Run, root: Path) -> VerifiedRun:
    run_dir = root / run.name
    errors = []
    for check in (check_runtime_identities, check_rdb):
        try:
            check(run, run_dir)
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
            errors.append(f"strict repository verifier failed with exit {result.returncode}: {detail}")
    except OSError as error:
        errors.append(f"could not run strict repository verifier: {error}")

    suffix, header, schema = expected_schema(run.mode)
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
            item = verify_run(run, root)
        except QualificationError as error:
            failures.append(str(error))
            continue
        verified.append(item)
        print(
            f"VERIFIED\t{run.name}\tmode={run.mode}\tschema={expected_schema(run.mode)[2]}\t"
            f"de_NumBuffers={run.buffers}"
        )
    if failures:
        raise QualificationError(
            "verification failures (no expected runs are omitted):\n  " + "\n  ".join(failures)
        )
    return verified


def group_rows(rows: list[VerifiedRun], family: str, variant: str) -> list[VerifiedRun]:
    return [row for row in rows if row.run.family == family and row.run.variant == variant]


def phase_map(values: dict[str, str], run_name: str) -> dict[str, int]:
    try:
        phases = PARSER.phase_metrics(values)
    except QualificationError:
        raise
    if not phases:
        raise QualificationError(f"{run_name}: no phase timing rows")
    if any(value <= 0 for value in phases.values()):
        raise QualificationError(f"{run_name}: phase timings must be positive")
    return phases


def summarize_series(title: str, rows: list[VerifiedRun]) -> None:
    if not rows:
        raise QualificationError(f"no verified runs for summary {title}")
    first = phase_map(rows[0].bfs, rows[0].run.name)
    phases = list(first)
    for row in rows:
        check_phase_inventories(row.bfs, row.pfs3, row.run.name)
        if set(phase_map(row.bfs, row.run.name)) != set(phases):
            raise QualificationError(f"{row.run.name}: phase inventory differs within {title}")
    print(f"\n== {title} ({len(rows)} required runs; all phases and controls; no filtering)")
    print(
        "phase\tBFS_mean_us\tBFS_CV_pct\tPFS3_mean_us\tPFS3_CV_pct\t"
        "BFS_over_PFS3_mean\tindividual_BFS_over_PFS3_by_repeat_order\t"
        "individual_ratios_over_5x"
    )
    for phase in phases:
        bfs_values = [phase_map(row.bfs, row.run.name)[phase] for row in rows]
        pfs_values = [phase_map(row.pfs3, row.run.name)[phase] for row in rows]
        bfs_mean, bfs_cv = PARSER.statistics_for(bfs_values)
        pfs_mean, pfs_cv = PARSER.statistics_for(pfs_values)
        ratios = [bfs / pfs for bfs, pfs in zip(bfs_values, pfs_values)]
        individual = ";".join(
            f"{row.run.repeat}/{row.run.order}={ratio:.4f}"
            for row, ratio in zip(rows, ratios)
        )
        over5 = sum(ratio > OVER5_LIMIT for ratio in ratios)
        print(
            f"{phase[:-3]}\t{bfs_mean:.2f}\t{bfs_cv:.2f}\t{pfs_mean:.2f}\t"
            f"{pfs_cv:.2f}\t{bfs_mean / pfs_mean:.4f}\t{individual}\t{over5}/{len(ratios)}"
        )


def summarize_paired_admission_m5(family: str, rows: list[VerifiedRun]) -> None:
    m5_by_repeat = {row.run.repeat: row for row in group_rows(rows, family, "m5")}
    admission_by_repeat = {row.run.repeat: row for row in group_rows(rows, family, "admission")}
    if not m5_by_repeat or m5_by_repeat.keys() != admission_by_repeat.keys():
        raise QualificationError(f"unpaired M5/admission repeat inventory for {family}")
    for repeat in m5_by_repeat:
        if m5_by_repeat[repeat].run.order != admission_by_repeat[repeat].run.order:
            raise QualificationError(f"paired order differs for {family} repeat {repeat}")
    phases = list(phase_map(m5_by_repeat[min(m5_by_repeat)].bfs, family))
    print(f"\n== Paired admission/M5 timings, family={family}; ratio=admission/M5")
    print("Every same-repeat, same-order pair is included for BFS and PFS3.")
    print("phase\tfilesystem\trepeat/order\tM5_us\tadmission_us\tadmission_over_M5")
    paired: dict[tuple[str, str], list[tuple[int, str, int, int, float]]] = {}
    for phase in phases:
        for filesystem in ("bfs", "pfs3"):
            for repeat in sorted(m5_by_repeat):
                m5_row = m5_by_repeat[repeat]
                admission_row = admission_by_repeat[repeat]
                m5_phases = phase_map(getattr(m5_row, filesystem), m5_row.run.name)
                admission_phases = phase_map(getattr(admission_row, filesystem), admission_row.run.name)
                if set(m5_phases) != set(phases) or set(admission_phases) != set(phases):
                    raise QualificationError(f"{family}: phase inventory differs in paired timing")
                m5_us = m5_phases[phase]
                admission_us = admission_phases[phase]
                ratio = admission_us / m5_us
                paired.setdefault((phase, filesystem), []).append(
                    (repeat, m5_row.run.order, m5_us, admission_us, ratio)
                )
                print(
                    f"{phase[:-3]}\t{filesystem.upper()}\t{repeat}/{m5_row.run.order}\t"
                    f"{m5_us}\t{admission_us}\t{ratio:.4f}"
                )
    print("\nPaired admission/M5 aggregate across all repeats.")
    print("phase\tfilesystem\tadmission_over_M5_min\tadmission_over_M5_median\t"
          "admission_over_M5_max\tadmission_slower_pairs")
    for phase in phases:
        for filesystem in ("bfs", "pfs3"):
            ratios = [pair[4] for pair in paired[(phase, filesystem)]]
            slower_pairs = sum(pair[3] > pair[2] for pair in paired[(phase, filesystem)])
            print(
                f"{phase[:-3]}\t{filesystem.upper()}\t{min(ratios):.4f}\t"
                f"{statistics.median(ratios):.4f}\t{max(ratios):.4f}\t"
                f"{slower_pairs}/{len(ratios)}"
            )


DEEP_RAW_COUNTERS = (
    "BIO_READS",
    "CACHE_READ_CALLS",
    "CACHE_READ_HITS",
    "CACHE_READ_MISSES",
    "INODE_READ_CALLS",
    "NODE_CRC_READ_CALLS",
    "NODE_CRC_READ_SAMPLES",
    "NODE_CRC_READ_SAMPLE_TICKS",
    "NODE_CRC_WRITE_CALLS",
    "NODE_CRC_WRITE_SAMPLES",
    "NODE_CRC_WRITE_SAMPLE_TICKS",
    "DIR_TREE_NODE_VIEWS",
    "DIR_TREE_RESIDENT_VIEWS",
    "DIR_TREE_LEAF_NODE_VIEWS",
    "DIR_TREE_LEAF_RESIDENT_VIEWS",
    "DIR_TREE_INTERNAL_NODE_VIEWS",
    "DIR_TREE_INTERNAL_RESIDENT_VIEWS",
    "INODE_TREE_NODE_VIEWS",
    "INODE_TREE_RESIDENT_VIEWS",
    "INODE_TREE_LEAF_NODE_VIEWS",
    "INODE_TREE_LEAF_RESIDENT_VIEWS",
    "INODE_TREE_INTERNAL_NODE_VIEWS",
    "INODE_TREE_INTERNAL_RESIDENT_VIEWS",
)


def summarize_deep_counters(rows: list[VerifiedRun]) -> None:
    if not rows:
        raise QualificationError("no verified deep runs for diagnostic counters")
    phases = [
        name for name in phase_map(rows[0].bfs, rows[0].run.name)
        if name.startswith(("LIST_EXNEXT_", "LIST_EXALL_"))
    ]
    if len(phases) != 12:
        raise QualificationError(f"deep schema 15 should contain 12 listing phases, got {len(phases)}")
    print("\n== Deep diagnostic counters (schema 15; BFS raw counters only)")
    print("Counts, samples and sampled ticks are emitted as recorded; no exclusive CPU attribution or extrapolation is made.")
    print("variant\trepeat\torder\tphase\t" + "\t".join(DEEP_RAW_COUNTERS))
    for row in rows:
        for phase in phases:
            prefix = phase[:-3]
            try:
                counters = [int(row.bfs[f"{prefix}_{name}"]) for name in DEEP_RAW_COUNTERS]
            except (KeyError, ValueError) as error:
                raise QualificationError(
                    f"{row.run.name}: missing or invalid raw diagnostic counter for {phase}"
                ) from error
            if any(value < 0 for value in counters):
                raise QualificationError(f"{row.run.name}: negative raw diagnostic counter for {phase}")
            print(
                f"{row.run.variant}\t{row.run.repeat}\t{row.run.order}\t{prefix}\t"
                + "\t".join(str(value) for value in counters)
            )


def print_summary(rows: list[VerifiedRun], selection: str) -> None:
    print("\nAll required runs passed the strict repository verifier; no row or outlier filtering is applied.")
    for family in ("pilot", "deep", "normal", "durable"):
        if not any(row.run.family == family for row in rows):
            continue
        for variant in ("m5", "admission"):
            selected = group_rows(rows, family, variant)
            if selected:
                summarize_series(
                    f"{family.capitalize()} {selected[0].run.mode}, variant={variant}", selected
                )
        summarize_paired_admission_m5(family, rows)
    deep_rows = [row for row in rows if row.run.family == "deep"]
    if deep_rows:
        summarize_deep_counters(deep_rows)
    if selection == "pilot":
        print("\nPILOT-ONLY RESULT: all four normal-schema pilot runs verified; NOT FULL QUALIFICATION.")
    elif selection == "deep":
        print("\nDEEP-ONLY RESULT: all four schema-15 deep runs verified; NOT FULL QUALIFICATION.")
    else:
        print("\nQUALIFICATION INVENTORY: all 40 required runs verified.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--pilot", action="store_true", help="verify only the four normal-schema pilot runs")
    selection.add_argument("--deep", action="store_true", help="verify only the four schema-15 deep runs")
    args = parser.parse_args()
    if args.pilot:
        selected, mode = PILOT_RUNS, "pilot only"
    elif args.deep:
        selected, mode = DEEP_RUNS, "deep only"
    else:
        selected, mode = ALL_RUNS, "full strict inventory"
    print(f"Inventory mode: {mode}; required={len(selected)}. No run filtering or outlier selection is applied.")
    try:
        rows = verify_inventory(SCRIPT_DIR, selected)
        print_summary(rows, "pilot" if args.pilot else "deep" if args.deep else "full")
    except (QualificationError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
