#!/usr/bin/env python3
"""Strict schema-12 reader for private split/coalesce attribution runs.

New path dimensions come from perf_probe.h. The unchanged schema-11 verifier
remains authoritative for all pre-existing fields and mounted-run evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable


PHASES_RE = re.compile(r"deep_compare_phases=\(([^)]*)\)")
TUPLE_RE = re.compile(r"X\(\s*([A-Z0-9_]+)\s*,\s*([a-z0-9_]+)\s*\)")
UINT_RE = re.compile(r"^[0-9]+$")
U32_MAX = (1 << 32) - 1
U64_MAX = (1 << 64) - 1
EXPECTED_MACRO_CARDINALITIES = {
    "BFS_PERF_TREE_ROLES": 5,
    "BFS_PERF_PATH_SHAPES": 2,
    "BFS_PERF_SPLIT_KINDS": 2,
    "BFS_PERF_SPLIT_EVENTS": 5,
    "BFS_PERF_COALESCE_KINDS": 2,
    "BFS_PERF_COALESCE_EVENTS": 2,
}


class ValidationError(Exception):
    pass


def fail(message: str) -> None:
    raise ValidationError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_macro(header_text: str, macro_name: str) -> list[tuple[str, str]]:
    lines = header_text.splitlines()
    marker = f"#define {macro_name}(X)"
    for index, line in enumerate(lines):
        if not line.startswith(marker):
            continue
        body_parts = [line[len(marker):]]
        continued = line.rstrip().endswith("\\")
        cursor = index + 1
        while continued:
            if cursor >= len(lines):
                fail(f"unterminated SSOT macro {macro_name}")
            body_line = lines[cursor]
            body_parts.append(body_line)
            continued = body_line.rstrip().endswith("\\")
            cursor += 1
        body = "\n".join(part.rstrip().removesuffix("\\") for part in body_parts)
        entries = TUPLE_RE.findall(body)
        residual = TUPLE_RE.sub("", body).replace("\\", "").strip()
        if not entries or residual:
            fail(f"cannot parse SSOT macro {macro_name}")
        if len({upper for upper, _ in entries}) != len(entries):
            fail(f"duplicate tuple in SSOT macro {macro_name}")
        return entries
    fail(f"missing SSOT macro {macro_name}")


def numeric_define(header_text: str, name: str) -> int:
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+([0-9]+)(?:[uUlL]*)\s*$",
        header_text,
        re.MULTILINE,
    )
    if not match:
        fail(f"missing or malformed header define {name}")
    return int(match.group(1), 10)


def phase_names_from_verifier(verifier_text: str) -> list[str]:
    match = PHASES_RE.search(verifier_text)
    if not match:
        fail("cannot derive legacy phase order from the original verifier")
    phases = match.group(1).split()
    if len(phases) != 6 or len(set(phases)) != len(phases):
        fail("legacy verifier phase list is not the expected six unique phases")
    return phases


def parse_loop_axes(guest_text: str, prefix: str, terminal: str,
                    expected: list[str]) -> list[str]:
    start = guest_text.find("static void emit_path_rows")
    end = guest_text.find("static void emit_deep_counter_rows", start)
    if start < 0 or end < 0:
        fail("cannot locate guest path-row emitter")
    body = guest_text[start:end]
    first = body.find(f"for (ULONG kind = 0; kind < BFS_PERF_{terminal}_COUNT; kind++)")
    if first < 0:
        fail(f"guest emitter does not start the {prefix} path loop")
    if prefix == "SPLIT":
        loop_body = body[first:body.find(
            "for (ULONG kind = 0; kind < BFS_PERF_COALESCE_COUNT; kind++)", first
        )]
    else:
        loop_body = body[first:]
    axes: list[str] = []
    for match in re.finditer(
        r"for\s*\(\s*ULONG\s+(kind|role|shape|event)\s*=\s*0\s*;\s*"
        r"\1\s*<\s*BFS_PERF_([A-Z_]+)_COUNT\s*;\s*\1\+\+\s*\)",
        loop_body,
    ):
        variable, limit = match.groups()
        expected_limit = {
            "kind": f"{terminal}",
            "role": "ROLE",
            "shape": "SHAPE",
            "event": f"{terminal}_EVENT",
        }[variable]
        if limit != expected_limit:
            fail(f"guest {prefix} loop uses unexpected bound {limit} for {variable}")
        axes.append(variable)
    if axes != expected:
        fail(f"guest {prefix} loop order changed: {axes!r}")
    emission = re.compile(
        rf'path_metric\(\s*phase,\s*"{prefix}",\s*'
        rf'{prefix.lower()}_kind_names\[kind\],\s*path_role_names\[role\],\s*'
        rf'path_shape_names\[shape\],\s*{prefix.lower()}_event_names\[event\],'
    )
    if not emission.search(loop_body):
        fail(f"guest {prefix} row names no longer follow the SSOT tuple axes")
    return axes


def load_schema(header_path: Path, verifier_path: Path) -> dict:
    if not header_path.is_file() or not verifier_path.is_file():
        fail("header or original schema-11 verifier does not exist")
    guest_path = header_path.resolve().parents[2] / "tools" / "fs-compare-bench.c"
    if not guest_path.is_file():
        fail("guest source adjacent to the ABI header is missing")
    header_text = header_path.read_text(encoding="utf-8")
    guest_text = guest_path.read_text(encoding="utf-8")
    verifier_text = verifier_path.read_text(encoding="utf-8")

    if numeric_define(header_text, "BFS_PERF_PROBE_VERSION") != 13:
        fail("unsupported probe ABI; expected ABI 13")
    if numeric_define(header_text, "BFS_PERF_DEEP_SCHEMA_VERSION") != 12:
        fail("unsupported deep-compare schema; expected schema 12")
    crc_stride = numeric_define(header_text, "BFS_PERF_CRC_SAMPLE_STRIDE")
    cpu_stride = numeric_define(header_text, "BFS_PERF_CPU_SAMPLE_STRIDE")
    if crc_stride <= 0 or cpu_stride <= 0:
        fail("sample strides must be positive")

    macros = {
        name: read_macro(header_text, name)
        for name in EXPECTED_MACRO_CARDINALITIES
    }
    for name, cardinality in EXPECTED_MACRO_CARDINALITIES.items():
        if len(macros[name]) != cardinality:
            fail(f"schema-12 SSOT dimension {name} has changed")

    split_axes = parse_loop_axes(
        guest_text, "SPLIT", "SPLIT", ["kind", "role", "shape", "event"]
    )
    coalesce_axes = parse_loop_axes(
        guest_text, "COALESCE", "COALESCE", ["kind", "role", "shape", "event"]
    )
    # Confirm path emission follows the pre-existing CPU fields in each phase.
    cpu_emit = guest_text.find("BFS_PERF_CPU_SCOPES(BFS_EMIT_CPU_SCOPE)")
    path_emit = guest_text.find("emit_path_rows(phase, snapshot);")
    if cpu_emit < 0 or path_emit < 0 or cpu_emit >= path_emit:
        fail("guest path rows are not appended after the existing CPU rows")
    phases = phase_names_from_verifier(verifier_text)

    dimensions = {
        "kind": macros["BFS_PERF_SPLIT_KINDS"],
        "role": macros["BFS_PERF_TREE_ROLES"],
        "shape": macros["BFS_PERF_PATH_SHAPES"],
        "event": macros["BFS_PERF_SPLIT_EVENTS"],
    }
    split_definitions = make_path_definitions("SPLIT", dimensions, split_axes)
    split_suffixes = [definition["suffix"] for definition in split_definitions]
    dimensions = {
        "kind": macros["BFS_PERF_COALESCE_KINDS"],
        "role": macros["BFS_PERF_TREE_ROLES"],
        "shape": macros["BFS_PERF_PATH_SHAPES"],
        "event": macros["BFS_PERF_COALESCE_EVENTS"],
    }
    coalesce_definitions = make_path_definitions("COALESCE", dimensions, coalesce_axes)
    coalesce_suffixes = [definition["suffix"] for definition in coalesce_definitions]
    if len(split_suffixes) != 100 or len(coalesce_suffixes) != 40:
        fail("schema-12 path matrix is not 100 split plus 40 coalesce rows")

    expected_event_names = {
        "BFS_PERF_SPLIT_EVENTS": {upper for upper, _ in macros["BFS_PERF_SPLIT_EVENTS"]},
        "BFS_PERF_COALESCE_EVENTS": {upper for upper, _ in macros["BFS_PERF_COALESCE_EVENTS"]},
    }
    if expected_event_names["BFS_PERF_SPLIT_EVENTS"] != {
        "ATTEMPT", "INITIAL_WRITE", "RIGHT_SELECT", "RIGHT_READ", "RIGHT_WRITE"
    } or expected_event_names["BFS_PERF_COALESCE_EVENTS"] != {
        "ATTEMPT", "COMPLETE"
    }:
        fail("schema-12 event names changed from the validated event semantics")

    return {
        "header_path": header_path.resolve(),
        "guest_path": guest_path.resolve(),
        "verifier_path": verifier_path.resolve(),
        "header_sha256": sha256_file(header_path),
        "guest_sha256": sha256_file(guest_path),
        "verifier_sha256": sha256_file(verifier_path),
        "phases": phases,
        "crc_stride": crc_stride,
        "cpu_stride": cpu_stride,
        "roles": [upper for upper, _ in macros["BFS_PERF_TREE_ROLES"]],
        "shapes": [upper for upper, _ in macros["BFS_PERF_PATH_SHAPES"]],
        "split_kinds": [upper for upper, _ in macros["BFS_PERF_SPLIT_KINDS"]],
        "split_events": [upper for upper, _ in macros["BFS_PERF_SPLIT_EVENTS"]],
        "coalesce_kinds": [upper for upper, _ in macros["BFS_PERF_COALESCE_KINDS"]],
        "coalesce_events": [upper for upper, _ in macros["BFS_PERF_COALESCE_EVENTS"]],
        "split_suffixes": split_suffixes,
        "coalesce_suffixes": coalesce_suffixes,
        "split_definitions": split_definitions,
        "coalesce_definitions": coalesce_definitions,
    }


def make_path_definitions(prefix: str, dimensions: dict[str, list[tuple[str, str]]],
                          axes: list[str]) -> list[dict[str, str]]:
    entries = [dimensions[axis] for axis in axes]
    definitions: list[dict[str, str]] = []
    for combination in itertools.product(*entries):
        by_axis = {
            axis: tuple_value[0] for axis, tuple_value in zip(axes, combination)
        }
        definitions.append({
            "suffix": prefix + "_" + "_".join(tuple_value[0] for tuple_value in combination),
            **by_axis,
        })
    return definitions


def read_tsv(path: Path) -> tuple[list[str], list[tuple[str, str]]]:
    if not path.is_file():
        fail(f"missing schema-12 input {path.name}")
    raw = path.read_bytes()
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        fail(f"{path.name} is not ASCII TSV")
    if not text.endswith("\n"):
        fail(f"{path.name} does not end with a complete TSV line")
    if "\r" in text:
        fail(f"{path.name} contains noncanonical carriage-return line endings")
    lines = text.splitlines()
    if not lines or any(line == "" for line in lines):
        fail(f"{path.name} contains an empty TSV line")
    records: list[tuple[str, str]] = []
    seen: set[str] = set()
    for line_number, line in enumerate(lines, 1):
        fields = line.split("\t")
        if len(fields) != 2 or not fields[0]:
            fail(f"{path.name}:{line_number} is not a two-column TSV record")
        key, value = fields
        if key in seen:
            fail(f"{path.name}:{line_number} duplicates field {key}")
        seen.add(key)
        records.append((key, value))
    return lines, records


def parse_uint(value: str, name: str, bits: int) -> int:
    if not UINT_RE.fullmatch(value):
        fail(f"{name} is not an unsigned decimal integer")
    number = int(value, 10)
    maximum = U32_MAX if bits == 32 else U64_MAX
    if number > maximum:
        fail(f"{name} exceeds unsigned {bits}-bit range")
    return number


def field_width(name: str) -> int:
    # All exported counters, elapsed values, and clock/stride fields are ULONG
    # (32-bit). Snapshot tick accumulators alone are uint64_t, except the
    # clock-pair duration, which is intentionally an ULONG.
    is_clock_pair = name == "CLOCK_PAIR_TICKS" or name.endswith("_CLOCK_PAIR_TICKS")
    return 64 if name.endswith("_TICKS") and not is_clock_pair else 32


def values_map(records: Iterable[tuple[str, str]]) -> dict[str, str]:
    return {name: value for name, value in records}


def check_matrix_values(name: str, values: dict[str, str],
                        schema: dict) -> dict:
    path_matrix: dict = {}
    split_totals = {
        "by_kind": {kind: {} for kind in schema["split_kinds"]},
        "by_role": {role: {} for role in schema["roles"]},
        "by_shape": {shape: {} for shape in schema["shapes"]},
        "by_event": {},
    }
    coalesce_totals = {
        "by_kind": {kind: {} for kind in schema["coalesce_kinds"]},
        "by_role": {role: {} for role in schema["roles"]},
        "by_shape": {shape: {} for shape in schema["shapes"]},
        "by_event": {},
    }

    def add(bucket: dict, key: str, amount: int) -> None:
        bucket[key] = bucket.get(key, 0) + amount

    for prefix, kinds, events, definitions, totals in (
        ("SPLIT", schema["split_kinds"], schema["split_events"],
         schema["split_definitions"], split_totals),
        ("COALESCE", schema["coalesce_kinds"], schema["coalesce_events"],
         schema["coalesce_definitions"], coalesce_totals),
    ):
        section: dict = {kind: {} for kind in kinds}
        for definition in definitions:
            suffix = definition["suffix"]
            kind, role, shape, event = (
                definition["kind"], definition["role"],
                definition["shape"], definition["event"]
            )
            field_name = f"{name}_{suffix}"
            count = parse_uint(values[field_name], field_name, 32)
            section[kind].setdefault(role, {}).setdefault(shape, {})[event] = str(count)
            add(totals["by_kind"][kind], event, count)
            add(totals["by_role"][role], event, count)
            add(totals["by_shape"][shape], event, count)
            add(totals["by_event"], event, count)
        path_matrix[prefix.lower()] = section

    for kind in schema["split_kinds"]:
        for role in schema["roles"]:
            for shape in schema["shapes"]:
                path = path_matrix["split"][kind][role][shape]
                attempt = int(path["ATTEMPT"])
                initial = int(path["INITIAL_WRITE"])
                selected = int(path["RIGHT_SELECT"])
                read = int(path["RIGHT_READ"])
                rewrite = int(path["RIGHT_WRITE"])
                if attempt != initial:
                    fail(f"{name} {kind}/{role}/{shape}: split attempt/initial-write chain differs")
                if selected != read or selected != rewrite:
                    fail(f"{name} {kind}/{role}/{shape}: right select/read/rewrite counts differ")
                if selected > attempt:
                    fail(f"{name} {kind}/{role}/{shape}: right branch exceeds split attempts")

    for kind in schema["coalesce_kinds"]:
        for role in schema["roles"]:
            for shape in schema["shapes"]:
                path = path_matrix["coalesce"][kind][role][shape]
                if path["ATTEMPT"] != path["COMPLETE"]:
                    fail(f"{name} {kind}/{role}/{shape}: coalesce attempt/complete counts differ")
                if role not in {"FREE_TREE", "OTHER_TREE"}:
                    if any(int(path[event]) != 0 for event in schema["coalesce_events"]):
                        fail(
                            f"{name} {kind}/{role}/{shape}: coalesce events are "
                            "invalid for this tree role"
                        )

    def stringify(value):
        if isinstance(value, dict):
            return {key: stringify(item) for key, item in value.items()}
        if isinstance(value, int):
            return str(value)
        return value

    return {
        "path_matrix": path_matrix,
        "totals": {
            "split": stringify(split_totals),
            "coalesce": stringify(coalesce_totals),
        },
    }


def check_split_node_write_budget(phase: str, path_matrix: dict,
                                  legacy_values: dict[str, str],
                                  schema: dict) -> None:
    for role in schema["roles"]:
        budget_field = f"{phase}_{role}_NODE_WRITES"
        if budget_field not in legacy_values:
            fail(f"{phase} is missing legacy node-write budget for {role}")
        budget = parse_uint(legacy_values[budget_field], budget_field, 32)
        attributed = 0
        for kind in schema["split_kinds"]:
            for shape in schema["shapes"]:
                path = path_matrix["split"][kind][role][shape]
                attributed += int(path["INITIAL_WRITE"]) + int(path["RIGHT_WRITE"])
        if attributed > budget:
            fail(
                f"{phase} {role}: split write events {attributed} exceed "
                f"role node writes {budget}"
            )


def validate_file(path: Path, filesystem: str, schema: dict) -> dict:
    _, records = read_tsv(path)
    if len(records) < 4:
        fail(f"{path.name} is truncated")
    if records[0] != ("FS_DEEP_COMPARE", "12"):
        fail(f"{path.name} must declare FS_DEEP_COMPARE schema 12")
    expected_drive = "DH1:" if filesystem == "bfs" else "DH2:"
    if records[1] != ("DRIVE", expected_drive):
        fail(f"{path.name} has the wrong drive")
    if records[-1] != ("PASS", "1"):
        fail(f"{path.name} is missing terminal PASS=1")

    data_records = records[2:-1]
    for field, value in data_records:
        parse_uint(value, field, field_width(field))

    if filesystem == "pfs3":
        expected_names = [f"{phase}_US" for phase in schema["phases"]]
        observed_names = [field for field, _ in data_records]
        if observed_names != expected_names:
            fail("pfs3 schema 12 must contain exactly the six elapsed rows and no counters")
        for field, _ in data_records:
            parse_uint(dict(data_records)[field], field, 32)
        return {
            "fields": values_map(records),
            "field_order": [name for name, _ in records],
            "sha256": sha256_file(path),
            "legacy_non_time_counters_by_phase": {},
            "path_matrix": None,
            "totals": None,
        }

    all_suffixes = schema["split_suffixes"] + schema["coalesce_suffixes"]
    expected_path_fields = {
        f"{phase}_{suffix}" for phase in schema["phases"] for suffix in all_suffixes
    }
    cursor = 2
    base_records: list[tuple[str, str]] = []
    matrices: dict[str, dict] = {}
    legacy_by_phase: dict[str, dict[str, str]] = {}
    for phase in schema["phases"]:
        phase_records: list[tuple[str, str]] = []
        while cursor < len(records) - 1 and records[cursor][0].startswith(phase + "_"):
            phase_records.append(records[cursor])
            cursor += 1
        if not phase_records:
            fail(f"{path.name} is missing phase {phase}")
        suffix_count = len(all_suffixes)
        if len(phase_records) < suffix_count:
            fail(f"{path.name} is missing schema-12 path rows for {phase}")
        base_part = phase_records[:-suffix_count]
        matrix_part = phase_records[-suffix_count:]
        expected_matrix_names = [f"{phase}_{suffix}" for suffix in all_suffixes]
        if [field for field, _ in matrix_part] != expected_matrix_names:
            fail(f"{path.name} has missing, reordered, or unknown path rows in {phase}")
        if any(field in expected_path_fields for field, _ in base_part):
            fail(f"{path.name} path rows are not appended after legacy rows in {phase}")
        base_records.extend(base_part)
        legacy_by_phase[phase] = values_map(base_part)
        matrix_values = {field: value for field, value in matrix_part}
        for field, value in matrix_part:
            parse_uint(value, field, 32)
        matrices[phase] = check_matrix_values(phase, matrix_values, schema)
        check_split_node_write_budget(
            phase, matrices[phase]["path_matrix"], legacy_by_phase[phase], schema
        )

    global_records = records[cursor:-1]
    expected_globals = [
        ("CLOCK_HZ", None),
        ("CRC_SAMPLE_STRIDE", str(schema["crc_stride"])),
        ("CPU_SAMPLE_STRIDE", str(schema["cpu_stride"])),
    ]
    if len(global_records) != len(expected_globals):
        fail(f"{path.name} has missing or extra global rows")
    for (observed_name, observed_value), (expected_name, expected_value) in zip(
        global_records, expected_globals
    ):
        if observed_name != expected_name:
            fail(f"{path.name} has reordered or unknown global row {observed_name}")
        if expected_value is not None and observed_value != expected_value:
            fail(f"{path.name} has incorrect {expected_name}")
        parse_uint(observed_value, observed_name, 32)
    if int(dict(global_records)["CLOCK_HZ"], 10) == 0:
        fail(f"{path.name} CLOCK_HZ must be positive")

    projected_records = [("FS_DEEP_COMPARE", "11"), ("DRIVE", expected_drive)]
    projected_records.extend(base_records)
    projected_records.extend(global_records)
    projected_records.append(("PASS", "1"))
    legacy_non_time: dict[str, dict[str, str]] = {}
    legacy_fields = values_map(projected_records)
    for phase in schema["phases"]:
        phase_legacy = {
            field: value
            for field, value in legacy_by_phase[phase].items()
            if not field.endswith("_US") and not field.endswith("_TICKS")
        }
        legacy_non_time[phase] = phase_legacy
    matrix_totals = {
        phase: matrices[phase]["totals"] for phase in schema["phases"]
    }
    return {
        "fields": values_map(records),
        "field_order": [name for name, _ in records],
        "legacy_fields": legacy_fields,
        "legacy_field_order": [name for name, _ in projected_records],
        "legacy_non_time_counters_by_phase": legacy_non_time,
        "path_matrix": {phase: matrices[phase]["path_matrix"] for phase in schema["phases"]},
        "path_totals_by_phase": matrix_totals,
        "sha256": sha256_file(path),
    }


def validate_run(run_dir: Path, header: Path, verifier: Path) -> dict:
    schema = load_schema(header, verifier)
    results_dir = run_dir / "system" / "Results"
    bfs_path = results_dir / "bfs.deep-compare.tsv"
    pfs3_path = results_dir / "pfs3.deep-compare.tsv"
    bfs = validate_file(bfs_path, "bfs", schema)
    pfs3 = validate_file(pfs3_path, "pfs3", schema)

    # Run the unchanged schema-11 verifier against a private projection. This
    # preserves every old field/order and all mounted-run marker/inventory
    # checks without changing the qualification verifier or source evidence.
    for required in ("complete.txt", "info-after-format.txt"):
        if not (results_dir / required).is_file():
            fail(f"mounted-run evidence is missing {required}")
    with tempfile.TemporaryDirectory(
        prefix=".schema11-projection-", dir=Path(__file__).resolve().parent
    ) as temporary:
        projected = Path(temporary) / "system" / "Results"
        projected.mkdir(parents=True)
        shutil.copy2(results_dir / "complete.txt", projected / "complete.txt")
        shutil.copy2(results_dir / "info-after-format.txt", projected / "info-after-format.txt")
        (projected / "bfs.deep-compare.tsv").write_text(
            project_records(bfs["legacy_fields"]), encoding="ascii"
        )
        (projected / "pfs3.deep-compare.tsv").write_text(
            project_records(pfs3["fields"], schema_version="11"), encoding="ascii"
        )
        result = subprocess.run(
            ["bash", str(verifier), temporary, "deep-compare"],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip().splitlines()
            fail("original schema-11 verifier rejected projection: " +
                 (detail[-1] if detail else f"exit {result.returncode}"))

    return {
        "schema_version": 12,
        "probe_abi": 13,
        "semantics": (
            "Instrumentation counters describe recorded control-flow events; "
            "ATTEMPT/INITIAL_WRITE do not independently prove successful media I/O."
        ),
        "source_sha256": {
            "header": schema["header_sha256"],
            "guest": schema["guest_sha256"],
            "original_schema11_verifier": schema["verifier_sha256"],
            "schema12_consumer": sha256_file(Path(__file__)),
        },
        "input_sha256": {"bfs": bfs["sha256"], "pfs3": pfs3["sha256"]},
        "phases": schema["phases"],
        "dimensions": {
            "roles": schema["roles"],
            "shapes": schema["shapes"],
            "split_kinds": schema["split_kinds"],
            "split_events": schema["split_events"],
            "coalesce_kinds": schema["coalesce_kinds"],
            "coalesce_events": schema["coalesce_events"],
            "split_rows_per_phase": len(schema["split_suffixes"]),
            "coalesce_rows_per_phase": len(schema["coalesce_suffixes"]),
        },
        "bfs_fields": bfs["fields"],
        "bfs_field_order": bfs["field_order"],
        "pfs3_fields": pfs3["fields"],
        "pfs3_field_order": pfs3["field_order"],
        "bfs_legacy_fields": bfs["legacy_fields"],
        "bfs_legacy_field_order": bfs["legacy_field_order"],
        "bfs_legacy_non_time_counters_by_phase":
            bfs["legacy_non_time_counters_by_phase"],
        "path_matrix_by_phase": bfs["path_matrix"],
        "path_totals_by_phase": bfs["path_totals_by_phase"],
        "original_schema11_verifier": "PASS",
    }


def project_records(fields: dict[str, str], schema_version: str = "11") -> str:
    ordered: list[tuple[str, str]] = []
    for name, value in fields.items():
        if name == "FS_DEEP_COMPARE":
            ordered.append((name, schema_version))
        else:
            ordered.append((name, value))
    return "".join(f"{name}\t{value}\n" for name, value in ordered)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate a schema-12 split/coalesce attribution run."
    )
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--verifier", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = validate_run(args.run_dir.resolve(), args.header.resolve(),
                              args.verifier.resolve())
    except (ValidationError, OSError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
