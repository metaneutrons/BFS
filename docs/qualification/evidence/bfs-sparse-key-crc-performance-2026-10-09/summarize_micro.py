#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Strict validator and paired timing summary for the CRC sparse microprobe."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import re
import statistics
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Sequence


PROBE_SHA256 = "0b9a67a9554a1a4ef594f0bde18efbe6d2be5a63db662578eff61b58d41266a4"
CONFIG_SHA256S = (
    "f80278f782ded39e8190323fbf798608a5d202c42288d1b7cb31d54c60c35683",
    "b3f621690f58f88805a5a62563fe819ec33b2cf282148ba63587bd0c83e3fc04",
)
ROM_SHA256 = "68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c"
DONE_MARKER = b"SPARSE_CRC_PROBE_COMPLETE\n"
EXPECTED_VECTOR_COUNT = 1297
EXPECTED_ORACLE_DIGEST = 2555652288
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
MASK32 = 0xFFFFFFFF

META_ROWS = (
    "META\toracle\tindependent-bitwise-reflected-IEEE",
    "META\tapi\ttyped size_t adapters; explicit uint32_t conversion; 32-bit lengths",
)
FIXTURE_ROWS = (
    "FIXTURE\tname\tdescription",
    "FIXTURE\tsmall\tzero,dense,32-nonzero+224-zero per 256; lengths 0,1,15,16,17,31,32,63,64,65,66,127,128,129; offsets 0..7",
    "FIXTURE\tzero_run_boundary\tprefix byte; 63,64,65 supplied zero bytes; following byte included separately",
    "FIXTURE\tlarge\t4096-byte zero,dense,block256,dirkey264; seeds 00000000,FFFFFFFF,12345678; offsets 0..7",
    "FIXTURE\tdirkey_like_264\tsynthetic packed 264-byte key: parent4,hash4,nameLen1,four-char name; 251 zero tail bytes",
    "FIXTURE\tsingle_byte_perturbation\t4096 zero bytes with one A5 at offsets 0,63,64,65,255,256,2047,4095",
    "FIXTURE\tmixed128_384\tdensity control; 128 nonzero and 384 zero bytes per 512-byte group; not an inode fixture",
)

TIMING_CASES = (
    ("dense4096", 4096, 64, "dense"),
    ("zero4096", 4096, 64, "zero"),
    ("sparse4096-dirkey264", 4096, 64, "dirkey_like_264"),
    ("short64-dense", 64, 512, "dense"),
    ("mixed128-384", 512, 64, "mixed128_384"),
)
SAMPLE_COUNT = 6
TIMING_HEADER = "TIMING\tcase\tlength\trepeats\tsample\torder\tbaseline_ticks\tsparse_ticks"

COUNT_ROWS_BEFORE_CLOCK = (
    ("vector_cases", EXPECTED_VECTOR_COUNT),
    ("baseline_calls", EXPECTED_VECTOR_COUNT),
    ("sparse_calls", EXPECTED_VECTOR_COUNT),
    ("abi_positive_checks", EXPECTED_VECTOR_COUNT * 2),
    ("abi_negative_control_cases", 2),
    ("abi_negative_control_passes", 2),
    ("abi_negative_control_errors", 0),
    ("oracle_errors", 0),
    ("abi_errors", 0),
    ("timing_errors", 0),
    ("errors", 0),
    ("timer_available", 1),
)
COUNT_ROWS_AFTER_CLOCK = (
    ("oracle_digest_fnv1a32", EXPECTED_ORACLE_DIGEST),
    ("timing_cases", 5),
    ("timing_samples", 30),
    ("baseline_timing_calls", 4608),
    ("sparse_timing_calls", 4608),
    ("timing_order_baseline_first", 15),
    ("timing_order_sparse_first", 15),
)


class ValidationError(ValueError):
    """Raised when a run does not exactly satisfy the probe protocol."""


def dense_byte(index: int) -> int:
    return (((index * 73 + (index >> 4) * 19 + 0x5B) & 0xFF) | 1) & 0xFF


def pattern_bytes(pattern: str, length: int) -> bytes:
    result = bytearray(length)
    for index in range(length):
        if pattern == "zero":
            value = 0
        elif pattern == "dense":
            value = dense_byte(index)
        elif pattern == "block32_224":
            value = dense_byte(index) if index % 256 < 32 else 0
        elif pattern == "dirkey_like_264":
            value = dense_byte(index) if index % 264 < 13 else 0
        elif pattern == "mixed128_384":
            value = dense_byte(index) if index % 512 < 128 else 0
        else:
            raise ValueError(f"unknown fixture pattern: {pattern}")
        result[index] = value
    return bytes(result)


def crc32_bitwise(initial: int, data: bytes) -> int:
    crc = (~initial) & MASK32
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & MASK32


def iter_oracle_vectors() -> Iterable[tuple[str, bytes, int, int, int]]:
    seeds = (0x00000000, 0xFFFFFFFF, 0x12345678)
    short_lengths = (0, 1, 15, 16, 17, 31, 32, 63, 64, 65, 66, 127, 128, 129)

    for pattern in ("zero", "dense", "block32_224"):
        for length in short_lengths:
            base = pattern_bytes(pattern, length)
            for offset in range(8):
                for seed in seeds:
                    yield pattern, base, length, offset, seed

    for offset in range(8):
        for run in (63, 64, 65):
            data = bytearray(67)
            data[0] = 0xA5
            data[run + 1] = 0x5A
            for seed in seeds:
                yield "zero_run_boundary", bytes(data), run + 1, offset, seed
                yield "zero_run_boundary", bytes(data), run + 2, offset, seed

    for pattern in ("zero", "dense", "block32_224", "dirkey_like_264"):
        data = pattern_bytes(pattern, 4096)
        for offset in range(8):
            for seed in seeds:
                yield pattern, data, 4096, offset, seed

    data = pattern_bytes("mixed128_384", 512)
    for offset in range(8):
        for seed in seeds:
            yield "mixed128_384_control", data, 512, offset, seed

    for position in (0, 63, 64, 65, 255, 256, 2047, 4095):
        data = bytearray(4096)
        data[position] = 0xA5
        for seed in seeds:
            yield "single_byte_perturbation", bytes(data), 4096, 0, seed

    yield "known_vector", b"123456789", 9, 0, 0


def regenerate_oracle_digest() -> tuple[int, int]:
    known = b"123456789"
    if binascii.crc32(known) & MASK32 != 0xCBF43926:
        raise ValidationError("binascii CRC32 known-vector check failed")
    if crc32_bitwise(0, known) != 0xCBF43926:
        raise ValidationError("bitwise CRC32 known-vector check failed")

    digest = FNV_OFFSET
    case_count = 0

    def digest_byte(value: int) -> None:
        nonlocal digest
        digest = ((digest ^ value) * FNV_PRIME) & MASK32

    def digest_u32(value: int) -> None:
        for shift in (0, 8, 16, 24):
            digest_byte((value >> shift) & 0xFF)

    for _fixture, data, length, offset, seed in iter_oracle_vectors():
        expected = binascii.crc32(data[:length], seed) & MASK32
        if expected != crc32_bitwise(seed, data[:length]):
            raise ValidationError("bitwise and binascii CRC32 oracles disagree")
        digest_u32(length)
        digest_u32(offset)
        digest_u32(seed)
        for value in data[:length]:
            digest_byte(value)
        digest_u32(expected)
        case_count += 1

    return case_count, digest


def _parse_uint(text: str, where: str, maximum: int = MASK32) -> int:
    if not re.fullmatch(r"0|[1-9][0-9]*", text):
        raise ValidationError(f"{where}: expected canonical unsigned decimal")
    value = int(text)
    if value > maximum:
        raise ValidationError(f"{where}: integer is out of range")
    return value


def _parse_hash_file(path: Path, run_index: int) -> dict[str, str]:
    try:
        text = path.read_bytes().decode("ascii")
    except OSError as error:
        raise ValidationError(f"run {run_index}: cannot read {path.name}: {error}") from error
    except UnicodeDecodeError as error:
        raise ValidationError(f"run {run_index}: {path.name} is not ASCII") from error
    if not text.endswith("\n") or "\r" in text:
        raise ValidationError(f"run {run_index}: malformed inputs.sha256 line endings")

    rows = text[:-1].split("\n")
    if len(rows) != 3 or any(not row for row in rows):
        raise ValidationError(f"run {run_index}: inputs.sha256 must contain exactly three entries")

    expected_suffixes = (
        ("probe", ("system", "C", "crc32-sparse-probe")),
        ("config", ("crc32.fs-uae",)),
        ("rom", ("kick.a1200.47.102.rom",)),
    )
    result: dict[str, str] = {}
    for row, (kind, suffix) in zip(rows, expected_suffixes):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", row)
        if not match:
            raise ValidationError(f"run {run_index}: malformed inputs.sha256 row")
        digest, recorded_path = match.groups()
        parts = PurePosixPath(recorded_path).parts
        if len(parts) < len(suffix) or tuple(parts[-len(suffix):]) != suffix:
            raise ValidationError(f"run {run_index}: wrong {kind} path in inputs.sha256")
        result[kind] = digest

    pinned = {
        "probe": PROBE_SHA256,
        "config": CONFIG_SHA256S[run_index - 1],
        "rom": ROM_SHA256,
    }
    for kind, expected in pinned.items():
        if result[kind] != expected:
            raise ValidationError(f"run {run_index}: wrong {kind} SHA-256 in inputs.sha256")
    return result


def _expect_line(lines: Sequence[str], index: int, expected: str, context: str) -> int:
    if index >= len(lines) or lines[index] != expected:
        actual = "<missing>" if index >= len(lines) else repr(lines[index])
        raise ValidationError(f"{context}: expected {expected!r}, got {actual}")
    return index + 1


def _parse_timing_row(line: str, context: str) -> dict[str, Any]:
    fields = line.split("\t")
    if len(fields) != 8 or fields[0] != "TIMING":
        raise ValidationError(f"{context}: malformed TIMING row")
    _, case, length_text, repeats_text, sample_text, order, baseline_text, sparse_text = fields
    length = _parse_uint(length_text, f"{context} length")
    repeats = _parse_uint(repeats_text, f"{context} repeats")
    sample = _parse_uint(sample_text, f"{context} sample")
    baseline = _parse_uint(baseline_text, f"{context} baseline_ticks", (1 << 64) - 1)
    sparse = _parse_uint(sparse_text, f"{context} sparse_ticks", (1 << 64) - 1)
    if baseline == 0 or sparse == 0:
        raise ValidationError(f"{context}: timing ticks must be positive")
    return {
        "case": case,
        "length": length,
        "repeats": repeats,
        "sample": sample,
        "order": order,
        "baseline_ticks": baseline,
        "sparse_ticks": sparse,
    }


def validate_report_text(text: str, run_index: int) -> dict[str, Any]:
    if not text.endswith("\n") or "\r" in text:
        raise ValidationError(f"run {run_index}: report must use LF and end with LF")
    lines = text[:-1].split("\n")
    if any(not line or not line.isascii() for line in lines):
        raise ValidationError(f"run {run_index}: report has an empty or non-ASCII line")

    index = 0
    index = _expect_line(lines, index, "SPARSE_CRC_PROBE\t2", f"run {run_index}")
    for row in META_ROWS + FIXTURE_ROWS:
        index = _expect_line(lines, index, row, f"run {run_index} inventory")

    counts: dict[str, int] = {}
    for name, expected in COUNT_ROWS_BEFORE_CLOCK:
        line = lines[index] if index < len(lines) else ""
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] != "COUNT" or fields[1] != name:
            raise ValidationError(f"run {run_index}: expected COUNT {name}")
        value = _parse_uint(fields[2], f"run {run_index} COUNT {name}")
        if value != expected:
            raise ValidationError(f"run {run_index}: COUNT {name} is {value}, expected {expected}")
        counts[name] = value
        index += 1

    if index >= len(lines) or not lines[index].startswith("CLOCK_HZ\t"):
        raise ValidationError(f"run {run_index}: missing CLOCK_HZ")
    clock_fields = lines[index].split("\t")
    if len(clock_fields) != 2 or clock_fields[0] != "CLOCK_HZ":
        raise ValidationError(f"run {run_index}: malformed CLOCK_HZ")
    clock_hz = _parse_uint(clock_fields[1], f"run {run_index} CLOCK_HZ")
    if clock_hz == 0:
        raise ValidationError(f"run {run_index}: CLOCK_HZ must be positive")
    index += 1

    for name, expected in COUNT_ROWS_AFTER_CLOCK:
        line = lines[index] if index < len(lines) else ""
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] != "COUNT" or fields[1] != name:
            raise ValidationError(f"run {run_index}: expected COUNT {name}")
        value = _parse_uint(fields[2], f"run {run_index} COUNT {name}")
        if value != expected:
            raise ValidationError(f"run {run_index}: COUNT {name} is {value}, expected {expected}")
        counts[name] = value
        index += 1

    index = _expect_line(lines, index, TIMING_HEADER, f"run {run_index} timing header")
    timing_rows: list[dict[str, Any]] = []
    for case_name, expected_length, expected_repeats, _pattern in TIMING_CASES:
        for sample in range(SAMPLE_COUNT):
            if index >= len(lines):
                raise ValidationError(f"run {run_index}: missing timing row {case_name}/{sample}")
            row = _parse_timing_row(lines[index], f"run {run_index} {case_name}/{sample}")
            expected_order = "baseline-first" if sample % 2 == 0 else "sparse-first"
            expected = (case_name, expected_length, expected_repeats, sample, expected_order)
            actual = (row["case"], row["length"], row["repeats"], row["sample"], row["order"])
            if actual != expected:
                raise ValidationError(
                    f"run {run_index}: timing row mismatch at position {len(timing_rows)}; "
                    f"got {actual!r}, expected {expected!r}"
                )
            timing_rows.append(row)
            index += 1

    index = _expect_line(lines, index, "STATUS\tPASS", f"run {run_index} status")
    if index != len(lines):
        raise ValidationError(f"run {run_index}: unexpected trailing report rows")

    vector_count, digest = regenerate_oracle_digest()
    if vector_count != EXPECTED_VECTOR_COUNT or digest != EXPECTED_ORACLE_DIGEST:
        raise ValidationError(
            "internal oracle inventory mismatch: "
            f"{vector_count} vectors, digest {digest}; expected "
            f"{EXPECTED_VECTOR_COUNT}, {EXPECTED_ORACLE_DIGEST}"
        )
    counts["clock_hz"] = clock_hz
    return {"counts": counts, "timing_rows": timing_rows, "oracle_digest": digest}


def _read_run(run_dir: Path, run_index: int) -> dict[str, Any]:
    if not run_dir.is_dir():
        raise ValidationError(f"run {run_index}: directory does not exist: {run_dir}")
    marker_path = run_dir / "system" / "Results" / "crc32-sparse-probe.done"
    report_path = run_dir / "system" / "Results" / "crc32-sparse-probe.tsv"
    try:
        marker = marker_path.read_bytes()
    except OSError as error:
        raise ValidationError(f"run {run_index}: missing completion marker: {error}") from error
    if marker != DONE_MARKER:
        raise ValidationError(f"run {run_index}: incorrect completion marker")

    config_path = run_dir / "crc32.fs-uae"
    try:
        config_hash = hashlib.sha256(config_path.read_bytes()).hexdigest()
    except OSError as error:
        raise ValidationError(
            f"run {run_index}: cannot read retained config {config_path.name}: {error}"
        ) from error
    if config_hash != CONFIG_SHA256S[run_index - 1]:
        raise ValidationError(f"run {run_index}: retained config SHA-256 mismatch")

    identities = _parse_hash_file(run_dir / "inputs.sha256", run_index)
    try:
        report = report_path.read_bytes().decode("ascii")
    except OSError as error:
        raise ValidationError(f"run {run_index}: cannot read report: {error}") from error
    except UnicodeDecodeError as error:
        raise ValidationError(f"run {run_index}: report is not ASCII") from error
    parsed = validate_report_text(report, run_index)
    return {
        "run": run_index,
        "name": run_dir.name,
        "path": str(run_dir.resolve()),
        "identities": identities,
        **parsed,
    }


def _case_statistics(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    baseline = [row["baseline_ticks"] for row in rows]
    sparse = [row["sparse_ticks"] for row in rows]
    deltas = [sparse_tick - baseline_tick for baseline_tick, sparse_tick in zip(baseline, sparse)]
    ratios = [sparse_tick / baseline_tick for baseline_tick, sparse_tick in zip(baseline, sparse)]
    return {
        "pair_count": len(rows),
        "baseline_ticks": baseline,
        "sparse_ticks": sparse,
        "paired_delta_ticks": deltas,
        "sparse_over_baseline": ratios,
        "baseline_min_ticks": min(baseline),
        "baseline_median_ticks": statistics.median(baseline),
        "baseline_max_ticks": max(baseline),
        "sparse_min_ticks": min(sparse),
        "sparse_median_ticks": statistics.median(sparse),
        "sparse_max_ticks": max(sparse),
        "delta_median_ticks": statistics.median(deltas),
        "delta_min_ticks": min(deltas),
        "delta_max_ticks": max(deltas),
        "ratio_median": statistics.median(ratios),
        "ratio_min": min(ratios),
        "ratio_max": max(ratios),
        "slower_pairs": sum(sparse_tick > baseline_tick for baseline_tick, sparse_tick in zip(baseline, sparse)),
    }


def _case_summaries(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for case_name, _length, _repeats, _pattern in TIMING_CASES:
        case_rows = [row for row in rows if row["case"] == case_name]
        result[case_name] = _case_statistics(case_rows)
    return result


def summarize_runs(run_dirs: Sequence[Path]) -> dict[str, Any]:
    if len(run_dirs) != 2:
        raise ValidationError("exactly two independent run directories are required")
    resolved = [path.resolve() for path in run_dirs]
    if resolved[0] == resolved[1] or resolved[0] in resolved[1].parents or resolved[1] in resolved[0].parents:
        raise ValidationError("run directories must be distinct and non-nested")
    if CONFIG_SHA256S[0] == CONFIG_SHA256S[1]:
        raise ValidationError("pinned run configurations do not identify independent runs")

    runs = [_read_run(Path(path), index + 1) for index, path in enumerate(run_dirs)]
    for run in runs:
        run["case_summary"] = _case_summaries(run["timing_rows"])

    pooled_rows: list[dict[str, Any]] = []
    for run in runs:
        for row in run["timing_rows"]:
            pooled_rows.append({"run": run["name"], **row})

    return {
        "protocol": "SPARSE_CRC_PROBE/2",
        "identity": {
            "probe_sha256": PROBE_SHA256,
            "config_sha256s": list(CONFIG_SHA256S),
            "rom_sha256": ROM_SHA256,
        },
        "oracle": {
            "vector_cases": EXPECTED_VECTOR_COUNT,
            "fnv1a32": EXPECTED_ORACLE_DIGEST,
            "known_crc32_123456789": "CBF43926",
        },
        "runs": runs,
        "pooled": {
            "pair_count": len(pooled_rows),
            "timing_rows": pooled_rows,
            "case_summary": _case_summaries(pooled_rows),
        },
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dirs", nargs=2, type=Path, metavar="RUN_DIR")
    args = parser.parse_args(argv)
    try:
        summary = summarize_runs(args.run_dirs)
    except (OSError, ValidationError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    json.dump(summary, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
