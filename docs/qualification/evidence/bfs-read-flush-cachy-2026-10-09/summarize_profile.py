#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the fixed 12-run Cachy split diagnostic."""

from __future__ import annotations

import argparse
import configparser
from dataclasses import dataclass
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import statistics
import sys
from typing import Any


BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[3]
VERIFIER_PATH = ROOT / "tools/verify-split-bench.py"
EXPECTED_BUFFERS = 30
EXPECTED_ASSET_COUNT = 67
M5_HANDLER_SHA256 = "4818ea526841a3a253cb437f925f949fcd14b9a5d780169b93a31bd8958c9277"
SPARSE_HANDLER_SHA256 = "33a77819f0f7a5e6a3b4dbe9fd4d2ae2367f51f03ce910bf5be0957ccf4f7141"
GUEST_SHA256 = "81c98af386792325a6e85f5debc6450895a48c76910816edae8716e1053d6bb4"
PFS3_SHA256 = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"
RUNTIME_PATHS = (
    "system/L/bfshandler",
    "system/L/pfs3aio",
    "system/C/fs-compare-bench",
)
IO_CRC_CALL_COUNTERS = (
    "BIO_READS",
    "BIO_WRITES",
    "BIO_UPDATES",
    "DATA_READS",
    "DATA_WRITES",
    "NODE_WRITES",
    "TXN_COMMITS",
    "EXTENT_MAPS",
    "NODE_CRC_READ_CALLS",
    "NODE_CRC_WRITE_CALLS",
)


def _load_verifier():
    spec = importlib.util.spec_from_file_location("bfs_split_bench_verifier", VERIFIER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load split result verifier: {VERIFIER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


VERIFIER = _load_verifier()


class QualificationError(ValueError):
    """Raised when any scheduled run or retained evidence is incomplete."""


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
        return "split-durable-compare" if self.family in {"durable", "aa-durable"} else "split-compare"

    @property
    def handler(self) -> str:
        return "sparse" if self.variant == "sparse" else "m5"

    def fields(self) -> tuple[str, ...]:
        return (
            str(self.sequence), self.family, str(self.repeat), self.order,
            self.variant, self.run_name,
        )


def expected_schedule() -> list[ScheduleEntry]:
    entries: list[ScheduleEntry] = []

    def add(family: str, repeat: int, order: str, variant: str) -> None:
        entries.append(ScheduleEntry(
            len(entries) + 1, family, repeat, order, variant,
            f"split-{family}-{variant}-{repeat}-{order}",
        ))

    for repeat in (1, 2):
        if repeat == 1:
            order = "bfs-first"
            variants = ("m5", "sparse")
            families = ("normal", "durable")
        else:
            order = "pfs3-first"
            variants = ("sparse", "m5")
            families = ("durable", "normal")
        for family in families:
            for variant in variants:
                add(family, repeat, order, variant)

    add("aa-normal", 1, "bfs-first", "m5a")
    add("aa-normal", 1, "bfs-first", "m5b")
    add("aa-durable", 1, "pfs3-first", "m5a")
    add("aa-durable", 1, "pfs3-first", "m5b")

    if len(entries) != 12 or len({entry.run_name for entry in entries}) != 12:
        raise RuntimeError("internal error: fixed Cachy schedule must contain 12 unique runs")
    return entries


EXPECTED_SCHEDULE = expected_schedule()
SCHEDULE_HEADER = ("sequence", "family", "repeat", "order", "variant", "run_name")


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

    expected_lines = ["\t".join(SCHEDULE_HEADER)]
    expected_lines.extend("\t".join(entry.fields()) for entry in EXPECTED_SCHEDULE)
    if lines != expected_lines:
        for line_number, (observed, expected) in enumerate(
            zip(lines, expected_lines), start=1
        ):
            if observed != expected:
                raise QualificationError(
                    f"schedule.tsv line {line_number} differs from the fixed 12-run schedule"
                )
        raise QualificationError(
            f"schedule.tsv has {len(lines)} lines; expected {len(expected_lines)}"
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
    handler_digest = SPARSE_HANDLER_SHA256 if entry.handler == "sparse" else M5_HANDLER_SHA256
    return [
        (handler_digest, RUNTIME_PATHS[0]),
        (PFS3_SHA256, RUNTIME_PATHS[1]),
        (GUEST_SHA256, RUNTIME_PATHS[2]),
    ]


def read_receipt(path: Path) -> list[tuple[str, str]]:
    try:
        content = path.read_bytes()
    except OSError as error:
        raise QualificationError(f"cannot read {path.name}: {error}") from error
    if not content.endswith(b"\n") or b"\r" in content:
        raise QualificationError(f"{path.name} must be LF-terminated without CR bytes")
    try:
        lines = content.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise QualificationError(f"{path.name} must be ASCII") from error
    entries: list[tuple[str, str]] = []
    seen: set[str] = set()
    for line_number, line in enumerate(lines, 1):
        fields = line.split("  ", 1)
        if (len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0])
                or not fields[1] or fields[1] in seen):
            raise QualificationError(f"{path.name}: malformed or duplicate row {line_number}")
        seen.add(fields[1])
        entries.append((fields[0], fields[1]))
    if not entries:
        raise QualificationError(f"{path.name} is empty")
    return entries


def check_runtime_receipts(entry: ScheduleEntry, run_dir: Path) -> None:
    expected = expected_runtime_receipt(entry)
    for receipt_name in ("runtime-inputs.sha256", "runtime-inputs.post.sha256"):
        observed = read_receipt(run_dir / receipt_name)
        if observed != expected:
            raise QualificationError(
                f"{entry.run_name}: {receipt_name} does not match pinned runtime inputs"
            )


def check_installed_assets(
    schedule: list[ScheduleEntry], results_root: Path,
) -> dict[str, str]:
    shared: dict[str, str] | None = None
    for entry in schedule:
        run_dir = results_root / entry.run_name
        path = run_dir / "installed-assets.sha256"
        receipt = read_receipt(path)
        if len(receipt) != EXPECTED_ASSET_COUNT:
            raise QualificationError(
                f"{entry.run_name}: expected exactly {EXPECTED_ASSET_COUNT} non-handler assets, "
                f"found {len(receipt)}"
        )
        paths = [asset_path for _, asset_path in receipt]
        assets = {asset_path: digest for digest, asset_path in receipt}
        if len(assets) != EXPECTED_ASSET_COUNT:
            raise QualificationError(f"{entry.run_name}: duplicate installed asset path")
        for asset_path in assets:
            pure = PurePosixPath(asset_path)
            if (pure.is_absolute() or ".." in pure.parts or
                    pure.parts[0] not in {"C", "L", "Libs"}):
                raise QualificationError(
                    f"{entry.run_name}: invalid installed asset path {asset_path!r}"
                )
        if "L/bfshandler" in assets:
            raise QualificationError(f"{entry.run_name}: handler must be excluded from asset receipt")
        if assets.get("C/fs-compare-bench") != GUEST_SHA256:
            raise QualificationError(f"{entry.run_name}: installed guest digest mismatch")
        if assets.get("L/pfs3aio") != PFS3_SHA256:
            raise QualificationError(f"{entry.run_name}: installed PFS3 digest mismatch")
        if shared is None:
            shared = assets
        elif assets != shared:
            raise QualificationError(
                f"{entry.run_name}: non-handler installed assets differ from the other runs"
            )
    if shared is None:
        raise QualificationError("no installed asset receipts were verified")
    return shared


def check_rdb(entry: ScheduleEntry, run_dir: Path) -> None:
    path = run_dir / "bfs-rdb.json"
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        partitions = document["rdb"]["partitions"]
        if not isinstance(partitions, list) or len(partitions) != 1:
            count = len(partitions) if isinstance(partitions, list) else "non-list"
            raise QualificationError(
                f"{entry.run_name}: expected exactly one BFS RDB partition, found {count}"
            )
        dos_env = partitions[0]["dos_env"]
        buffers = dos_env["num_buffer"]
    except QualificationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise QualificationError(f"{entry.run_name}: invalid BFS RDB JSON: {error}") from error
    if type(buffers) is not int or buffers != EXPECTED_BUFFERS:
        raise QualificationError(
            f"{entry.run_name}: BFS RDB num_buffer must be {EXPECTED_BUFFERS}, found {buffers!r}"
        )


def check_fs_uae_config(entry: ScheduleEntry, run_dir: Path) -> None:
    path = run_dir / "bench.fs-uae"
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        with path.open(encoding="ascii") as stream:
            parser.read_file(stream)
    except (OSError, UnicodeError, configparser.Error) as error:
        raise QualificationError(f"{entry.run_name}: invalid bench.fs-uae: {error}") from error
    if not parser.has_section("fs-uae"):
        raise QualificationError(f"{entry.run_name}: bench.fs-uae has no [fs-uae] section")
    config = parser["fs-uae"]
    required = {
        "amiga_model": "A1200",
        "cpu": "68040",
        "uae_cpu_speed": "max",
        "chip_memory": "2048",
        "fast_memory": "8192",
    }
    for key, expected in required.items():
        if config.get(key, "").strip() != expected:
            raise QualificationError(
                f"{entry.run_name}: bench.fs-uae {key} must be {expected}"
            )
    path_keys = ("kickstart_file", "hard_drive_0", "hard_drive_1", "hard_drive_2")
    basenames = []
    for key in path_keys:
        value = config.get(key, "").strip()
        if not value:
            raise QualificationError(f"{entry.run_name}: bench.fs-uae is missing {key}")
        basename = PurePosixPath(value).name
        if not basename:
            raise QualificationError(f"{entry.run_name}: bench.fs-uae {key} has no basename")
        basenames.append(basename)
    if len(set(basenames)) != len(path_keys):
        raise QualificationError(
            f"{entry.run_name}: bench.fs-uae ROM and disk path basenames must be unique"
        )


def check_startup_sequence(entry: ScheduleEntry, run_dir: Path) -> None:
    path = run_dir / "system/S/Startup-Sequence"
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(
            f"{entry.run_name}: could not read Startup-Sequence: {error}"
        ) from error
    stack_lines = [line.strip() for line in lines if line.strip().lower().startswith("stack")]
    if stack_lines != ["Stack 32768"]:
        raise QualificationError(
            f"{entry.run_name}: Startup-Sequence must contain exactly one Stack 32768"
        )
    command_lines = [line.strip() for line in lines if "C:fs-compare-bench" in line]
    first = ("DH1:", "bfs") if entry.order == "bfs-first" else ("DH2:", "pfs3")
    second = ("DH2:", "pfs3") if entry.order == "bfs-first" else ("DH1:", "bfs")
    args = "split-durable" if entry.mode == "split-durable-compare" else "split"
    expected = [
        f"C:fs-compare-bench {drive} {args} >SYS:Results/{filesystem}.split.tsv"
        for drive, filesystem in (first, second)
    ]
    if command_lines != expected:
        raise QualificationError(
            f"{entry.run_name}: Startup-Sequence commands or filesystem order do not match schedule"
        )


def _phase_rows(values: dict[str, int | str], filesystem: str) -> dict[str, Any]:
    inventories = VERIFIER._inventories()
    phases = inventories["phases"]
    if len(phases) != 23:
        raise QualificationError(f"protocol phase inventory changed: expected 23, found {len(phases)}")
    longest_first = sorted(phases, key=len, reverse=True)
    rows_by_phase: dict[str, dict[str, int | str]] = {phase: {} for phase in phases}
    globals_: dict[str, int | str] = {}
    for key, value in values.items():
        phase = next((candidate for candidate in longest_first
                      if key.startswith(candidate + "_")), None)
        if phase is None:
            globals_[key] = value
        else:
            rows_by_phase[phase][key[len(phase) + 1:]] = value
    if any(not rows for rows in rows_by_phase.values()):
        missing = [phase for phase, rows in rows_by_phase.items() if not rows]
        raise QualificationError(f"{filesystem}: missing phase inventory: {', '.join(missing)}")

    clock_hz = values["CLOCK_HZ"]
    assert isinstance(clock_hz, int)
    phase_summaries = {}
    for phase in phases:
        raw = rows_by_phase[phase]
        tick_microseconds = (
            {key: value * 1_000_000 / clock_hz for key, value in raw.items()
             if key.endswith("_TICKS") and isinstance(value, int)}
            if clock_hz else {}
        )
        phase_summaries[phase] = {
            "raw": raw,
            "tick_microseconds": tick_microseconds,
        }
    return {
        "clock_hz": clock_hz,
        "global": globals_,
        "phases": phase_summaries,
    }


def verify_and_load_run(entry: ScheduleEntry, run_dir: Path) -> dict[str, Any]:
    check_runtime_receipts(entry, run_dir)
    check_rdb(entry, run_dir)
    check_fs_uae_config(entry, run_dir)
    check_startup_sequence(entry, run_dir)

    results_dir = run_dir / "system/Results"
    try:
        VERIFIER.verify(results_dir, entry.mode)
        filesystems = {}
        for filesystem in ("bfs", "pfs3"):
            values = VERIFIER._read_tsv(
                results_dir / f"{filesystem}.split.tsv",
                VERIFIER.build_schema(filesystem, entry.mode),
            )
            filesystems[filesystem] = _phase_rows(values, filesystem)
    except (OSError, ValueError) as error:
        raise QualificationError(f"{entry.run_name}: split protocol rejected evidence: {error}") from error

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


def _ratio(numerator: int, denominator: int, numerator_name: str,
           denominator_name: str) -> dict[str, int | float | None]:
    return {
        f"numerator_{numerator_name}": numerator,
        f"denominator_{denominator_name}": denominator,
        "ratio": numerator / denominator if denominator else None,
    }


def _paired_phase_ratios(
    numerator: dict[str, Any], denominator: dict[str, Any],
    numerator_name: str, denominator_name: str,
) -> dict[str, Any]:
    inventories = VERIFIER._inventories()
    result: dict[str, Any] = {}
    for phase in inventories["phases"]:
        numerator_raw = numerator["phases"][phase]["raw"]
        denominator_raw = denominator["phases"][phase]["raw"]
        guest = {}
        for operation in inventories["ops"]:
            guest[operation] = {
                "calls": _ratio(
                    numerator_raw[f"GUEST_{operation}_CALLS"],
                    denominator_raw[f"GUEST_{operation}_CALLS"],
                    numerator_name, denominator_name,
                ),
                "us": _ratio(
                    numerator_raw[f"GUEST_{operation}_US"],
                    denominator_raw[f"GUEST_{operation}_US"],
                    numerator_name, denominator_name,
                ),
            }
        result[phase] = {
            "phase_us": _ratio(
                numerator_raw["US"], denominator_raw["US"],
                numerator_name, denominator_name,
            ),
            "work_us": _ratio(
                numerator_raw["WORK_US"], denominator_raw["WORK_US"],
                numerator_name, denominator_name,
            ),
            "volume_flush_us": _ratio(
                numerator_raw["VOLUME_FLUSH_US"], denominator_raw["VOLUME_FLUSH_US"],
                numerator_name, denominator_name,
            ),
            "guest_operations": guest,
        }
    return result


def _io_crc_differences(
    numerator: dict[str, Any], denominator: dict[str, Any],
    numerator_name: str, denominator_name: str,
) -> dict[str, Any]:
    differences = {}
    for phase in VERIFIER._inventories()["phases"]:
        numerator_raw = numerator["phases"][phase]["raw"]
        denominator_raw = denominator["phases"][phase]["raw"]
        differences[phase] = {}
        for stage in ("WORK", "VOLUME_FLUSH"):
            stage_differences = {}
            for counter in IO_CRC_CALL_COUNTERS:
                key = f"{stage}_{counter}"
                denominator_value = denominator_raw[key]
                numerator_value = numerator_raw[key]
                stage_differences[counter] = {
                    denominator_name: denominator_value,
                    numerator_name: numerator_value,
                    f"{numerator_name}_minus_{denominator_name}": (
                        numerator_value - denominator_value
                    ),
                }
            differences[phase][stage] = stage_differences
    return differences


def make_pair_comparison(
    numerator_entry: ScheduleEntry,
    denominator_entry: ScheduleEntry,
    verified: dict[str, dict[str, Any]],
    numerator_name: str,
    denominator_name: str,
    ratio_label: str,
) -> dict[str, Any]:
    numerator = verified[numerator_entry.run_name]["filesystems"]
    denominator = verified[denominator_entry.run_name]["filesystems"]
    numerator_bfs = numerator["bfs"]
    denominator_bfs = denominator["bfs"]
    numerator_pfs3 = numerator["pfs3"]
    denominator_pfs3 = denominator["pfs3"]
    return {
        "ratio_label": ratio_label,
        "numerator_run": numerator_entry.run_name,
        "denominator_run": denominator_entry.run_name,
        "bfs_ratios": _paired_phase_ratios(
            numerator_bfs, denominator_bfs, numerator_name, denominator_name,
        ),
        "pfs3_control_ratios": _paired_phase_ratios(
            numerator_pfs3, denominator_pfs3, numerator_name, denominator_name,
        ),
        "bfs_io_crc_call_counter_differences": _io_crc_differences(
            numerator_bfs, denominator_bfs, numerator_name, denominator_name,
        ),
    }


def _walk_ratio_leaves(value: Any, prefix: tuple[str, ...] = ()):
    if isinstance(value, dict) and "ratio" in value:
        yield prefix, value["ratio"]
        return
    if isinstance(value, dict):
        for key, child in value.items():
            yield from _walk_ratio_leaves(child, prefix + (key,))


def diagnostic_pair_medians(pairs: list[dict[str, Any]]) -> dict[str, Any]:
    if len(pairs) != 2:
        raise QualificationError(f"expected exactly two sparse/M5 pairs per mode, found {len(pairs)}")
    trees = [
        {
            "bfs_ratios": pair["bfs_ratios"],
            "pfs3_control_ratios": pair["pfs3_control_ratios"],
        }
        for pair in pairs
    ]
    first = dict(_walk_ratio_leaves(trees[0]))
    second = dict(_walk_ratio_leaves(trees[1]))
    if first.keys() != second.keys():
        raise QualificationError("paired ratio inventories differ across repeats")
    metrics = []
    for path in sorted(first):
        ratios = [first[path], second[path]]
        median = statistics.median(ratios) if all(value is not None for value in ratios) else None
        metrics.append({
            "metric_path": ".".join(path),
            "pair_ratios": ratios,
            "pair_count": 2,
            "median": median,
            "median_scope": "diagnostic only; not statistical clearance",
        })
    return {
        "pair_count": 2,
        "interpretation": "diagnostic only; not statistical clearance",
        "metrics": metrics,
    }


def summarize(results_root: Path) -> dict[str, Any]:
    schedule = validate_schedule(results_root)
    check_run_inventory(results_root, schedule)
    shared_assets = check_installed_assets(schedule, results_root)

    verified: dict[str, dict[str, Any]] = {}
    for entry in schedule:
        run_dir = results_root / entry.run_name
        verified[entry.run_name] = verify_and_load_run(entry, run_dir)

    by_key = {(entry.family, entry.repeat, entry.variant): entry for entry in schedule}
    sparse_m5_pairs = []
    for family in ("normal", "durable"):
        for repeat in (1, 2):
            m5 = by_key[(family, repeat, "m5")]
            sparse = by_key[(family, repeat, "sparse")]
            comparison = make_pair_comparison(
                sparse, m5, verified, "sparse", "m5", "sparse/M5",
            )
            comparison.update({"family": family, "repeat": repeat, "order": m5.order})
            sparse_m5_pairs.append(comparison)

    same_handler_pairs = []
    for family in ("aa-normal", "aa-durable"):
        m5a = by_key[(family, 1, "m5a")]
        m5b = by_key[(family, 1, "m5b")]
        comparison = make_pair_comparison(
            m5b, m5a, verified, "m5b", "m5a", "M5-B/M5-A",
        )
        comparison.update({"family": family, "repeat": 1, "order": m5a.order})
        same_handler_pairs.append(comparison)

    medians = {
        family: diagnostic_pair_medians(
            [pair for pair in sparse_m5_pairs if pair["family"] == family]
        )
        for family in ("normal", "durable")
    }
    return {
        "scope": (
            "Fixed 12-run Cachy split diagnostic. Probe intervals are inclusive; "
            "nested timings are not summed and no exclusive-time claim is made. "
            "Two-pair medians are diagnostic only, not statistical clearance or production speedup claims."
        ),
        "verified_schedule_count": len(schedule),
        "verified_non_handler_asset_count": len(shared_assets),
        "non_handler_assets_identical_across_runs": True,
        "non_handler_asset_receipt_includes": {
            "guest_fs_compare_bench_sha256": GUEST_SHA256,
            "pfs3aio_sha256": PFS3_SHA256,
        },
        "runs": verified,
        "comparisons": {
            "sparse_over_m5": sparse_m5_pairs,
            "same_m5_handler_m5b_over_m5a": same_handler_pairs,
            "two_pair_medians_by_mode": medians,
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify all 12 fixed Cachy split diagnostic runs and emit JSON."
    )
    parser.add_argument(
        "results_root", nargs="?", type=Path, default=BUNDLE / "results",
        help="run-profile.sh results directory (default: this bundle's results/)",
    )
    args = parser.parse_args(argv)
    try:
        summary = summarize(args.results_root)
    except (OSError, QualificationError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
