#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly verify and summarize the fixed sparse CRC append confirmation."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import importlib.util
import json
from pathlib import Path
import re
import statistics
import sys
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[3]
PREVIOUS_SUMMARY = (
    SCRIPT_DIR.parent / "bfs-sparse-key-crc-performance-2026-10-09" / "summarize.py"
)
SCHEDULE_HEADER = ("sequence", "family", "repeat", "order", "variant", "run_name")
EXPECTED_BUFFER_COUNT = 30
EXPECTED_PHASES = (
    "SMALL_CREATE_40_US",
    "LOOKUP_400_US",
    "SMALL_READ_40_US",
    "LIST_EXNEXT_400_US",
    "LIST_EXALL_400_US",
    "SEQ_WRITE_8M_US",
    "SEQ_READ_8M_US",
    "SMALL_DELETE_40_US",
    "APPEND_4K_1M_US",
    "APPEND_1K_256K_US",
    "APPEND_READ_1280K_US",
    "LIST_EXNEXT_40_ENTRIES_FIRST_PASS_US",
    "LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXALL_40_ENTRIES_FIRST_PASS_US",
    "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXNEXT_400_ENTRIES_FIRST_PASS_US",
    "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXALL_400_ENTRIES_FIRST_PASS_US",
    "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXNEXT_1000_ENTRIES_FIRST_PASS_US",
    "LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXALL_1000_ENTRIES_FIRST_PASS_US",
    "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL_US",
)


def import_previous_summary():
    """Reuse the immutable production parser by its fixed sibling path."""
    spec = importlib.util.spec_from_file_location(
        "bfs_sparse_key_crc_performance_summary", PREVIOUS_SUMMARY
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load shared parser: {PREVIOUS_SUMMARY}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


PARSER = import_previous_summary()
QualificationError = PARSER.QualificationError


@dataclass(frozen=True)
class ScheduleEntry:
    sequence: int
    family: str
    repeat: int
    order: str
    variant: str
    run_name: str
    mode_family: str
    parser_variant: str
    first_handler: str

    def tsv_fields(self) -> tuple[str, ...]:
        return (
            str(self.sequence), self.family, str(self.repeat), self.order,
            self.variant, self.run_name,
        )


@dataclass(frozen=True)
class VerifiedConfirmationRun:
    schedule: ScheduleEntry
    verified: Any


def expected_schedule() -> list[ScheduleEntry]:
    """Expand run-confirmation.sh's fixed loops without relying on directory names."""
    entries: list[ScheduleEntry] = []

    def append(family: str, repeat: int, order: str, variant: str,
               first_handler: str) -> None:
        mode_family = "normal" if family.endswith("normal") else "durable"
        parser_variant = "m5" if variant in {"m5", "m5a", "m5b"} else "sparse"
        entries.append(ScheduleEntry(
            sequence=len(entries) + 1,
            family=family,
            repeat=repeat,
            order=order,
            variant=variant,
            run_name=f"confirm-{family}-{variant}-{repeat}-{order}",
            mode_family=mode_family,
            parser_variant=parser_variant,
            first_handler=first_handler,
        ))

    for repeat in range(1, 9):
        order = "bfs-first" if repeat % 2 else "pfs3-first"
        families = ("normal", "durable") if repeat % 2 else ("durable", "normal")
        variants = ("m5", "sparse") if (repeat - 1) % 4 < 2 else ("sparse", "m5")
        for family in families:
            for variant in variants:
                append(family, repeat, order, variant, variants[0])

        if repeat in {4, 8}:
            aa_repeat = repeat // 4
            aa_order = "bfs-first" if aa_repeat == 1 else "pfs3-first"
            for family in ("aa-normal", "aa-durable"):
                append(family, aa_repeat, aa_order, "m5a", "m5a")
                append(family, aa_repeat, aa_order, "m5b", "m5a")

    if len(entries) != 40 or len({entry.run_name for entry in entries}) != 40:
        raise RuntimeError("internal error: confirmation schedule must contain 40 unique runs")
    return entries


EXPECTED_SCHEDULE = expected_schedule()
PRIMARY_FAMILIES = ("normal", "durable")
AA_FAMILIES = ("aa-normal", "aa-durable")
MODE_BY_FAMILY = {"normal": "compare", "durable": "durable-compare"}


def validate_schedule_file(root: Path) -> list[ScheduleEntry]:
    path = root / "schedule.tsv"
    if not path.is_file():
        raise QualificationError(f"missing fixed schedule file: {path}")
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(f"could not read schedule.tsv: {error}") from error
    expected_header = "\t".join(SCHEDULE_HEADER)
    if not lines or lines[0] != expected_header:
        raise QualificationError(
            f"schedule.tsv: expected exact header {expected_header!r}"
        )

    observed: list[tuple[str, ...]] = []
    malformed: list[str] = []
    for line_number, line in enumerate(lines[1:], 2):
        fields = tuple(line.split("\t"))
        if len(fields) != len(SCHEDULE_HEADER) or any(not field for field in fields):
            malformed.append(str(line_number))
            continue
        observed.append(fields)
    if malformed:
        raise QualificationError(
            "schedule.tsv: malformed rows at line(s) " + ", ".join(malformed)
        )

    expected_fields = [entry.tsv_fields() for entry in EXPECTED_SCHEDULE]
    expected_names = [row[-1] for row in expected_fields]
    observed_names = [row[-1] for row in observed]
    duplicate_names = sorted(
        name for name, count in Counter(observed_names).items() if count > 1
    )
    missing = sorted(set(expected_names) - set(observed_names))
    unexpected = sorted(set(observed_names) - set(expected_names))
    problems = []
    if duplicate_names:
        problems.append("duplicate scheduled run names: " + ", ".join(duplicate_names))
    if missing:
        problems.append("missing schedule rows: " + ", ".join(missing))
    if unexpected:
        problems.append("unexpected schedule rows: " + ", ".join(unexpected))

    expected_by_name = {row[-1]: row for row in expected_fields}
    observed_by_name = {row[-1]: row for row in observed}
    altered = [
        name for name in expected_names
        if name in observed_by_name and observed_by_name[name] != expected_by_name[name]
    ]
    if altered:
        problems.append("schedule fields differ from fixed script for: " + ", ".join(altered))
    if not problems and observed != expected_fields:
        problems.append("schedule.tsv row order differs from the fixed 40-run script order")
    if problems:
        raise QualificationError("\n".join(problems))
    return EXPECTED_SCHEDULE


def check_run_directories(root: Path, schedule: list[ScheduleEntry]) -> None:
    expected_names = [entry.run_name for entry in schedule]
    expected_set = set(expected_names)
    missing = sorted(name for name in expected_set if not (root / name).is_dir())
    unexpected = sorted(
        path.name for path in root.iterdir()
        if path.is_dir() and path.name.startswith("confirm") and path.name not in expected_set
    ) if root.is_dir() else []
    duplicate_names = len(expected_names) - len(expected_set)
    problems = []
    if missing:
        problems.append("missing expected run directories: " + ", ".join(missing))
    if unexpected:
        problems.append("unexpected confirm* directories: " + ", ".join(unexpected))
    if duplicate_names:
        problems.append("internal error: expected directory inventory contains duplicate names")
    if problems:
        raise QualificationError("\n".join(problems))


INPUT_IDENTITIES = (
    ("79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4", "build/inputs/m5/bfshandler"),
    ("486e3bbbf5c09a2b7d5cae0e3afd02a3a0a57d76a892d0aca6dfec31dbdb21f4", "build/inputs/sparse/bfshandler"),
    ("eb641e3210b4a158594a9c235013b5ea32ef48fe028e7d54a916a3e83835b56d", "build/inputs/m5/fs-compare-bench"),
    ("bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7", "pfs3aio"),
    ("68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c", "kick.a1200.47.102.rom"),
    ("f785d4936d8226f22b36e0efc5da21c977339d5400b28f3eab161f66f8893d55", "bfs-formatter"),
)


def check_top_level_input_identities(root: Path) -> None:
    identity_path = root / "input-identities.sha256"
    check_path = root / "input-check.log"
    for path in (identity_path, check_path):
        if not path.is_file():
            raise QualificationError(f"missing top-level input identity evidence: {path.name}")
    try:
        identity_lines = identity_path.read_text(encoding="ascii").splitlines()
        check_lines = check_path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(f"could not read top-level input identity evidence: {error}") from error

    expected_digests = {suffix: digest for digest, suffix in INPUT_IDENTITIES}
    observed: dict[str, tuple[str, str]] = {}
    for line_number, line in enumerate(identity_lines, 1):
        fields = line.split("  ", 1)
        if (len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0])
                or not Path(fields[1]).is_absolute()):
            raise QualificationError(
                f"input-identities.sha256: malformed row {line_number}"
            )
        matches = [suffix for suffix in expected_digests if fields[1].endswith(suffix)]
        if len(matches) != 1 or matches[0] in observed:
            raise QualificationError(
                f"input-identities.sha256: unexpected or duplicate path {fields[1]!r}"
            )
        observed[matches[0]] = (fields[0], fields[1])
    if set(observed) != set(expected_digests):
        missing = sorted(set(expected_digests) - set(observed))
        raise QualificationError(
            "input-identities.sha256: missing expected source(s): " + ", ".join(missing)
        )
    for suffix, expected_digest in expected_digests.items():
        digest, _ = observed[suffix]
        if digest != expected_digest:
            raise QualificationError(
                f"input-identities.sha256: identity mismatch for {suffix}"
            )
    expected_check_lines = [
        f"{observed[suffix][1]}: OK" for _, suffix in INPUT_IDENTITIES
    ]
    if check_lines != expected_check_lines:
        raise QualificationError(
            "input-check.log: expected six successful checks in fixed input order"
        )


FAILURE_MARKER = re.compile(r"\b(?:FAIL(?:ED|URE)?|ERROR)\b", re.IGNORECASE)


def check_run_logs(run_dir: Path, run_name: str) -> None:
    for name in ("build.log", "run.log"):
        path = run_dir / name
        if not path.is_file():
            raise QualificationError(f"{run_name}: missing {name}")
        try:
            content = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise QualificationError(f"{run_name}: could not read {name}: {error}") from error
        if not content.strip():
            raise QualificationError(f"{run_name}: empty {name}")
        if FAILURE_MARKER.search(content):
            raise QualificationError(f"{run_name}: failure marker in {name}")


STARTUP_COMMAND = re.compile(
    r"^\s*C:fs-compare-bench\s+(DH[12]):[ \t]*(.*?)>[ \t]*"
    r"SYS:Results/(bfs|pfs3)\.(tsv|durable\.tsv)\s*$",
    re.IGNORECASE,
)


def check_startup_sequence(run: ScheduleEntry, run_dir: Path) -> None:
    path = run_dir / "system/S/Startup-Sequence"
    if not path.is_file():
        raise QualificationError(f"{run.run_name}: missing retained Startup-Sequence")
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise QualificationError(
            f"{run.run_name}: could not read retained Startup-Sequence: {error}"
        ) from error
    command_lines = [line for line in lines if "C:fs-compare-bench" in line]
    if len(command_lines) != 2:
        raise QualificationError(
            f"{run.run_name}: expected exactly two fs-compare-bench commands in Startup-Sequence"
        )
    observed = []
    for line in command_lines:
        match = STARTUP_COMMAND.fullmatch(line)
        if match is None:
            raise QualificationError(
                f"{run.run_name}: malformed fs-compare-bench command in Startup-Sequence"
            )
        drive, arguments, filesystem, suffix = match.groups()
        expected_suffix = "durable.tsv" if run.mode_family == "durable" else "tsv"
        expected_arguments = "durable" if run.mode_family == "durable" else ""
        if suffix.lower() != expected_suffix or arguments.strip().lower() != expected_arguments:
            raise QualificationError(
                f"{run.run_name}: Startup-Sequence command does not match {run.mode_family} mode"
            )
        observed.append((drive.upper(), filesystem.lower()))
    expected = (
        [("DH1", "bfs"), ("DH2", "pfs3")]
        if run.order == "bfs-first"
        else [("DH2", "pfs3"), ("DH1", "bfs")]
    )
    if observed != expected:
        raise QualificationError(
            f"{run.run_name}: Startup-Sequence filesystem order disagrees with declared {run.order}"
        )


def parser_run_for(schedule: ScheduleEntry):
    mode = MODE_BY_FAMILY[schedule.mode_family]
    return PARSER.make_run(
        schedule.run_name,
        mode,
        schedule.mode_family,
        schedule.parser_variant,
        schedule.repeat,
        schedule.order,
    )


def phase_metrics(values: dict[str, str], run_name: str, filesystem: str) -> dict[str, int]:
    try:
        metrics = PARSER.phase_map(values, run_name)
    except QualificationError as error:
        raise QualificationError(f"{run_name}/{filesystem}: {error}") from error
    if set(metrics) != set(EXPECTED_PHASES):
        missing = sorted(set(EXPECTED_PHASES) - set(metrics))
        unexpected = sorted(set(metrics) - set(EXPECTED_PHASES))
        raise QualificationError(
            f"{run_name}/{filesystem}: expected all 23 fixed phases; "
            f"missing={missing}, unexpected={unexpected}"
        )
    if any(value <= 0 for value in metrics.values()):
        raise QualificationError(f"{run_name}/{filesystem}: phase timings must be positive")
    return metrics


def verify_inventory(root: Path) -> list[VerifiedConfirmationRun]:
    schedule = validate_schedule_file(root)
    check_run_directories(root, schedule)
    check_top_level_input_identities(root)
    if not PARSER.VERIFIER.is_file():
        raise QualificationError(f"repository verifier not found: {PARSER.VERIFIER}")

    verified: list[VerifiedConfirmationRun] = []
    failures: list[str] = []
    for entry in schedule:
        run_dir = root / entry.run_name
        entry_errors = []
        for check in (
            lambda: check_run_logs(run_dir, entry.run_name),
            lambda: check_startup_sequence(entry, run_dir),
        ):
            try:
                check()
            except QualificationError as error:
                entry_errors.append(str(error))
        try:
            parsed = PARSER.verify_run(parser_run_for(entry), root)
        except QualificationError as error:
            entry_errors.append(str(error))
            parsed = None
        if parsed is not None:
            try:
                phase_metrics(parsed.bfs, entry.run_name, "BFS")
                phase_metrics(parsed.pfs3, entry.run_name, "PFS3")
            except QualificationError as error:
                entry_errors.append(str(error))
        if entry_errors:
            failures.extend(entry_errors)
            continue
        verified.append(VerifiedConfirmationRun(entry, parsed))
    if failures:
        raise QualificationError(
            "verification failures (no expected run is omitted):\n  " + "\n  ".join(failures)
        )
    return verified


def ratio_statistics(ratios: list[float]) -> dict[str, Any]:
    if not ratios:
        raise QualificationError("cannot summarize an empty ratio series")
    return {
        "pair_count": len(ratios),
        "median": statistics.median(ratios),
        "range": [min(ratios), max(ratios)],
        "slower_pairs_ratio_above_1": sum(ratio > 1.0 for ratio in ratios),
    }


def comparison_observation(
    *,
    phase: str,
    family: str,
    repeat: int,
    order: str,
    first_handler: str,
    baseline_label: str,
    candidate_label: str,
    baseline_bfs: int,
    candidate_bfs: int,
    baseline_pfs3: int,
    candidate_pfs3: int,
) -> dict[str, Any]:
    if min(baseline_bfs, candidate_bfs, baseline_pfs3, candidate_pfs3) <= 0:
        raise QualificationError(f"{family}/{repeat}: comparison timings must be positive")
    bfs_ratio = candidate_bfs / baseline_bfs
    pfs3_ratio = candidate_pfs3 / baseline_pfs3
    return {
        "phase": phase,
        "family": family,
        "repeat": repeat,
        "filesystem_order": order,
        "first_handler": first_handler,
        "baseline": {"variant": baseline_label, "bfs_us": baseline_bfs, "pfs3_us": baseline_pfs3},
        "candidate": {"variant": candidate_label, "bfs_us": candidate_bfs, "pfs3_us": candidate_pfs3},
        "ratios": {
            "bfs_candidate_over_baseline": bfs_ratio,
            "pfs3_candidate_over_baseline": pfs3_ratio,
            "normalized_bfs_over_pfs3": bfs_ratio / pfs3_ratio,
        },
    }


def grouped_ratio_summary(
    observations: list[dict[str, Any]], ratio_key: str
) -> dict[str, Any]:
    return ratio_statistics([row["ratios"][ratio_key] for row in observations])


def primary_pair_summaries(rows: list[VerifiedConfirmationRun]) -> list[dict[str, Any]]:
    output = []
    for family in PRIMARY_FAMILIES:
        family_rows = [row for row in rows if row.schedule.family == family]
        by_variant = {
            variant: {row.schedule.repeat: row for row in family_rows
                      if row.schedule.variant == variant}
            for variant in ("m5", "sparse")
        }
        if any(set(by_variant[variant]) != set(range(1, 9)) for variant in by_variant):
            raise QualificationError(f"{family}: expected eight paired M5/sparse repeats")
        first_handler_by_repeat = {}
        for repeat in range(1, 9):
            repeat_rows = [row.schedule for row in family_rows if row.schedule.repeat == repeat]
            repeat_rows.sort(key=lambda item: item.sequence)
            first_handler_by_repeat[repeat] = repeat_rows[0].variant

        for phase in EXPECTED_PHASES:
            observations = []
            for repeat in range(1, 9):
                m5_row, sparse_row = by_variant["m5"][repeat], by_variant["sparse"][repeat]
                if m5_row.schedule.order != sparse_row.schedule.order:
                    raise QualificationError(f"{family}/{repeat}: paired filesystem order differs")
                m5_bfs = phase_metrics(m5_row.verified.bfs, m5_row.schedule.run_name, "BFS")[phase]
                sparse_bfs = phase_metrics(sparse_row.verified.bfs, sparse_row.schedule.run_name, "BFS")[phase]
                m5_pfs3 = phase_metrics(m5_row.verified.pfs3, m5_row.schedule.run_name, "PFS3")[phase]
                sparse_pfs3 = phase_metrics(sparse_row.verified.pfs3, sparse_row.schedule.run_name, "PFS3")[phase]
                observations.append(comparison_observation(
                    phase=phase,
                    family=family,
                    repeat=repeat,
                    order=m5_row.schedule.order,
                    first_handler=first_handler_by_repeat[repeat],
                    baseline_label="m5",
                    candidate_label="sparse",
                    baseline_bfs=m5_bfs,
                    candidate_bfs=sparse_bfs,
                    baseline_pfs3=m5_pfs3,
                    candidate_pfs3=sparse_pfs3,
                ))

            strata = []
            for order in ("bfs-first", "pfs3-first"):
                for first_handler in ("m5", "sparse"):
                    subset = [
                        row for row in observations
                        if row["filesystem_order"] == order
                        and row["first_handler"] == first_handler
                    ]
                    if len(subset) != 2:
                        raise QualificationError(
                            f"{family}/{phase}: stratum {order}/{first_handler} has "
                            f"{len(subset)} pairs, expected 2"
                        )
                    strata.append({
                        "filesystem_order": order,
                        "first_handler": first_handler,
                        "pair_count": len(subset),
                        "ratios": {
                            key: grouped_ratio_summary(subset, key)
                            for key in (
                                "bfs_candidate_over_baseline",
                                "pfs3_candidate_over_baseline",
                                "normalized_bfs_over_pfs3",
                            )
                        },
                    })
            output.append({
                "family": family,
                "phase": phase,
                "ratio_direction": "sparse / M5",
                "raw_pairs": observations,
                "aggregate": {
                    key: grouped_ratio_summary(observations, key)
                    for key in (
                        "bfs_candidate_over_baseline",
                        "pfs3_candidate_over_baseline",
                        "normalized_bfs_over_pfs3",
                    )
                },
                "strata_by_filesystem_order_and_first_handler": strata,
                "normalized_ratio_interpretation": (
                    "descriptive interaction ratio (sparse BFS/M5 BFS) / "
                    "(sparse PFS3/M5 PFS3); it does not replace either raw ratio"
                ),
            })
    return output


def aa_control_summaries(rows: list[VerifiedConfirmationRun]) -> list[dict[str, Any]]:
    aa_rows = [row for row in rows if row.schedule.family in AA_FAMILIES]
    pairs: list[tuple[VerifiedConfirmationRun, VerifiedConfirmationRun]] = []
    for family in AA_FAMILIES:
        family_rows = [row for row in aa_rows if row.schedule.family == family]
        by_repeat = {
            repeat: {row.schedule.variant: row for row in family_rows if row.schedule.repeat == repeat}
            for repeat in (1, 2)
        }
        if any(set(group) != {"m5a", "m5b"} for group in by_repeat.values()):
            raise QualificationError(f"{family}: expected m5a/m5b pairs for both control repeats")
        for repeat in (1, 2):
            baseline, candidate = by_repeat[repeat]["m5a"], by_repeat[repeat]["m5b"]
            if baseline.schedule.order != candidate.schedule.order:
                raise QualificationError(f"{family}/{repeat}: paired A/A filesystem order differs")
            pairs.append((baseline, candidate))
    if len(pairs) != 4:
        raise QualificationError(f"A/A controls: expected all four control pairs, got {len(pairs)}")

    output = []
    for phase in EXPECTED_PHASES:
        observations = []
        for baseline_row, candidate_row in pairs:
            baseline = baseline_row.schedule
            candidate = candidate_row.schedule
            observations.append(comparison_observation(
                phase=phase,
                family=baseline.family,
                repeat=baseline.repeat,
                order=baseline.order,
                first_handler="m5a",
                baseline_label="m5a",
                candidate_label="m5b",
                baseline_bfs=phase_metrics(baseline_row.verified.bfs, baseline.run_name, "BFS")[phase],
                candidate_bfs=phase_metrics(candidate_row.verified.bfs, candidate.run_name, "BFS")[phase],
                baseline_pfs3=phase_metrics(baseline_row.verified.pfs3, baseline.run_name, "PFS3")[phase],
                candidate_pfs3=phase_metrics(candidate_row.verified.pfs3, candidate.run_name, "PFS3")[phase],
            ))
        by_mode = []
        for family in AA_FAMILIES:
            mode_observations = [row for row in observations if row["family"] == family]
            if len(mode_observations) != 2:
                raise QualificationError(
                    f"A/A/{phase}: {family} has {len(mode_observations)} controls, expected two"
                )
            by_mode.append({
                "family": family,
                "pair_count": len(mode_observations),
                "ratios": {
                    key: grouped_ratio_summary(mode_observations, key)
                    for key in (
                        "bfs_candidate_over_baseline",
                        "pfs3_candidate_over_baseline",
                        "normalized_bfs_over_pfs3",
                    )
                },
            })
        strata = []
        for order in ("bfs-first", "pfs3-first"):
            subset = [row for row in observations if row["filesystem_order"] == order]
            if len(subset) != 2:
                raise QualificationError(
                    f"A/A/{phase}: {order} has {len(subset)} controls, expected two"
                )
            strata.append({
                "filesystem_order": order,
                "first_handler": "m5a",
                "pair_count": len(subset),
                "ratios": {
                    key: grouped_ratio_summary(subset, key)
                    for key in (
                        "bfs_candidate_over_baseline",
                        "pfs3_candidate_over_baseline",
                        "normalized_bfs_over_pfs3",
                    )
                },
            })
        output.append({
            "phase": phase,
            "ratio_direction": "m5b / m5a",
            "raw_pairs": observations,
            "aggregate_by_mode": by_mode,
            "mixed_mode_aggregate_all_four_controls": {
                key: grouped_ratio_summary(observations, key)
                for key in (
                    "bfs_candidate_over_baseline",
                    "pfs3_candidate_over_baseline",
                    "normalized_bfs_over_pfs3",
                )
            },
            "mixed_mode_aggregate_note": (
                "Descriptive combined summary across normal and durable controls; "
                "use aggregate_by_mode for within-mode interpretation."
            ),
            "strata_by_filesystem_order_mixed_mode": strata,
            "control_pair_count": len(observations),
        })
    return output


def bfs_pfs3_calibration(rows: list[VerifiedConfirmationRun]) -> list[dict[str, Any]]:
    output = []
    for phase in EXPECTED_PHASES:
        records = []
        counts_by_group: dict[tuple[str, str], list[bool]] = defaultdict(list)
        for row in rows:
            bfs_us = phase_metrics(row.verified.bfs, row.schedule.run_name, "BFS")[phase]
            pfs3_us = phase_metrics(row.verified.pfs3, row.schedule.run_name, "PFS3")[phase]
            ratio = bfs_us / pfs3_us
            over_5 = ratio > 5.0
            group = (row.schedule.family, row.schedule.variant)
            counts_by_group[group].append(over_5)
            records.append({
                "run_name": row.schedule.run_name,
                "family": row.schedule.family,
                "mode_family": row.schedule.mode_family,
                "variant": row.schedule.variant,
                "repeat": row.schedule.repeat,
                "filesystem_order": row.schedule.order,
                "bfs_us": bfs_us,
                "pfs3_us": pfs3_us,
                "bfs_over_pfs3": ratio,
                "over_5": over_5,
            })
        groups = [
            {
                "family": family,
                "variant": variant,
                "run_count": len(flags),
                "over_5_count": sum(flags),
            }
            for (family, variant), flags in sorted(counts_by_group.items())
        ]
        output.append({
            "phase": phase,
            "individual_ratios": records,
            "over_5_count_all_40_runs": sum(row["over_5"] for row in records),
            "run_count": len(records),
            "over_5_counts_by_family_and_variant": groups,
        })
    return output


def build_summary(root: Path, rows: list[VerifiedConfirmationRun]) -> dict[str, Any]:
    if len(rows) != 40:
        raise QualificationError(f"expected 40 verified confirmation runs, got {len(rows)}")
    if len(EXPECTED_PHASES) != 23:
        raise RuntimeError("internal error: fixed phase inventory must contain 23 phases")
    return {
        "summary": "BFS sparse CRC append/create confirmation",
        "input_directory": str(root.resolve()),
        "run_inventory": {
            "expected_runs": 40,
            "verified_runs": len(rows),
            "primary_timing_runs": 32,
            "primary_pairs": 16,
            "aa_control_runs": 8,
            "aa_control_pairs": 4,
            "all_23_phases_required": True,
            "phase_count": len(EXPECTED_PHASES),
        },
        "cohort_separation": {
            "confirmation_primary": (
                "The 32 normal/durable confirmation runs are summarized as their own cohort."
            ),
            "confirmation_controls": (
                "The eight m5a/m5b A/A control runs are summarized separately."
            ),
            "previous_main_32_reference": str(
                (SCRIPT_DIR.parent / "bfs-sparse-key-crc-performance-2026-10-09").resolve()
            ),
            "previous_main_32_included_or_pooled": False,
        },
        "interpretation_caveats": [
            (
                "Directory CRC work can affect file creation and initial Open in "
                "append-labeled phases. Those cases use Open MODE_NEWFILE and grow a "
                "new file through one handle; they do not measure appending to an "
                "already existing file or handle."
            ),
            (
                "The runner checks snapdog-runner.service, which is not found on Cachy, "
                "making that guard effectively fail-open for "
                "actions.runner.metaneutrons-snapdog-os.cachy.service. The separately "
                "retained actual-ci-state.log (14:28:58Z) and runner-inventory.log record "
                "the actual CI state: runner units loaded but inactive/dead with PID 0, "
                "and no active Runner.Listener or Worker."
            ),
        ],
        "primary_sparse_over_m5": primary_pair_summaries(rows),
        "aa_controls_m5b_over_m5a": aa_control_summaries(rows),
        "bfs_pfs3_individual_ratios_and_over_5_counts": bfs_pfs3_calibration(rows),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path", nargs="?", type=Path, default=SCRIPT_DIR,
        help="confirmation evidence directory (default: this script's directory)",
    )
    args = parser.parse_args(argv)
    try:
        rows = verify_inventory(args.path)
        summary = build_summary(args.path, rows)
    except (QualificationError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    json.dump(summary, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
