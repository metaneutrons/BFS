#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the fixed resident-root floor pilot."""

from __future__ import annotations

import argparse
import configparser
from dataclasses import dataclass
import importlib.util
import json
from pathlib import Path, PurePosixPath
import subprocess  # nosec B404 - fixed repository verifier, without a shell
import sys
from typing import Any


BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[3]
PRODUCTION_VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
PROFILE_SUMMARY_PATH = (
    ROOT / "docs/qualification/evidence/bfs-read-flush-cachy-2026-10-09"
    / "summarize_profile.py"
)
SEALED_SUMMARY_PATH = (
    ROOT / "docs/qualification/evidence/bfs-sparse-key-crc-performance-2026-10-09"
    / "summarize.py"
)

EXPECTED_BUFFERS = 30
EXPECTED_ASSET_COUNT = 67
RUNTIME_PATHS = (
    "system/L/bfshandler",
    "system/L/pfs3aio",
    "system/C/fs-compare-bench",
)

PRODUCTION_BASE_HANDLER_SHA256 = (
    "486e3bbbf5c09a2b7d5cae0e3afd02a3a0a57d76a892d0aca6dfec31dbdb21f4"
)
PRODUCTION_CANDIDATE_HANDLER_SHA256 = (
    "3dba24d2af0d568d723717f1efcff75784014a165afc84d9da5458facf11fcd5"
)
DIAGNOSTIC_BASE_HANDLER_SHA256 = (
    "e2f99ecc68e411358f96e5f85a9d452518af0d8c127b4cec4a3d3319d191d39c"
)
DIAGNOSTIC_CANDIDATE_HANDLER_SHA256 = (
    "c5e14aee449d2a18984cfdfb9e183748a2fb7246c1724d41556e952dea63e5d3"
)
PRODUCTION_GUEST_SHA256 = (
    "eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d"
)
DIAGNOSTIC_GUEST_SHA256 = (
    "20e751e8c558af9ca80ed4c597162fad272326fb8cf0265c3f4a7ad8c2395bf2"
)
PFS3_SHA256 = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load shared evidence helper: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


# These summaries own the maintained protocol inventories and their validators.
PROFILE = _load_module("bfs_floor_profile_helpers", PROFILE_SUMMARY_PATH)
SEALED = _load_module("bfs_floor_sealed_helpers", SEALED_SUMMARY_PATH)
SPLIT_VERIFIER = PROFILE.VERIFIER
BENCH_PARSER = SEALED.PARSER


class QualificationError(ValueError):
    """An inventory, identity, configuration, or protocol failure."""


@dataclass(frozen=True)
class ScheduleEntry:
    sequence: int
    family: str
    repeat: int
    order: str
    variant: str
    run_name: str

    @property
    def mode(self) -> str:
        if self.family == "diag":
            return "split-write-durable-compare"
        return "durable-compare" if self.family in {"durable", "aa-durable"} else "compare"

    @property
    def guest_sha256(self) -> str:
        return DIAGNOSTIC_GUEST_SHA256 if self.family == "diag" else PRODUCTION_GUEST_SHA256

    @property
    def handler_sha256(self) -> str:
        if self.family == "diag":
            return (DIAGNOSTIC_BASE_HANDLER_SHA256 if self.variant == "base"
                    else DIAGNOSTIC_CANDIDATE_HANDLER_SHA256)
        if self.variant in {"base", "basea", "baseb"}:
            return PRODUCTION_BASE_HANDLER_SHA256
        return PRODUCTION_CANDIDATE_HANDLER_SHA256

    def fields(self) -> tuple[str, ...]:
        return (
            str(self.sequence), self.family, str(self.repeat), self.order,
            self.variant, self.run_name,
        )


SCHEDULE_HEADER = ("sequence", "family", "repeat", "order", "variant", "run_name")


def expected_schedule() -> list[ScheduleEntry]:
    rows = (
        ("diag", 1, "bfs-first", "base"),
        ("diag", 1, "bfs-first", "candidate"),
        ("normal", 1, "bfs-first", "base"),
        ("normal", 1, "bfs-first", "candidate"),
        ("durable", 1, "bfs-first", "base"),
        ("durable", 1, "bfs-first", "candidate"),
        ("durable", 2, "pfs3-first", "candidate"),
        ("durable", 2, "pfs3-first", "base"),
        ("normal", 2, "pfs3-first", "candidate"),
        ("normal", 2, "pfs3-first", "base"),
        ("aa-normal", 1, "bfs-first", "basea"),
        ("aa-normal", 1, "bfs-first", "baseb"),
        ("aa-durable", 1, "pfs3-first", "basea"),
        ("aa-durable", 1, "pfs3-first", "baseb"),
    )
    entries = [
        ScheduleEntry(
            sequence, family, repeat, order, variant,
            f"floor-{family}-{variant}-{repeat}-{order}",
        )
        for sequence, (family, repeat, order, variant) in enumerate(rows, start=1)
    ]
    if len(entries) != 14 or len({entry.run_name for entry in entries}) != 14:
        raise RuntimeError("internal error: the fixed floor pilot must contain 14 unique runs")
    return entries


EXPECTED_SCHEDULE = expected_schedule()


def validate_schedule(results_root: Path) -> list[ScheduleEntry]:
    path = results_root / "schedule.tsv"
    try:
        content = path.read_bytes()
    except OSError as error:
        raise QualificationError(f"missing or unreadable schedule.tsv: {error}") from error
    if not content.endswith(b"\n") or b"\r" in content:
        raise QualificationError("schedule.tsv must be LF-terminated without CR bytes")
    try:
        lines = content.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise QualificationError("schedule.tsv must be ASCII") from error
    expected = ["\t".join(SCHEDULE_HEADER)]
    expected.extend("\t".join(entry.fields()) for entry in EXPECTED_SCHEDULE)
    if lines != expected:
        for line_number, (observed, required) in enumerate(zip(lines, expected), start=1):
            if observed != required:
                raise QualificationError(
                    f"schedule.tsv line {line_number} differs from the fixed 14-run schedule"
                )
        raise QualificationError(
            f"schedule.tsv has {len(lines)} lines; expected {len(expected)}"
        )
    return EXPECTED_SCHEDULE


def check_run_inventory(results_root: Path, schedule: list[ScheduleEntry]) -> None:
    expected = {entry.run_name for entry in schedule}
    try:
        actual = {path.name for path in results_root.iterdir() if path.is_dir()}
    except OSError as error:
        raise QualificationError(f"could not enumerate run directories: {error}") from error
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing or unexpected:
        details = []
        if missing:
            details.append("missing=" + ", ".join(missing))
        if unexpected:
            details.append("unexpected=" + ", ".join(unexpected))
        raise QualificationError("run-directory inventory differs from schedule: " + "; ".join(details))


def expected_runtime_receipt(entry: ScheduleEntry) -> list[tuple[str, str]]:
    return [
        (entry.handler_sha256, RUNTIME_PATHS[0]),
        (PFS3_SHA256, RUNTIME_PATHS[1]),
        (entry.guest_sha256, RUNTIME_PATHS[2]),
    ]


def check_runtime_receipts(entry: ScheduleEntry, run_dir: Path) -> None:
    expected = expected_runtime_receipt(entry)
    for name in ("runtime-inputs.sha256", "runtime-inputs.post.sha256"):
        try:
            observed = PROFILE.read_receipt(run_dir / name)
        except ValueError as error:
            raise QualificationError(f"{entry.run_name}: invalid {name}: {error}") from error
        if observed != expected:
            raise QualificationError(
                f"{entry.run_name}: {name} does not match the pinned installed inputs"
            )


def _safe_asset_path(run_name: str, path: str) -> None:
    pure = PurePosixPath(path)
    if (pure.is_absolute() or ".." in pure.parts or not pure.parts
            or pure.parts[0] not in {"C", "L", "Libs"}):
        raise QualificationError(f"{run_name}: invalid installed asset path {path!r}")


def check_installed_assets(
    schedule: list[ScheduleEntry], results_root: Path,
) -> dict[str, dict[str, str]]:
    assets_by_group: dict[str, dict[str, str]] = {}
    for entry in schedule:
        group = "diagnostic" if entry.family == "diag" else "production"
        path = results_root / entry.run_name / "installed-assets.sha256"
        try:
            receipt = PROFILE.read_receipt(path)
        except ValueError as error:
            raise QualificationError(f"{entry.run_name}: invalid installed-assets.sha256: {error}") from error
        if len(receipt) != EXPECTED_ASSET_COUNT:
            raise QualificationError(
                f"{entry.run_name}: expected exactly {EXPECTED_ASSET_COUNT} installed asset mappings, "
                f"found {len(receipt)}"
            )
        assets = {asset_path: digest for digest, asset_path in receipt}
        if len(assets) != EXPECTED_ASSET_COUNT:
            raise QualificationError(f"{entry.run_name}: duplicate installed asset path")
        for asset_path in assets:
            _safe_asset_path(entry.run_name, asset_path)
        if "L/bfshandler" in assets:
            raise QualificationError(
                f"{entry.run_name}: the handler must be recorded in runtime-inputs receipts"
            )
        if assets.get("C/fs-compare-bench") != entry.guest_sha256:
            raise QualificationError(f"{entry.run_name}: installed guest digest mismatch")
        if assets.get("L/pfs3aio") != PFS3_SHA256:
            raise QualificationError(f"{entry.run_name}: installed PFS3 digest mismatch")
        previous = assets_by_group.get(group)
        if previous is None:
            assets_by_group[group] = assets
        elif assets != previous:
            raise QualificationError(
                f"{entry.run_name}: installed assets differ within the {group} group"
            )

    if set(assets_by_group) != {"production", "diagnostic"}:
        raise QualificationError("installed asset receipts are missing a production or diagnostic group")
    production_common = {
        path: digest for path, digest in assets_by_group["production"].items()
        if path != "C/fs-compare-bench"
    }
    diagnostic_common = {
        path: digest for path, digest in assets_by_group["diagnostic"].items()
        if path != "C/fs-compare-bench"
    }
    if production_common != diagnostic_common:
        raise QualificationError(
            "production and diagnostic installed assets differ outside the pinned guest"
        )
    return assets_by_group


def check_rdb(entry: ScheduleEntry, run_dir: Path) -> None:
    try:
        PROFILE.check_rdb(entry, run_dir)
    except ValueError as error:
        raise QualificationError(str(error)) from error


def check_fs_uae_config(entry: ScheduleEntry, run_dir: Path) -> None:
    try:
        PROFILE.check_fs_uae_config(entry, run_dir)
    except ValueError as error:
        raise QualificationError(str(error)) from error

    parser = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        with (run_dir / "bench.fs-uae").open(encoding="ascii") as stream:
            parser.read_file(stream)
    except (OSError, UnicodeError, configparser.Error) as error:
        raise QualificationError(f"{entry.run_name}: invalid bench.fs-uae: {error}") from error
    config = parser["fs-uae"]
    expected_basenames = {
        "hard_drive_0": "system",
        "hard_drive_1": "bench-bfs.hdf",
        "hard_drive_2": "bench-pfs3.hdf",
    }
    for key, basename in expected_basenames.items():
        observed = PurePosixPath(config.get(key, "").strip()).name
        if observed != basename:
            raise QualificationError(
                f"{entry.run_name}: bench.fs-uae {key} must identify {basename}"
            )


def _mode_arguments(entry: ScheduleEntry) -> tuple[str, str, str]:
    if entry.family == "diag":
        return "split-write-durable", "bfs.split.tsv", "pfs3.split.tsv"
    if entry.mode == "durable-compare":
        return "durable", "bfs.durable.tsv", "pfs3.durable.tsv"
    return "", "bfs.tsv", "pfs3.tsv"


def check_startup_sequence(entry: ScheduleEntry, run_dir: Path) -> None:
    path = run_dir / "system/S/Startup-Sequence"
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(f"{entry.run_name}: could not read Startup-Sequence: {error}") from error
    stack_lines = [line.strip() for line in lines if line.strip().lower().startswith("stack")]
    expected_stack_lines = ["Stack 32768"] if entry.family == "diag" else []
    if stack_lines != expected_stack_lines:
        raise QualificationError(
            f"{entry.run_name}: Startup-Sequence stack setting differs from run mode"
        )
    command_lines = [line.strip() for line in lines if "C:fs-compare-bench" in line]
    if entry.order == "bfs-first":
        order = (("DH1:", "bfs"), ("DH2:", "pfs3"))
    else:
        order = (("DH2:", "pfs3"), ("DH1:", "bfs"))
    mode_arg, bfs_file, pfs3_file = _mode_arguments(entry)
    output_names = {"bfs": bfs_file, "pfs3": pfs3_file}
    expected = [
        f"C:fs-compare-bench {drive} {mode_arg} >SYS:Results/{output_names[filesystem]}"
        for drive, filesystem in order
    ]
    if command_lines != expected:
        raise QualificationError(
            f"{entry.run_name}: Startup-Sequence mode or filesystem order differs from schedule"
        )


def _phase_names() -> tuple[str, ...]:
    phases = tuple(SPLIT_VERIFIER._inventories()["phases"])
    if len(phases) != 23:
        raise QualificationError(f"protocol phase inventory changed: expected 23, found {len(phases)}")
    return phases


def _production_phase_rows(values: dict[str, str], filesystem: str) -> dict[str, Any]:
    phases = _phase_names()
    phase_metrics = BENCH_PARSER.phase_metrics(values)
    expected_keys = {f"{phase}_US" for phase in phases}
    if set(phase_metrics) != expected_keys:
        missing = sorted(expected_keys - set(phase_metrics))
        extra = sorted(set(phase_metrics) - expected_keys)
        raise QualificationError(
            f"{filesystem}: production phase inventory is not the fixed 23 phases "
            f"(missing={missing}, extra={extra})"
        )
    if any(value <= 0 for value in phase_metrics.values()):
        raise QualificationError(f"{filesystem}: production phase timings must be positive")
    return {
        "global": {
            key: value for key, value in values.items()
            if key not in phase_metrics
        },
        "phases": {
            phase: {"raw": {"US": phase_metrics[f"{phase}_US"]}, "tick_microseconds": {}}
            for phase in phases
        },
    }


def _verify_diagnostic_run(entry: ScheduleEntry, results_dir: Path) -> dict[str, Any]:
    try:
        SPLIT_VERIFIER.verify(results_dir, entry.mode)
        filesystems = {
            filesystem: PROFILE._phase_rows(
                SPLIT_VERIFIER._read_tsv(
                    results_dir / f"{filesystem}.split.tsv",
                    SPLIT_VERIFIER.build_schema(filesystem, entry.mode),
                ),
                filesystem,
            )
            for filesystem in ("bfs", "pfs3")
        }
    except (OSError, ValueError) as error:
        raise QualificationError(
            f"{entry.run_name}: split protocol rejected evidence: {error}"
        ) from error
    return filesystems


def _verify_production_run(entry: ScheduleEntry, run_dir: Path) -> dict[str, Any]:
    try:
        result = subprocess.run(
            ["/bin/bash", str(PRODUCTION_VERIFIER), str(run_dir), entry.mode],
            capture_output=True, text=True, check=False,
        )
    except OSError as error:
        raise QualificationError(f"{entry.run_name}: could not run strict benchmark verifier: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise QualificationError(
            f"{entry.run_name}: strict benchmark verifier failed with exit "
            f"{result.returncode}: {detail}"
        )

    suffix, header, schema = SEALED.expected_schema(entry.mode)
    results_dir = run_dir / "system/Results"
    filesystems: dict[str, Any] = {}
    for filesystem, drive in (("bfs", "DH1:"), ("pfs3", "DH2:")):
        try:
            values = BENCH_PARSER.load_tsv(
                results_dir / f"{filesystem}.{suffix}", header, schema, drive,
            )
            filesystems[filesystem] = _production_phase_rows(values, filesystem)
        except (BENCH_PARSER.QualificationError, ValueError) as error:
            raise QualificationError(f"{entry.run_name}: {error}") from error
    bfs_phases = set(filesystems["bfs"]["phases"])
    pfs3_phases = set(filesystems["pfs3"]["phases"])
    if bfs_phases != pfs3_phases:
        raise QualificationError(f"{entry.run_name}: BFS/PFS3 phase inventories differ")
    return filesystems


def verify_and_load_run(entry: ScheduleEntry, run_dir: Path) -> dict[str, Any]:
    check_runtime_receipts(entry, run_dir)
    check_rdb(entry, run_dir)
    check_fs_uae_config(entry, run_dir)
    check_startup_sequence(entry, run_dir)
    results_dir = run_dir / "system/Results"
    filesystems = (
        _verify_diagnostic_run(entry, results_dir)
        if entry.family == "diag"
        else _verify_production_run(entry, run_dir)
    )
    for filesystem in ("bfs", "pfs3"):
        if set(filesystems[filesystem]["phases"]) != set(_phase_names()):
            raise QualificationError(
                f"{entry.run_name}: {filesystem} must contain every fixed phase"
            )
    return {
        "schedule": {
            "sequence": entry.sequence,
            "family": entry.family,
            "repeat": entry.repeat,
            "order": entry.order,
            "variant": entry.variant,
            "run_name": entry.run_name,
        },
        "mode": entry.mode,
        "filesystems": filesystems,
    }


def _ratio(numerator: int, denominator: int) -> dict[str, int | float | None]:
    return {
        "numerator": numerator,
        "denominator": denominator,
        "ratio": numerator / denominator if denominator else None,
    }


def _paired_ratios(
    numerator: dict[str, Any], denominator: dict[str, Any],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for phase in _phase_names():
        numerator_raw = numerator["phases"][phase]["raw"]
        denominator_raw = denominator["phases"][phase]["raw"]
        if numerator_raw.keys() != denominator_raw.keys():
            raise QualificationError(f"{phase}: paired raw metric inventories differ")
        ratios = {}
        for metric, numerator_value in numerator_raw.items():
            denominator_value = denominator_raw[metric]
            if type(numerator_value) is not int or type(denominator_value) is not int:
                continue
            ratios[metric] = _ratio(numerator_value, denominator_value)
        result[phase] = ratios
    return result


def make_pair_comparison(
    numerator_entry: ScheduleEntry,
    denominator_entry: ScheduleEntry,
    verified: dict[str, dict[str, Any]],
    ratio_label: str,
) -> dict[str, Any]:
    numerator = verified[numerator_entry.run_name]["filesystems"]
    denominator = verified[denominator_entry.run_name]["filesystems"]
    return {
        "ratio_label": ratio_label,
        "family": numerator_entry.family,
        "repeat": numerator_entry.repeat,
        "order": numerator_entry.order,
        "numerator_run": numerator_entry.run_name,
        "denominator_run": denominator_entry.run_name,
        "bfs_ratios": _paired_ratios(numerator["bfs"], denominator["bfs"]),
        "pfs3_control_ratios": _paired_ratios(numerator["pfs3"], denominator["pfs3"]),
    }


def _make_comparisons(
    schedule: list[ScheduleEntry], verified: dict[str, dict[str, Any]],
) -> dict[str, list[dict[str, Any]]]:
    by_key = {
        (entry.family, entry.repeat, entry.variant): entry
        for entry in schedule
    }
    diagnostic = [make_pair_comparison(
        by_key[("diag", 1, "candidate")], by_key[("diag", 1, "base")],
        verified, "diagnostic candidate/base",
    )]
    production = []
    for family in ("normal", "durable"):
        for repeat in (1, 2):
            production.append(make_pair_comparison(
                by_key[(family, repeat, "candidate")], by_key[(family, repeat, "base")],
                verified, "production candidate/base",
            ))
    controls = []
    for family in ("aa-normal", "aa-durable"):
        controls.append(make_pair_comparison(
            by_key[(family, 1, "baseb")], by_key[(family, 1, "basea")],
            verified, "same-handler baseb/basea control",
        ))
    return {
        "diagnostic_candidate_over_base": diagnostic,
        "production_candidate_over_base": production,
        "same_handler_baseb_over_basea_controls": controls,
    }


def summarize(results_root: Path) -> dict[str, Any]:
    schedule = validate_schedule(results_root)
    check_run_inventory(results_root, schedule)
    assets_by_group = check_installed_assets(schedule, results_root)

    verified: dict[str, dict[str, Any]] = {}
    for entry in schedule:
        verified[entry.run_name] = verify_and_load_run(
            entry, results_root / entry.run_name,
        )

    return {
        "scope": (
            "Fixed 14-run resident-root floor pilot. Every scheduled run is included; "
            "all 23 phases and raw protocol fields are retained. Ratios are paired only "
            "within the scheduled repeat/order. No pooling, outlier exclusion, exclusive-time "
            "calculation, or performance-clearance judgment is made."
        ),
        "verified_schedule_count": len(schedule),
        "verified_phase_count": len(_phase_names()),
        "verified_installed_asset_count_per_group": EXPECTED_ASSET_COUNT,
        "installed_assets_identical_within_group": True,
        "asset_group_guest_sha256": {
            group: assets["C/fs-compare-bench"]
            for group, assets in assets_by_group.items()
        },
        "runs": verified,
        "comparisons": _make_comparisons(schedule, verified),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify and summarize the fixed 14-run resident-root floor pilot."
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
