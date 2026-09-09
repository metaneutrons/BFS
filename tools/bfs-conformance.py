#!/usr/bin/env python3
"""Deterministic BFS conformance orchestrator.

The catalog and replay parser deliberately know no BFS binary layout. Backend
programs own storage or mounted-path observations and return one JSON record.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "tests" / "conformance" / "scenarios.json"
REPLAY_FORMAT_VERSION = 1


class ConformanceError(Exception):
    """A malformed request or untrustworthy execution record."""


def load_catalog():
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if catalog.get("catalog_version") != 1:
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
        by_id[scenario_id] = scenario
    return catalog, by_id


def load_replay(path, known_ids):
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
       header.get("format_version") != REPLAY_FORMAT_VERSION:
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
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def invoke(program, scenario_id, seed, root):
    if not program.is_file() or not os.access(program, os.X_OK):
        return {"id": scenario_id, "status": "error", "code": "missing-backend"}
    command = [str(program), "--case", scenario_id, "--seed", str(seed)]
    if root is not None:
        command.extend(["--root", str(root)])
    try:
        completed = subprocess.run(command, capture_output=True, text=True, timeout=60,
                                   check=False)
    except subprocess.TimeoutExpired:
        return {"id": scenario_id, "status": "error", "code": "timeout"}
    if completed.returncode not in (0, 1, 2, 3):
        return {"id": scenario_id, "status": "error", "code": "backend-crash"}
    try:
        record = json.loads(completed.stdout)
    except json.JSONDecodeError:
        return {"id": scenario_id, "status": "error", "code": "invalid-backend-json"}
    if record.get("id") != scenario_id or record.get("status") not in {
        "pass", "fail", "skip", "error"
    }:
        return {"id": scenario_id, "status": "error", "code": "invalid-backend-record"}
    return record


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=("core", "posix"), required=True)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--seed", type=int, default=20260909)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--core-program", type=Path,
                        default=ROOT / "build" / "host" / "bfs-conformance-core")
    parser.add_argument("--posix-program", type=Path,
                        default=ROOT / "build" / "host" / "bfs-conformance-posix")
    args = parser.parse_args(argv)
    try:
        catalog, known_ids = load_catalog()
        replay_header = None
        if args.replay:
            replay_header, selected = load_replay(args.replay, known_ids)
        else:
            selected = args.cases or []
        if not selected:
            raise ConformanceError("no scenario selected")
        if len(set(selected)) != len(selected) or any(case not in known_ids for case in selected):
            raise ConformanceError("invalid scenario selection")
        if args.backend == "posix" and args.root is None:
            raise ConformanceError("mounted POSIX mode requires --root")
        program = args.core_program if args.backend == "core" else args.posix_program
        records = []
        for scenario_id in selected:
            scenario = known_ids[scenario_id]
            capability = "direct" if args.backend == "core" else "mounted"
            if not scenario[capability]:
                records.append({"id": scenario_id, "status": "skip", "code": "not-applicable"})
            else:
                records.append(invoke(program, scenario_id, args.seed, args.root))
        statuses = [record["status"] for record in records]
        status = "pass" if all(item == "pass" for item in statuses) else \
            "fail" if "fail" in statuses else "error" if "error" in statuses else "skip"
        result = {
            "format_version": 1,
            "catalog_version": catalog["catalog_version"],
            "replay": replay_header,
            "backend": args.backend,
            "seed": args.seed,
            "status": status,
            "records": records,
            "identity": {
                "git": git_identity(),
                "program_sha256": sha256_file(program) if program.is_file() else "missing",
                "platform": platform.platform(),
                "python": platform.python_version(),
            },
        }
    except ConformanceError as error:
        result = {"format_version": 1, "status": "error", "code": str(error)}
    output = json.dumps(result, sort_keys=True, separators=(",", ":"))
    print(output)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    return {"pass": 0, "fail": 1, "skip": 2}.get(result["status"], 3)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
