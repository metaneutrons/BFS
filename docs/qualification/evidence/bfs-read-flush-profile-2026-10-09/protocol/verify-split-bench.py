#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strictly validate split comparison diagnostics produced by the guest."""

from __future__ import annotations

import argparse
from functools import lru_cache
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
MAX_FILE_BYTES = 1024 * 1024
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
MARKER = b"BFS-PFS3-SPLIT-COMPLETE\n"
FLUSH_PHASES = {
    "SMALL_CREATE_40",
    "SEQ_WRITE_8M",
    "SMALL_DELETE_40",
    "APPEND_4K_1M",
    "APPEND_1K_256K",
}
READONLY_COUNTERS = {
    "BIO_WRITES",
    "BIO_UPDATES",
    "DATA_WRITES",
    "NODE_WRITES",
    "TXN_COMMITS",
    "NODE_CRC_WRITE_CALLS",
    "NODE_CRC_WRITE_SAMPLES",
    "NODE_CRC_WRITE_SAMPLE_TICKS",
}
UINT32_COUNTERS = {
    "BIO_READS",
    "BIO_WRITES",
    "BIO_UPDATES",
    "DATA_READS",
    "DATA_WRITES",
    "NODE_WRITES",
    "TXN_COMMITS",
    "EXTENT_MAPS",
    "CLOCK_PAIR_TICKS",
}


def _macro_entries(path: Path, macro: str, arity: int) -> tuple[tuple[str, ...], ...]:
    """Read X-macro entries from the C source of truth."""
    lines = path.read_text(encoding="ascii").splitlines()
    definition = f"#define {macro}(X)"
    for index, line in enumerate(lines):
        if not line.startswith(definition):
            continue
        entries = []
        for entry_line in lines[index + 1:]:
            stripped = entry_line.strip()
            if not stripped.startswith("X("):
                break
            if stripped.endswith("\\"):
                stripped = stripped[:-1].rstrip()
            match = re.fullmatch(r"X\((.*)\)", stripped)
            if not match:
                raise ValueError(f"malformed {macro} entry: {entry_line!r}")
            fields = tuple(field.strip() for field in match.group(1).split(","))
            if len(fields) != arity or any(not field for field in fields):
                raise ValueError(f"malformed {macro} entry: {entry_line!r}")
            entries.append(fields)
        if not entries:
            raise ValueError(f"empty or malformed macro {macro} in {path}")
        return tuple(entries)
    raise ValueError(f"macro {macro} not found in {path}")


def _macro_integer(path: Path, macro: str) -> int:
    pattern = re.compile(rf"^#define\s+{re.escape(macro)}\s+([0-9]+)\s*$")
    matches = [pattern.fullmatch(line) for line in path.read_text(encoding="ascii").splitlines()]
    values = [int(match.group(1)) for match in matches if match]
    if len(values) != 1:
        raise ValueError(f"expected one integer definition for {macro} in {path}")
    return values[0]


@lru_cache(maxsize=1)
def _inventories():
    split_header = ROOT / "tools/fs-compare-split.h"
    probe_header = ROOT / "src/amiga/perf_probe.h"
    return {
        "ops": tuple(row[0] for row in _macro_entries(split_header, "BFS_SPLIT_OPS", 2)),
        "phases": tuple(row[0] for row in _macro_entries(split_header, "BFS_SPLIT_PHASES", 1)),
        "counters": tuple(row[0] for row in _macro_entries(split_header, "BFS_SPLIT_COUNTERS", 2)),
        "cpu_scopes": tuple(row[0] for row in _macro_entries(probe_header, "BFS_PERF_CPU_SCOPES", 2)),
        "lookup_counters": tuple(row[0] for row in _macro_entries(probe_header, "BFS_PERF_LOOKUP_COUNTERS", 2)),
        "probe_version": _macro_integer(probe_header, "BFS_PERF_PROBE_VERSION"),
    }


def build_schema(filesystem: str, mode: str) -> list[tuple[str, str | None]]:
    """Return rows as (key, fixed value); None marks a numeric value."""
    if filesystem not in ("bfs", "pfs3"):
        raise ValueError(f"unknown filesystem: {filesystem}")
    if mode not in ("split-compare", "split-durable-compare"):
        raise ValueError(f"unknown mode: {mode}")

    inventories = _inventories()
    bfs = filesystem == "bfs"
    durable = mode == "split-durable-compare"
    drive = "DH1:" if bfs else "DH2:"
    rows: list[tuple[str, str | None]] = [
        ("FS_SPLIT_COMPARE", "1"),
        ("DRIVE", drive),
        ("DURABLE_MODE", "1" if durable else "0"),
        ("PROBE_ENABLED", "1" if bfs else "0"),
    ]

    for phase in inventories["phases"]:
        prefix = phase
        rows.extend((
            (f"{prefix}_US", None),
            (f"{prefix}_WORK_US", None),
            (f"{prefix}_VOLUME_FLUSH_US", None),
            (f"{prefix}_HAS_VOLUME_FLUSH", None),
        ))
        for op in inventories["ops"]:
            rows.extend((
                (f"{prefix}_GUEST_{op}_CALLS", None),
                (f"{prefix}_GUEST_{op}_US", None),
            ))
        if bfs:
            for stage in ("WORK", "VOLUME_FLUSH"):
                stage_prefix = f"{prefix}_{stage}_"
                rows.extend((
                    (stage_prefix + counter, None)
                    for counter in inventories["counters"]
                ))
                for scope in inventories["cpu_scopes"]:
                    rows.extend((
                        (f"{stage_prefix}{scope}_CALLS", None),
                        (f"{stage_prefix}{scope}_SAMPLES", None),
                        (f"{stage_prefix}{scope}_SAMPLE_TICKS", None),
                    ))
                rows.extend((
                    (stage_prefix + counter, None)
                    for counter in inventories["lookup_counters"]
                ))

    rows.extend((
        ("PROBE_VERSION", str(inventories["probe_version"]) if bfs else "0"),
        ("CLOCK_HZ", None),
        ("CRC_SAMPLE_STRIDE", "1" if bfs else "0"),
        ("CPU_SAMPLE_STRIDE", "1" if bfs else "0"),
        ("PASS", "1"),
    ))
    return rows


def _uint32_field(key: str, inventories: dict[str, object]) -> bool:
    if key == "CLOCK_HZ" or key.endswith(("_CALLS", "_SAMPLES", "_WORK_US",
                                          "_VOLUME_FLUSH_US")):
        return True
    for stage_marker in ("_WORK_", "_VOLUME_FLUSH_"):
        if stage_marker in key:
            metric = key.split(stage_marker, 1)[1]
            return (metric in UINT32_COUNTERS or
                    metric in inventories["lookup_counters"])
    return False


def _read_tsv(path: Path, schema: list[tuple[str, str | None]]) -> dict[str, int | str]:
    try:
        content = path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read {path.name}: {error}") from error
    if len(content) > MAX_FILE_BYTES:
        raise ValueError(f"{path.name} exceeds the {MAX_FILE_BYTES}-byte limit")
    if not content.endswith(b"\n") or b"\r" in content:
        raise ValueError(f"{path.name} must use LF-terminated rows")
    try:
        text = content.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"{path.name} is not ASCII") from error

    lines = text[:-1].split("\n")
    values: dict[str, int | str] = {}
    for index, (expected_key, fixed_value) in enumerate(schema):
        row_number = index + 1
        if index >= len(lines):
            raise ValueError(f"{path.name}: missing row {expected_key} at line {row_number}")
        fields = lines[index].split("\t")
        if len(fields) != 2 or not fields[0] or not fields[1]:
            raise ValueError(f"{path.name}: line {row_number} must contain one key and one value")
        key, raw_value = fields
        if key != expected_key:
            raise ValueError(
                f"{path.name}: line {row_number} expected {expected_key}, found {key}"
            )
        if fixed_value is not None:
            if raw_value != fixed_value:
                raise ValueError(
                    f"{path.name}: {key} must be {fixed_value}, found {raw_value}"
                )
            if fixed_value.isdecimal():
                values[key] = int(fixed_value)
            else:
                values[key] = fixed_value
            continue
        if not re.fullmatch(r"[0-9]+", raw_value):
            raise ValueError(f"{path.name}: {key} must be a nonnegative integer")
        number = int(raw_value)
        limit = UINT32_MAX if _uint32_field(key, _inventories()) else UINT64_MAX
        if number > limit:
            bits = 32 if limit == UINT32_MAX else 64
            raise ValueError(f"{path.name}: {key} exceeds the unsigned {bits}-bit limit")
        values[key] = number
    if len(lines) != len(schema):
        extra_line = len(schema) + 1
        raise ValueError(f"{path.name}: unexpected row at line {extra_line}")
    return values


def _validate_semantics(values: dict[str, int | str], filesystem: str, mode: str) -> None:
    inventories = _inventories()
    durable = mode == "split-durable-compare"
    bfs = filesystem == "bfs"
    readonly_phases = {
        "LOOKUP_400", "SMALL_READ_40", "SEQ_READ_8M", "APPEND_READ_1280K",
        *(phase for phase in inventories["phases"] if phase.startswith("LIST_")),
    }

    for phase in inventories["phases"]:
        prefix = phase
        work_us = values[f"{prefix}_WORK_US"]
        flush_us = values[f"{prefix}_VOLUME_FLUSH_US"]
        if work_us <= 0:
            raise ValueError(f"{filesystem}.{phase}: WORK_US must be positive")
        if values[f"{prefix}_US"] != work_us + flush_us:
            raise ValueError(f"{filesystem}.{phase}: US must equal WORK_US plus VOLUME_FLUSH_US")

        has_flush = values[f"{prefix}_HAS_VOLUME_FLUSH"]
        should_flush = durable and phase in FLUSH_PHASES
        if has_flush != int(should_flush):
            expected = "1" if should_flush else "0"
            raise ValueError(f"{filesystem}.{phase}: HAS_VOLUME_FLUSH must be {expected}")
        if not has_flush:
            for key, value in values.items():
                if key.startswith(f"{prefix}_VOLUME_FLUSH_") and value != 0:
                    raise ValueError(f"{filesystem}.{phase}: absent flush has nonzero {key}")

        guest_us_total = 0
        for op in inventories["ops"]:
            calls = values[f"{prefix}_GUEST_{op}_CALLS"]
            elapsed = values[f"{prefix}_GUEST_{op}_US"]
            if calls == 0 and elapsed != 0:
                raise ValueError(f"{filesystem}.{phase}: {op} time is nonzero with zero calls")
            guest_us_total += elapsed
        if guest_us_total > work_us:
            raise ValueError(f"{filesystem}.{phase}: guest operation time exceeds WORK_US")

        if phase in readonly_phases:
            for op in ("WRITE", "HANDLE_FLUSH"):
                if (values[f"{prefix}_GUEST_{op}_CALLS"] != 0 or
                        values[f"{prefix}_GUEST_{op}_US"] != 0):
                    raise ValueError(f"{filesystem}.{phase}: read-only phase has {op} activity")
            if bfs:
                for metric in READONLY_COUNTERS:
                    if values[f"{prefix}_WORK_{metric}"] != 0:
                        raise ValueError(
                            f"{filesystem}.{phase}: read-only phase has nonzero {metric}"
                        )

        expected_ops = None
        if phase == "SEQ_READ_8M":
            expected_ops = {"READ": 129, "VERIFY": 128, "OPEN": 1, "CLOSE": 1}
        elif phase == "APPEND_READ_1280K":
            expected_ops = {"READ": 22, "VERIFY": 20, "OPEN": 2, "CLOSE": 2}
        if expected_ops:
            for op, expected_calls in expected_ops.items():
                calls = values[f"{prefix}_GUEST_{op}_CALLS"]
                if calls != expected_calls:
                    raise ValueError(
                        f"{filesystem}.{phase}: {op} calls must be {expected_calls}, found {calls}"
                    )

        if not bfs:
            continue
        for stage in ("WORK", "VOLUME_FLUSH"):
            stage_prefix = f"{prefix}_{stage}_"
            for crc in ("NODE_CRC_READ", "NODE_CRC_WRITE"):
                calls = values[f"{stage_prefix}{crc}_CALLS"]
                samples = values[f"{stage_prefix}{crc}_SAMPLES"]
                sample_ticks = values[f"{stage_prefix}{crc}_SAMPLE_TICKS"]
                if samples != calls:
                    raise ValueError(f"{filesystem}.{phase}: {crc} samples must equal calls")
                if calls == 0 and sample_ticks != 0:
                    raise ValueError(
                        f"{filesystem}.{phase}: {crc} ticks are nonzero with zero calls"
                    )

            for scope in inventories["cpu_scopes"]:
                calls = values[f"{stage_prefix}{scope}_CALLS"]
                samples = values[f"{stage_prefix}{scope}_SAMPLES"]
                if samples != calls:
                    raise ValueError(
                        f"{filesystem}.{phase}: {scope} samples must equal calls"
                    )

            # Packet scopes are inclusive, but these categories partition PACKET.
            # Other CPU-scope timings are inclusive and must not be summed.
            packet_categories = tuple(
                scope for scope in inventories["cpu_scopes"]
                if scope.startswith("PACKET_")
            )
            for suffix in ("CALLS", "SAMPLES", "SAMPLE_TICKS"):
                total = sum(values[f"{stage_prefix}{scope}_{suffix}"]
                            for scope in packet_categories)
                if total != values[f"{stage_prefix}PACKET_{suffix}"]:
                    raise ValueError(
                        f"{filesystem}.{phase}: PACKET_{suffix} does not equal category sum"
                    )

            for data_name, bio_name in (("DATA_READS", "BIO_READS"),
                                        ("DATA_WRITES", "BIO_WRITES"),
                                        ("DATA_READ_TICKS", "READ_TICKS"),
                                        ("DATA_WRITE_TICKS", "WRITE_TICKS")):
                if values[f"{stage_prefix}{data_name}"] > values[f"{stage_prefix}{bio_name}"]:
                    raise ValueError(
                        f"{filesystem}.{phase}: {data_name} exceeds {bio_name}"
                    )


def verify(results: Path, mode: str) -> None:
    if mode not in ("split-compare", "split-durable-compare"):
        raise ValueError("mode must be split-compare or split-durable-compare")
    try:
        marker = (results / "complete.txt").read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read complete.txt: {error}") from error
    if marker != MARKER:
        raise ValueError("guest completion marker must be exactly BFS-PFS3-SPLIT-COMPLETE plus LF")

    info_path = results / "info-after-format.txt"
    try:
        info = info_path.read_bytes()
    except OSError as error:
        raise ValueError(f"post-format volume inventory is missing or unreadable: {error}") from error
    if not info or len(info) > MAX_FILE_BYTES:
        raise ValueError("post-format volume inventory is empty or exceeds the size limit")
    if not re.search(rb"DH1.*Read/Write BFSTest", info):
        raise ValueError("BFS volume was not mounted")
    if not re.search(rb"DH2.*Read/Write PFSTest", info):
        raise ValueError("PFS3 volume was not mounted")

    for filesystem in ("bfs", "pfs3"):
        output = results / f"{filesystem}.split.tsv"
        values = _read_tsv(output, build_schema(filesystem, mode))
        if filesystem == "bfs" and values["CLOCK_HZ"] <= 0:
            raise ValueError("bfs: CLOCK_HZ must be positive")
        if filesystem == "pfs3" and values["CLOCK_HZ"] != 0:
            raise ValueError("pfs3: CLOCK_HZ must be zero")
        _validate_semantics(values, filesystem, mode)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Strictly verify split comparison result TSVs."
    )
    parser.add_argument("results", type=Path, help="system/Results directory")
    parser.add_argument("mode", choices=("split-compare", "split-durable-compare"))
    args = parser.parse_args(argv)
    try:
        verify(args.results, args.mode)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("PASS: split comparison outputs are complete and internally consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
