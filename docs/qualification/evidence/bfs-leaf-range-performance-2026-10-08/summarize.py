#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the 2026-10-08 leaf-range run inventory."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import statistics
import subprocess  # nosec B404 - invokes the fixed repository verifier, without a shell
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[3]
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
BFS_DOSTYPE = 0x42465300
OVER5_LIMIT = 5.0


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


class QualificationError(Exception):
    """An inventory, evidence, or verification failure."""


def make_run(name: str, mode: str, buffers: int, family: str,
             variant: str, repeat: int, order: str) -> Run:
    return Run(name, mode, buffers, family, variant, repeat, order)


def expected_runs() -> list[Run]:
    runs: list[Run] = []
    for buffers in (30, 64, 128):
        for repeat, order in ((1, "bfs-first"), (2, "pfs3-first")):
            runs.append(make_run(
                f"cache-capacity-20261008-b{buffers}-{repeat}-{order}",
                "compare", buffers, "cache", f"b{buffers}", repeat, order,
            ))

    for variant in ("m3", "range"):
        for repeat, order in ((1, "bfs-first"), (2, "pfs3-first")):
            runs.append(make_run(
                f"leaf-range-pilot-20261008-{variant}-{repeat}-{order}",
                "compare", 30, "pilot", variant, repeat, order,
            ))

    for family, mode in (("normal", "compare"), ("durable", "durable-compare")):
        for variant in ("m3", "range"):
            for repeat in range(1, 9):
                order = "bfs-first" if repeat % 2 else "pfs3-first"
                runs.append(make_run(
                    f"leaf-range-{family}-20261008-{variant}-{repeat}-{order}",
                    mode, 30, family, variant, repeat, order,
                ))

    for repeat, order in ((1, "bfs-first"), (2, "pfs3-first")):
        runs.append(make_run(
            f"leaf-range-deep-20261008-range-{repeat}-{order}",
            "deep-compare", 30, "deep", "range", repeat, order,
        ))
    return runs


ALL_RUNS = expected_runs()
PILOT_RUNS = [run for run in ALL_RUNS if run.family in {"cache", "pilot"}]
PRODUCTION_RUNS = [run for run in ALL_RUNS if run.family in {"normal", "durable", "deep"}]
if (len(ALL_RUNS), len(PILOT_RUNS), len(PRODUCTION_RUNS)) != (44, 10, 34):
    raise RuntimeError("internal error: run inventory count changed")


def inventory_errors(expected: list[Run]) -> list[str]:
    expected_names = {run.name for run in expected}
    missing = sorted(name for name in expected_names if not (SCRIPT_DIR / name).is_dir())
    relevant_families = {run.family for run in expected}
    relevant_prefixes = tuple(
        prefix for prefix, family in (
            ("cache-capacity-20261008-", "cache"),
            ("leaf-range-pilot-20261008-", "pilot"),
            ("leaf-range-normal-20261008-", "normal"),
            ("leaf-range-durable-20261008-", "durable"),
            ("leaf-range-deep-20261008-", "deep"),
        )
        if family in relevant_families
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


def expected_schema(mode: str) -> tuple[str, str, int]:
    if mode == "compare":
        return "tsv", "FS_COMPARE_BENCH", 4
    if mode == "durable-compare":
        return "durable.tsv", "FS_DURABLE_COMPARE", 4
    if mode == "deep-compare":
        return "deep-compare.tsv", "FS_DEEP_COMPARE", 14
    raise QualificationError(f"unsupported run mode: {mode}")


def load_tsv(path: Path, header: str, schema: int, drive: str) -> dict[str, str]:
    if not path.is_file():
        raise QualificationError(f"missing TSV output: {path}")
    values: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(f"could not read ASCII TSV {path}: {error}") from error
    for line_number, line in enumerate(lines, 1):
        fields = line.split("\t")
        if len(fields) != 2 or not fields[0]:
            raise QualificationError(f"malformed TSV row at {path}:{line_number}")
        name, value = fields
        if name in values:
            raise QualificationError(f"duplicate TSV key {name!r} at {path}:{line_number}")
        values[name] = value
    if values.get(header) != str(schema):
        raise QualificationError(
            f"{path}: expected {header} schema {schema}, got {values.get(header)!r}"
        )
    if values.get("DRIVE") != drive:
        raise QualificationError(f"{path}: expected DRIVE={drive}, got {values.get('DRIVE')!r}")
    if values.get("PASS") != "1":
        raise QualificationError(f"{path}: expected exactly one PASS=1 row")
    return values


def check_rdb(run: Run, run_dir: Path) -> None:
    path = run_dir / "bfs-rdb.json"
    if not path.is_file():
        raise QualificationError(f"{run.name}: missing BFS RDB JSON: {path}")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise QualificationError(f"{run.name}: invalid BFS RDB JSON: {error}") from error

    if not isinstance(document, dict) or not isinstance(document.get("rdb"), dict):
        raise QualificationError(f"{run.name}: RDB JSON has no rdb object")
    partitions = document["rdb"].get("partitions")
    if not isinstance(partitions, list):
        raise QualificationError(f"{run.name}: RDB JSON has no partitions list")
    bfs_partitions = [
        part for part in partitions
        if isinstance(part, dict)
        and isinstance(part.get("dos_env"), dict)
        and (
            part["dos_env"].get("dos_type") == BFS_DOSTYPE
            or part["dos_env"].get("dos_type_str") == "BFS0"
        )
    ]
    if len(bfs_partitions) != 1:
        raise QualificationError(
            f"{run.name}: expected exactly one BFS0 RDB partition, found {len(bfs_partitions)}"
        )
    partition = bfs_partitions[0]
    if (partition.get("name") != "DH1"
            or partition["dos_env"].get("dos_type") != BFS_DOSTYPE
            or partition["dos_env"].get("dos_type_str") != "BFS0"):
        raise QualificationError(f"{run.name}: unique BFS partition must be DH1/BFS0")
    observed = partition["dos_env"].get("num_buffer")
    if type(observed) is not int or observed != run.buffers:
        raise QualificationError(
            f"{run.name}: expected de_NumBuffers={run.buffers}, got {observed!r}"
        )


def verify_run(run: Run) -> VerifiedRun:
    run_dir = SCRIPT_DIR / run.name
    check_runtime_identities(run, run_dir)
    errors = []
    try:
        result = subprocess.run(
            ["/bin/bash", str(VERIFIER), str(run_dir), run.mode],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            errors.append(
                f"verifier failed with exit {result.returncode}: {detail}"
            )
    except OSError as error:
        errors.append(f"could not run verifier: {error}")

    try:
        check_rdb(run, run_dir)
    except QualificationError as error:
        errors.append(str(error))
    suffix, header, schema = expected_schema(run.mode)
    results_dir = run_dir / "system/Results"
    values = {}
    for filesystem, drive in (("bfs", "DH1:"), ("pfs3", "DH2:")):
        try:
            values[filesystem] = load_tsv(
                results_dir / f"{filesystem}.{suffix}", header, schema, drive
            )
        except QualificationError as error:
            errors.append(str(error))
    bfs = values.get("bfs")
    pfs3 = values.get("pfs3")
    if bfs is not None and pfs3 is not None:
        try:
            if set(phase_metrics(bfs)) != set(phase_metrics(pfs3)):
                errors.append("BFS/PFS3 phase inventory differs")
        except QualificationError as error:
            errors.append(str(error))
    if errors:
        raise QualificationError(f"{run.name}: " + "; ".join(errors))
    return VerifiedRun(run, bfs, pfs3)


def check_runtime_identities(run: Run, run_dir: Path) -> None:
    """Check recorded installed copies, not a forensic digest of the RDB image."""
    path = run_dir / "runtime-inputs.sha256"
    if not path.is_file():
        raise QualificationError(f"{run.name}: missing installed-input digests")
    observed = {}
    for line in path.read_text(encoding="ascii").splitlines():
        fields = line.split("  ", 1)
        if len(fields) != 2 or fields[1] in observed:
            raise QualificationError(f"{run.name}: malformed or duplicate installed-input digest")
        observed[fields[1]] = fields[0]
    if run.family == "deep":
        handler = "a46a153a387de6cac773aee8bc6b757ce0bccf7a6dbc03b2c6ec8f8f8cc7981c"
        guest = "7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661"
    else:
        guest = "798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52"
        if run.family == "cache" or run.variant == "m3":
            handler = "adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27"
        elif run.family == "pilot":
            handler = "cc77f452e54dd192694ae14010c8d597f8fecc124af9d21db4c5e5cc69602d14"
        else:
            handler = "79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4"
    expected = {
        "system/L/bfshandler": handler,
        "system/L/pfs3aio": "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7",
        "system/C/fs-compare-bench": guest,
    }
    if observed != expected:
        raise QualificationError(f"{run.name}: installed input identity mismatch")


def phase_metrics(values: dict[str, str]) -> dict[str, int]:
    phases: dict[str, int] = {}
    for name, value in values.items():
        if name.endswith("_US"):
            try:
                phases[name] = int(value)
            except ValueError as error:
                raise QualificationError(f"non-integer timing value for {name}: {value!r}") from error
    return phases


def statistics_for(values: list[float | int]) -> tuple[float, float]:
    mean = statistics.mean(values)
    cv = statistics.stdev(values) / mean * 100.0 if len(values) > 1 and mean else 0.0
    return mean, cv


def summarize_series(title: str, rows: list[VerifiedRun]) -> None:
    if not rows:
        raise QualificationError(f"no verified runs for summary {title}")
    phases = list(phase_metrics(rows[0].bfs))
    if not phases:
        raise QualificationError(f"no phase timing metrics for {title}")
    print(f"\n== {title} ({len(rows)} required runs; no run filtering)")
    print("phase\tBFS_mean_us\tBFS_CV_pct\tPFS3_mean_us\tPFS3_CV_pct\tratio_of_means\trun_ratio_min\trun_ratio_max\trun_ratio_median\tover_5x")
    for phase in phases:
        bfs_values = [phase_metrics(row.bfs)[phase] for row in rows]
        pfs_values = [phase_metrics(row.pfs3)[phase] for row in rows]
        bfs_mean, bfs_cv = statistics_for(bfs_values)
        pfs_mean, pfs_cv = statistics_for(pfs_values)
        ratios = [bfs / pfs for bfs, pfs in zip(bfs_values, pfs_values)]
        print(
            f"{phase[:-3]}\t{bfs_mean:.2f}\t{bfs_cv:.2f}\t"
            f"{pfs_mean:.2f}\t{pfs_cv:.2f}\t{bfs_mean / pfs_mean:.4f}\t"
            f"{min(ratios):.4f}\t{max(ratios):.4f}\t{statistics.median(ratios):.4f}\t"
            f"{sum(ratio > OVER5_LIMIT for ratio in ratios)}/{len(ratios)}"
        )


def summarize_pairs(mode: str, old_rows: list[VerifiedRun], new_rows: list[VerifiedRun]) -> None:
    old_by_repeat = {row.run.repeat: row for row in old_rows}
    new_by_repeat = {row.run.repeat: row for row in new_rows}
    if old_by_repeat.keys() != new_by_repeat.keys():
        raise QualificationError(f"unpaired repeat inventory for {mode}")
    phases = list(phase_metrics(old_rows[0].bfs))
    print(f"\n== Paired BFS timing, mode={mode}; baseline=m3, candidate=range")
    print("Lower candidate/M3 ratios favor the candidate. Every compare phase, including controls, is shown.")
    print("phase\tcandidate_over_m3_by_repeat_and_order\tratio_min\tratio_max\tratio_median\tphase_mean_reduction_pct")
    for phase in phases:
        ratios_by_run: list[float] = []
        ratios_display: list[str] = []
        old_values: list[int] = []
        new_values: list[int] = []
        for repeat in sorted(old_by_repeat):
            old_row = old_by_repeat[repeat]
            new_row = new_by_repeat[repeat]
            if old_row.run.order != new_row.run.order:
                raise QualificationError(f"paired order differs for {mode} repeat {repeat}")
            old_time = phase_metrics(old_row.bfs)[phase]
            new_time = phase_metrics(new_row.bfs)[phase]
            ratio = new_time / old_time
            ratios_by_run.append(ratio)
            ratios_display.append(f"{repeat}/{old_row.run.order}={ratio:.4f}")
            old_values.append(old_time)
            new_values.append(new_time)
        reduction = (
            (statistics.mean(old_values) - statistics.mean(new_values))
            / statistics.mean(old_values) * 100.0
        )
        print(
            f"{phase[:-3]}\t{';'.join(ratios_display)}\t"
            f"{min(ratios_by_run):.4f}\t{max(ratios_by_run):.4f}\t"
            f"{statistics.median(ratios_by_run):.4f}\t{reduction:.2f}"
        )


def summarize_deep_listing(rows: list[VerifiedRun]) -> None:
    listing_phases = [
        name for name in phase_metrics(rows[0].bfs)
        if name.startswith(("LIST_EXNEXT_", "LIST_EXALL_"))
    ]
    if len(listing_phases) != 12:
        raise QualificationError(
            f"deep schema 14 should contain 12 listing phases, got {len(listing_phases)}"
        )
    print("\n== Deep listing counters (schema 14, BFS probe only)")
    print("NOTE: sample ticks are raw sampled counters; no production-time extrapolation is made.")
    print("BTREE_INDEX_HINT_HITS counts exact-key index hints; BTREE_LEAF_HINT_HITS combines legacy and range leaf hints.")
    print("repeat\torder\tphase\tBFS_us\tPFS3_us\tINODE_SEARCH_calls/samples/ticks\texact_index_hint_hits\tcombined_leaf_hint_hits\tall_hint_hits\tINODE_TREE_views/resident\tDIR_TREE_views/resident")
    for row in rows:
        bfs_phases = phase_metrics(row.bfs)
        pfs3_phases = phase_metrics(row.pfs3)
        for phase in listing_phases:
            prefix = phase[:-3]
            try:
                search = (
                    int(row.bfs[f"{prefix}_DETAIL_INODE_SEARCH_CALLS"]),
                    int(row.bfs[f"{prefix}_DETAIL_INODE_SEARCH_SAMPLES"]),
                    int(row.bfs[f"{prefix}_DETAIL_INODE_SEARCH_SAMPLE_TICKS"]),
                )
                exact_hits = int(row.bfs[f"{prefix}_BTREE_INDEX_HINT_HITS"])
                leaf_hits = int(row.bfs[f"{prefix}_BTREE_LEAF_HINT_HITS"])
                inode_views = int(row.bfs[f"{prefix}_INODE_TREE_NODE_VIEWS"])
                inode_resident = int(row.bfs[f"{prefix}_INODE_TREE_RESIDENT_VIEWS"])
                dir_views = int(row.bfs[f"{prefix}_DIR_TREE_NODE_VIEWS"])
                dir_resident = int(row.bfs[f"{prefix}_DIR_TREE_RESIDENT_VIEWS"])
            except (KeyError, ValueError) as error:
                raise QualificationError(
                    f"{row.run.name}: missing or invalid listing counter for {phase}"
                ) from error
            print(
                f"{row.run.repeat}\t{row.run.order}\t{prefix}\t"
                f"{bfs_phases[phase]}\t{pfs3_phases[phase]}\t"
                f"{search[0]}/{search[1]}/{search[2]}\t{exact_hits}\t{leaf_hits}\t"
                f"{exact_hits + leaf_hits}\t{inode_views}/{inode_resident}\t"
                f"{dir_views}/{dir_resident}"
            )


def group_rows(rows: list[VerifiedRun], family: str, variant: str) -> list[VerifiedRun]:
    return [row for row in rows if row.run.family == family and row.run.variant == variant]


def verify_inventory(runs: list[Run]) -> list[VerifiedRun]:
    errors = inventory_errors(runs)
    if errors:
        raise QualificationError("\n".join(errors))
    if not VERIFIER.is_file():
        raise QualificationError(f"repository verifier not found: {VERIFIER}")
    verified = []
    failures = []
    for run in runs:
        try:
            result = verify_run(run)
        except QualificationError as error:
            failures.append(str(error))
            continue
        verified.append(result)
        schema = expected_schema(run.mode)[2]
        print(f"VERIFIED\t{run.name}\tmode={run.mode}\tschema={schema}\t"
              f"de_NumBuffers={run.buffers}")
    if failures:
        raise QualificationError(
            "verification failures (no runs are omitted):\n  "
            + "\n  ".join(failures)
        )
    return verified


def print_pilot_summary(rows: list[VerifiedRun]) -> None:
    print("\nHistorical pilot summaries are exploratory and are not qualification evidence.")
    for buffers in (30, 64, 128):
        summarize_series(
            f"Historical cache-capacity pilot, de_NumBuffers={buffers}",
            [row for row in rows if row.run.family == "cache" and row.run.buffers == buffers],
        )
    for variant in ("m3", "range"):
        summarize_series(
            f"Historical leaf-range pilot, variant={variant} (candidate cc77 superseded)",
            group_rows(rows, "pilot", variant),
        )


def print_production_summary(rows: list[VerifiedRun]) -> None:
    for family, mode in (("normal", "compare"), ("durable", "durable-compare")):
        for variant in ("m3", "range"):
            summarize_series(
                f"Production {family}, variant={variant}",
                group_rows(rows, family, variant),
            )
        summarize_pairs(
            mode,
            group_rows(rows, family, "m3"),
            group_rows(rows, family, "range"),
        )
    deep = group_rows(rows, "deep", "range")
    summarize_series("Production deep-compare, variant=range", deep)
    summarize_deep_listing(deep)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pilot-only",
        action="store_true",
        help="verify and summarize the ten historical pilot runs; this is not qualification",
    )
    args = parser.parse_args()
    selected = PILOT_RUNS if args.pilot_only else ALL_RUNS
    print(
        f"Inventory mode: {'historical pilots only' if args.pilot_only else 'strict 44-run qualification'}; "
        f"required={len(selected)}. No valid-run filtering is applied."
    )
    try:
        rows = verify_inventory(selected)
        if args.pilot_only:
            print_pilot_summary(rows)
            print("\nPILOT-ONLY RESULT: all 10 historical pilots verified; NOT QUALIFICATION.")
        else:
            print_pilot_summary([row for row in rows if row.run.family in {"cache", "pilot"}])
            print_production_summary(rows)
            print("\nQUALIFICATION INVENTORY: all 44 required runs verified.")
    except (QualificationError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
