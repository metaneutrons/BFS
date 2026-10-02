#!/usr/bin/env python3
"""Synthetic schema-12 acceptance/rejection fixtures for consume.py."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import consume


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_records(path: Path, records: list[tuple[str, str]]) -> None:
    path.write_text(
        "".join(f"{name}\t{value}\n" for name, value in records),
        encoding="ascii",
    )


def read_records(path: Path) -> list[tuple[str, str]]:
    _, records = consume.read_tsv(path)
    return records


def make_synthetic_run(template_results: Path, destination: Path,
                       schema: dict) -> None:
    results = destination / "system" / "Results"
    results.mkdir(parents=True)
    for evidence in ("complete.txt", "info-after-format.txt"):
        source = template_results / evidence
        if not source.is_file():
            raise RuntimeError(f"template is missing {evidence}")
        shutil.copy2(source, results / evidence)

    base_bfs = read_records(template_results / "bfs.deep-compare.tsv")
    base_pfs3 = read_records(template_results / "pfs3.deep-compare.tsv")
    if base_bfs[0] != ("FS_DEEP_COMPARE", "11") or base_pfs3[0] != (
        "FS_DEEP_COMPARE", "11"
    ):
        raise RuntimeError("fixture template must be a valid schema-11 run")

    bfs_records = [("FS_DEEP_COMPARE", "12"), base_bfs[1]]
    cursor = 2
    for phase in schema["phases"]:
        phase_records: list[tuple[str, str]] = []
        while cursor < len(base_bfs) - 1 and base_bfs[cursor][0].startswith(phase + "_"):
            phase_records.append(base_bfs[cursor])
            cursor += 1
        if not phase_records:
            raise RuntimeError(f"schema-11 fixture has no phase {phase}")
        bfs_records.extend(phase_records)
        counts = {suffix: 0 for suffix in
                  schema["split_suffixes"] + schema["coalesce_suffixes"]}
        counts.update({
            "SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT": 2,
            "SPLIT_LEAF_FREE_TREE_ROOT_INITIAL_WRITE": 2,
            "SPLIT_LEAF_FREE_TREE_ROOT_RIGHT_SELECT": 1,
            "SPLIT_LEAF_FREE_TREE_ROOT_RIGHT_READ": 1,
            "SPLIT_LEAF_FREE_TREE_ROOT_RIGHT_WRITE": 1,
            "SPLIT_INTERNAL_DIR_TREE_DEEP_ATTEMPT": 1,
            "SPLIT_INTERNAL_DIR_TREE_DEEP_INITIAL_WRITE": 1,
            "COALESCE_TWO_SIDED_FREE_TREE_ROOT_ATTEMPT": 2,
            "COALESCE_TWO_SIDED_FREE_TREE_ROOT_COMPLETE": 2,
            "COALESCE_RIGHT_ONLY_INODE_TREE_DEEP_ATTEMPT": 1,
            "COALESCE_RIGHT_ONLY_INODE_TREE_DEEP_COMPLETE": 1,
        })
        for suffix in schema["split_suffixes"] + schema["coalesce_suffixes"]:
            bfs_records.append((f"{phase}_{suffix}", str(counts[suffix])))
    bfs_records.extend(base_bfs[cursor:])
    write_records(results / "bfs.deep-compare.tsv", bfs_records)

    pfs3_records = list(base_pfs3)
    pfs3_records[0] = ("FS_DEEP_COMPARE", "12")
    write_records(results / "pfs3.deep-compare.tsv", pfs3_records)


def copy_fixture(source: Path, destination: Path, name: str) -> Path:
    target = destination / name
    shutil.copytree(source, target)
    return target


def edit_tsv(run: Path, filesystem: str, edit) -> None:
    path = run / "system" / "Results" / f"{filesystem}.deep-compare.tsv"
    records = read_records(path)
    edit(records)
    write_records(path, records)


def find_row(records: list[tuple[str, str]], suffix: str) -> int:
    for index, (name, _) in enumerate(records):
        if name.endswith(suffix):
            return index
    raise RuntimeError(f"synthetic field not found: {suffix}")


def invoke_consumer(consumer_path: Path, run: Path,
                    header: Path, verifier: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            str(consumer_path),
            str(run),
            "--header",
            str(header),
            "--verifier",
            str(verifier),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--verifier", required=True, type=Path)
    parser.add_argument("--template-results", required=True, type=Path)
    args = parser.parse_args()

    consumer_path = Path(__file__).with_name("consume.py").resolve()
    header = args.header.resolve()
    verifier = args.verifier.resolve()
    template = args.template_results.resolve()
    owned_dir = Path(__file__).resolve().parent
    schema = consume.load_schema(header, verifier)
    identities = {
        "header": sha256_file(header),
        "guest": sha256_file(schema["guest_path"]),
        "verifier": sha256_file(verifier),
        "template_bfs": sha256_file(template / "bfs.deep-compare.tsv"),
        "template_pfs3": sha256_file(template / "pfs3.deep-compare.tsv"),
        "consumer": sha256_file(consumer_path),
        "fixture_harness": sha256_file(Path(__file__).resolve()),
    }

    rows: list[tuple[str, Path, str | None]] = []
    with tempfile.TemporaryDirectory(prefix=".synthetic-schema12-", dir=owned_dir) as temp:
        root = Path(temp)
        valid = root / "valid"
        make_synthetic_run(template, valid, schema)
        rows.append(("valid_acceptance", valid, None))

        missing = copy_fixture(valid, root, "missing_path_row")
        edit_tsv(
            missing, "bfs",
            lambda records: records.pop(find_row(
                records, "SPLIT_LEAF_FREE_TREE_ROOT_RIGHT_WRITE"
            )),
        )
        rows.append(("missing_path_row", missing, "missing, reordered, or unknown path rows"))

        reordered = copy_fixture(valid, root, "reordered_path_rows")
        def swap_path_rows(records):
            first = find_row(records, "SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT")
            second = find_row(records, "SPLIT_LEAF_FREE_TREE_ROOT_INITIAL_WRITE")
            records[first], records[second] = records[second], records[first]
        edit_tsv(reordered, "bfs", swap_path_rows)
        rows.append(("reordered_path_rows", reordered, "missing, reordered, or unknown path rows"))

        duplicate = copy_fixture(valid, root, "duplicate_path_row")
        def add_duplicate(records):
            index = find_row(records, "SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT")
            records.insert(index + 1, records[index])
        edit_tsv(duplicate, "bfs", add_duplicate)
        rows.append(("duplicate_path_row", duplicate, "duplicates field"))

        bad_bfs_version = copy_fixture(valid, root, "wrong_bfs_schema_version")
        edit_tsv(bad_bfs_version, "bfs",
                 lambda records: records.__setitem__(0, ("FS_DEEP_COMPARE", "13")))
        rows.append(("wrong_bfs_schema_version", bad_bfs_version, "schema 12"))

        bad_pfs_version = copy_fixture(valid, root, "wrong_pfs_schema_version")
        edit_tsv(bad_pfs_version, "pfs3",
                 lambda records: records.__setitem__(0, ("FS_DEEP_COMPARE", "11")))
        rows.append(("wrong_pfs_schema_version", bad_pfs_version, "schema 12"))

        overflow = copy_fixture(valid, root, "path_u32_overflow")
        edit_tsv(
            overflow, "bfs",
            lambda records: records.__setitem__(
                find_row(records, "SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT"),
                (records[find_row(records, "SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT")][0],
                 str(1 << 32)),
            ),
        )
        rows.append(("path_u32_overflow", overflow, "unsigned 32-bit"))

        bad_split_chain = copy_fixture(valid, root, "split_chain_mismatch")
        def break_split_chain(records):
            index = find_row(records, "SPLIT_LEAF_FREE_TREE_ROOT_RIGHT_READ")
            name, _ = records[index]
            records[index] = (name, "0")
        edit_tsv(bad_split_chain, "bfs", break_split_chain)
        rows.append(("split_chain_mismatch", bad_split_chain, "right select/read/rewrite"))

        bad_coalesce = copy_fixture(valid, root, "coalesce_completion_mismatch")
        def break_coalesce(records):
            index = find_row(records, "COALESCE_TWO_SIDED_FREE_TREE_ROOT_COMPLETE")
            name, _ = records[index]
            records[index] = (name, "1")
        edit_tsv(bad_coalesce, "bfs", break_coalesce)
        rows.append(("coalesce_completion_mismatch", bad_coalesce, "attempt/complete"))

        pfs_counter = copy_fixture(valid, root, "pfs3_forbidden_counter")
        def add_pfs_counter(records):
            records.insert(-1, ("SMALL_CREATE_40_SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT", "0"))
        edit_tsv(pfs_counter, "pfs3", add_pfs_counter)
        rows.append(("pfs3_forbidden_counter", pfs_counter, "exactly the six elapsed rows"))

        bad_marker = copy_fixture(valid, root, "bad_completion_marker")
        (bad_marker / "system" / "Results" / "complete.txt").write_text(
            "BFS-PFS3-COMPLETE\n", encoding="ascii"
        )
        rows.append(("bad_completion_marker", bad_marker, "completion marker"))

        missing_legacy = copy_fixture(valid, root, "missing_legacy_counter")
        edit_tsv(
            missing_legacy, "bfs",
            lambda records: records.pop(find_row(records, "SMALL_CREATE_40_BIO_READS")),
        )
        rows.append(("missing_legacy_counter", missing_legacy,
                    "original schema-11 verifier rejected projection"))

        unknown_legacy = copy_fixture(valid, root, "unknown_legacy_field")
        def add_unknown_legacy(records):
            index = find_row(records, "SMALL_CREATE_40_SPLIT_LEAF_FREE_TREE_ROOT_ATTEMPT")
            records.insert(index, ("SMALL_CREATE_40_UNEXPECTED_COUNTER", "1"))
        edit_tsv(unknown_legacy, "bfs", add_unknown_legacy)
        rows.append(("unknown_legacy_field", unknown_legacy,
                    "original schema-11 verifier rejected projection"))

        cpu_partition = copy_fixture(valid, root, "legacy_cpu_partition_mismatch")
        def break_cpu_partition(records):
            index = find_row(records, "SMALL_CREATE_40_PACKET_CALLS")
            name, _ = records[index]
            records[index] = (name, "119")
        edit_tsv(cpu_partition, "bfs", break_cpu_partition)
        rows.append(("legacy_cpu_partition_mismatch", cpu_partition,
                    "original schema-11 verifier rejected projection"))

        elapsed_overflow = copy_fixture(valid, root, "elapsed_u32_overflow")
        def overflow_elapsed(records):
            index = find_row(records, "SMALL_CREATE_40_US")
            records[index] = (records[index][0], str(1 << 32))
        edit_tsv(elapsed_overflow, "bfs", overflow_elapsed)
        rows.append(("elapsed_u32_overflow", elapsed_overflow, "unsigned 32-bit"))

        tick_overflow = copy_fixture(valid, root, "tick_u64_overflow")
        def overflow_tick(records):
            index = find_row(records, "SMALL_CREATE_40_READ_TICKS")
            records[index] = (records[index][0], str(1 << 64))
        edit_tsv(tick_overflow, "bfs", overflow_tick)
        rows.append(("tick_u64_overflow", tick_overflow, "unsigned 64-bit"))

        clock_pair_overflow = copy_fixture(valid, root, "clock_pair_u32_overflow")
        def overflow_clock_pair(records):
            index = find_row(records, "SMALL_CREATE_40_CLOCK_PAIR_TICKS")
            records[index] = (records[index][0], str(1 << 32))
        edit_tsv(clock_pair_overflow, "bfs", overflow_clock_pair)
        rows.append(("clock_pair_u32_overflow", clock_pair_overflow, "unsigned 32-bit"))

        bad_clock = copy_fixture(valid, root, "zero_clock")
        def zero_clock(records):
            index = find_row(records, "CLOCK_HZ")
            records[index] = ("CLOCK_HZ", "0")
        edit_tsv(bad_clock, "bfs", zero_clock)
        rows.append(("zero_clock", bad_clock, "CLOCK_HZ must be positive"))

        bad_stride = copy_fixture(valid, root, "wrong_sample_stride")
        def wrong_stride(records):
            index = find_row(records, "CRC_SAMPLE_STRIDE")
            records[index] = ("CRC_SAMPLE_STRIDE", "32")
        edit_tsv(bad_stride, "bfs", wrong_stride)
        rows.append(("wrong_sample_stride", bad_stride, "incorrect CRC_SAMPLE_STRIDE"))

        missing_pass = copy_fixture(valid, root, "missing_pass_row")
        edit_tsv(missing_pass, "bfs", lambda records: records.pop())
        rows.append(("missing_pass_row", missing_pass, "terminal PASS=1"))

        bad_inventory = copy_fixture(valid, root, "bad_inventory")
        (bad_inventory / "system" / "Results" / "info-after-format.txt").write_text(
            "DH1 volume absent\n", encoding="ascii"
        )
        rows.append(("bad_inventory", bad_inventory, "volume was not mounted"))

        results: list[dict] = []
        failures = 0
        for label, run, expected_error in rows:
            completed = invoke_consumer(consumer_path, run, header, verifier)
            if expected_error is None:
                if completed.returncode != 0:
                    failures += 1
                    results.append({
                        "case": label,
                        "expected": "accept",
                        "result": "FAIL",
                        "detail": completed.stderr.strip(),
                    })
                    continue
                try:
                    output = json.loads(completed.stdout)
                except json.JSONDecodeError as error:
                    failures += 1
                    results.append({
                        "case": label,
                        "expected": "JSON",
                        "result": "FAIL",
                        "detail": str(error),
                    })
                    continue
                leaf_path = output["path_matrix_by_phase"]["SMALL_CREATE_40"][
                    "split"
                ]["LEAF"]["FREE_TREE"]["ROOT"]
                if (
                    output["schema_version"] != 12
                    or output["probe_abi"] != 13
                    or output["dimensions"]["split_rows_per_phase"] != 100
                    or output["dimensions"]["coalesce_rows_per_phase"] != 40
                    or leaf_path["ATTEMPT"] != "2"
                    or leaf_path["RIGHT_SELECT"] != "1"
                    or output["path_totals_by_phase"]["SMALL_CREATE_40"]["split"][
                        "by_event"
                    ]["ATTEMPT"] != "3"
                    or any(("SPLIT_" in field or "COALESCE_" in field) for field in
                           output["bfs_legacy_non_time_counters_by_phase"][
                               "SMALL_CREATE_40"
                           ])
                    or any(field.endswith("_US") or field.endswith("_TICKS")
                           for field in output[
                               "bfs_legacy_non_time_counters_by_phase"
                           ]["SMALL_CREATE_40"])
                ):
                    failures += 1
                    results.append({
                        "case": label,
                        "expected": "parsed field/matrix/totals",
                        "result": "FAIL",
                        "detail": "machine-readable output oracle mismatch",
                    })
                else:
                    results.append({
                        "case": label,
                        "expected": "accept",
                        "result": "PASS",
                        "detail": "schema, projections, matrix, aggregates",
                    })
            else:
                if completed.returncode == 0:
                    failures += 1
                    results.append({
                        "case": label,
                        "expected": f"reject: {expected_error}",
                        "result": "FAIL",
                        "detail": "consumer accepted invalid fixture",
                    })
                elif expected_error not in completed.stderr:
                    failures += 1
                    results.append({
                        "case": label,
                        "expected": f"reject: {expected_error}",
                        "result": "FAIL",
                        "detail": completed.stderr.strip(),
                    })
                else:
                    results.append({
                        "case": label,
                        "expected": f"reject: {expected_error}",
                        "result": "PASS",
                        "detail": completed.stderr.strip().splitlines()[-1],
                    })

    report = {
        "cases": len(rows),
        "passed": len(rows) - failures,
        "failed": failures,
        "source_sha256": identities,
        "results": results,
    }
    log_path = owned_dir / "fixture-run.log"
    log_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    print(f"LOG: {log_path}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
