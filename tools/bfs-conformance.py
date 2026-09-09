#!/usr/bin/env python3
"""Deterministic BFS conformance orchestrator.

The catalog and replay parser deliberately know no BFS binary layout. Backend
programs own storage or mounted-path observations and return one JSON record.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import sys

from bfs_command_runner import CommandTimeout, run_command


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "tests" / "conformance" / "scenarios.json"
REPLAY_FORMAT_VERSION = 1
EXIT_PASS, EXIT_FAIL, EXIT_SKIP, EXIT_ERROR = range(4)


class ConformanceError(Exception):
    """A malformed request or untrustworthy execution record."""


def load_catalog():
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if catalog.get("catalog_version") != 1 or catalog.get("contract_version") != 1:
        raise ConformanceError("unsupported catalog version")
    scenarios = catalog.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        raise ConformanceError("catalog has no scenarios")
    by_id = {}
    for scenario in scenarios:
        scenario_id = scenario.get("id")
        if not isinstance(scenario_id, str) or not scenario_id or scenario_id in by_id:
            raise ConformanceError("catalog has an invalid or duplicate scenario id")
        if not isinstance(scenario.get("contract"), str):
            raise ConformanceError("catalog scenario has no contract id")
        amiga_test_ids = scenario.get("amiga_test_ids", [])
        if not isinstance(amiga_test_ids, list) or any(
            not isinstance(test_id, str) or not test_id for test_id in amiga_test_ids
        ) or len(set(amiga_test_ids)) != len(amiga_test_ids):
            raise ConformanceError("catalog scenario has invalid Amiga test ids")
        expected = scenario.get("expected")
        if not isinstance(expected, dict) or any(
            not isinstance(expected.get(backend), list) or
            not expected[backend] or any(status not in {"pass", "fail", "skip", "error"}
                                         for status in expected[backend])
            for backend in ("core", "posix")
        ):
            raise ConformanceError("catalog scenario has invalid expected outcomes")
        by_id[scenario_id] = scenario
    return catalog, by_id


def load_replay(path, known_ids, catalog_version):
    records = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ConformanceError(f"cannot read replay: {error}") from error
    if len(lines) < 2:
        raise ConformanceError("replay is incomplete")
    for line_number, line in enumerate(lines, 1):
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise ConformanceError(f"invalid replay JSON at line {line_number}") from error
    header = records[0]
    if header.get("type") != "bfs-conformance-replay" or \
       header.get("format_version") != REPLAY_FORMAT_VERSION or \
       header.get("catalog_version") != catalog_version:
        raise ConformanceError("unsupported replay header")
    selected = []
    for record in records[1:-1]:
        if record.get("type") != "case" or record.get("id") not in known_ids:
            raise ConformanceError("replay contains an unknown case")
        if record["id"] in selected:
            raise ConformanceError("replay selects a case twice")
        selected.append(record["id"])
    if records[-1] != {"type": "complete"}:
        raise ConformanceError("replay has no completion record")
    if not selected:
        raise ConformanceError("replay selects no cases")
    return header, selected


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_identity():
    try:
        result = run_command("git", ["-C", str(ROOT), "rev-parse", "HEAD"], 5)
        return result.stdout.strip() if result.returncode == EXIT_PASS else "unavailable"
    except (OSError, CommandTimeout):
        return "unavailable"


def invoke(backend, scenario_id, seed, root):
    arguments = ["--case", scenario_id, "--seed", str(seed)]
    if root is not None:
        arguments.extend(["--root", str(root)])
    try:
        completed = run_command(backend, arguments, 60)
    except OSError:
        return {"id": scenario_id, "status": "error", "code": "missing-backend"}
    except CommandTimeout:
        return {"id": scenario_id, "status": "error", "code": "timeout"}
    if completed.returncode not in (0, 1, 2, 3):
        return {"id": scenario_id, "status": "error", "code": "backend-crash"}
    return validate_backend_result(completed, scenario_id)


def validate_backend_result(completed, scenario_id):
    """Accept only a result whose semantic status matches its process exit."""
    try:
        record = json.loads(completed.stdout)
    except json.JSONDecodeError:
        return {"id": scenario_id, "status": "error", "code": "invalid-backend-json"}
    if record.get("id") != scenario_id or record.get("status") not in {
        "pass", "fail", "skip", "error"
    }:
        return {"id": scenario_id, "status": "error", "code": "invalid-backend-record"}
    expected_exit = {
        "pass": EXIT_PASS, "fail": EXIT_FAIL, "skip": EXIT_SKIP, "error": EXIT_ERROR,
    }[record["status"]]
    if completed.returncode != expected_exit:
        return {"id": scenario_id, "status": "error", "code": "invalid-backend-exit"}
    return record


def enforce_scenario_contract(record, allowed_statuses):
    """Turn a well-formed but disallowed backend result into an error."""
    if record["status"] != "error" and record["status"] not in allowed_statuses:
        return {"id": record["id"], "status": "error", "code": "contract-mismatch"}
    return record


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=("core", "posix"), required=True)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--seed", type=int, default=20260909)
    parser.add_argument("--output", type=Path)
    return parser.parse_args(argv)


def select_scenarios(args, catalog, known_ids):
    replay_header = None
    if args.replay:
        replay_header, selected = load_replay(args.replay, known_ids, catalog["catalog_version"])
    else:
        selected = args.cases or []
    if not selected:
        raise ConformanceError("no scenario selected")
    if len(set(selected)) != len(selected) or any(case not in known_ids for case in selected):
        raise ConformanceError("invalid scenario selection")
    if args.backend == "posix" and args.root is None:
        raise ConformanceError("mounted POSIX mode requires --root")
    return replay_header, selected


def run_scenarios(args, known_ids, selected):
    program = ROOT / "build" / "host" / f"bfs-conformance-{args.backend}"
    capability = "direct" if args.backend == "core" else "mounted"
    records = []
    for scenario_id in selected:
        scenario = known_ids[scenario_id]
        if not scenario[capability]:
            record = {"id": scenario_id, "status": "skip", "code": "not-applicable"}
        else:
            record = invoke(args.backend, scenario_id, args.seed, args.root)
            record = enforce_scenario_contract(record, scenario["expected"][args.backend])
        record["contract"] = scenario["contract"]
        records.append(record)
    return program, records


def result_status(records):
    statuses = [record["status"] for record in records]
    return "pass" if all(item == "pass" for item in statuses) else \
        "fail" if "fail" in statuses else "error" if "error" in statuses else "skip"


def build_result(args, catalog, replay_header, program, records):
    return {
        "format_version": 1,
        "catalog_version": catalog["catalog_version"],
        "contract_version": catalog["contract_version"],
        "replay": replay_header,
        "backend": args.backend,
        "seed": args.seed,
        "status": result_status(records),
        "records": records,
        "identity": {
            "git": git_identity(),
            "program_sha256": sha256_file(program) if program.is_file() else "missing",
            "catalog_sha256": sha256_file(CATALOG_PATH),
            "replay_sha256": sha256_file(args.replay) if args.replay else "none",
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
    }


def main(argv):
    args = parse_arguments(argv)
    try:
        catalog, known_ids = load_catalog()
        replay_header, selected = select_scenarios(args, catalog, known_ids)
        program, records = run_scenarios(args, known_ids, selected)
        result = build_result(args, catalog, replay_header, program, records)
    except ConformanceError as error:
        result = {"format_version": 1, "status": "error", "code": str(error)}
    output = json.dumps(result, sort_keys=True, separators=(",", ":"))
    print(output)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    return {"pass": EXIT_PASS, "fail": EXIT_FAIL, "skip": EXIT_SKIP}.get(
        result["status"], EXIT_ERROR
    )


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
