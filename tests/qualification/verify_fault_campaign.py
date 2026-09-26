#!/usr/bin/env python3
"""Verify retained evidence from the bounded #28 image fault campaign."""

import argparse
import json
from pathlib import Path
import sys

import fault_campaign


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_json(path, description):
    fault_campaign.regular_file(path, description)
    try:
        return json.loads(path.read_text(encoding="ascii"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid {description}: {error}") from error


def verify(output):
    output = Path(output).resolve()
    fault_campaign.directory(output, "campaign evidence directory")
    require(not output.is_symlink(), "campaign evidence directory must not be a symlink")
    manifest_path = output / "manifest.json"
    result_path = output / "result.json"
    records_path = output / "cases.jsonl"
    manifest = fault_campaign.load_manifest(manifest_path)
    result = load_json(result_path, "campaign result")
    fault_campaign.regular_file(records_path, "campaign case record")
    lines = records_path.read_text(encoding="ascii").splitlines()
    require(lines, "campaign case record is empty")
    try:
        records = [json.loads(line) for line in lines]
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid campaign case record: {error}") from error
    require(result.get("format_version") == 1, "unsupported campaign result format")
    require(result.get("suite_version") == manifest["suite_version"],
            "campaign result suite version does not match manifest")
    require(result.get("qualification_scope") == "bounded-modelled-image-and-adapter-campaign",
            "campaign scope is not declared")
    require(result.get("unqualified_scopes") ==
            ["mounted-fuse-surface", "physical-power-controller-media"],
            "campaign claim boundary changed")
    require(result.get("case_count") == len(records) == len(manifest["cases"]),
            "campaign case count is incomplete")
    require(result.get("manifest_sha256") == fault_campaign.digest(manifest_path),
            "campaign manifest digest does not match")
    require(result.get("binary_hashes", {}).get("manifest") == result.get("manifest_sha256"),
            "campaign recorded-manifest identity does not match")
    require(result.get("cases_sha256") == fault_campaign.digest(records_path),
            "campaign case-record digest does not match")
    evidence = dict(result)
    supplied_evidence_digest = evidence.pop("evidence_sha256", None)
    require(supplied_evidence_digest == fault_campaign.digest_bytes(fault_campaign.canonical(evidence)),
            "campaign evidence summary digest does not match")
    expected_cases = manifest["cases"]
    for expected, record in zip(expected_cases, records):
        identifier = expected["case_id"]
        require(record.get("case_id") == identifier, f"campaign case order changed: {identifier}")
        require(record.get("suite_version") == manifest["suite_version"],
                f"campaign case suite version changed: {identifier}")
        for field in ("family", "scenario", "seed", "fault_model", "fault_point", "mutation_location",
                      "expected_class", "expected_states", "timeout_seconds"):
            require(record.get(field) == expected[field], f"campaign case contract changed: {identifier}.{field}")
        require(record.get("observer_commands") == expected["observers"],
                f"campaign case contract changed: {identifier}.observers")
        require(record.get("source_commit") == result.get("source_commit"),
                f"campaign source identity changed: {identifier}")
        require(record.get("binary_hashes") == result.get("binary_hashes"),
                f"campaign binary identity changed: {identifier}")
        require(isinstance(record.get("commands"), list) and record["commands"],
                f"campaign commands are missing: {identifier}")
        for command in record["commands"]:
            require(isinstance(command.get("command"), list) and command["command"],
                    f"campaign command is invalid: {identifier}")
            require(isinstance(command.get("returncode"), int),
                    f"campaign command result is invalid: {identifier}")
            require(len(command.get("stdout_sha256", "")) == 64 and
                    len(command.get("stderr_sha256", "")) == 64,
                    f"campaign command digest is invalid: {identifier}")
        if record.get("result") == "passed":
            require(record.get("observed_class") == expected["expected_class"],
                    f"campaign case outcome mismatch: {identifier}")
            case_directory = output / "cases" / identifier
            fault_campaign.path_within(case_directory, output)
            baseline = case_directory / "baseline.bfs"
            image = case_directory / "image.bfs"
            fault_campaign.regular_file(baseline, "campaign baseline")
            fault_campaign.regular_file(image, "campaign final image")
            require(record.get("baseline_sha256") == fault_campaign.digest(baseline),
                    f"campaign baseline digest does not match: {identifier}")
            require(record.get("final_sha256") == fault_campaign.digest(image),
                    f"campaign final-image digest does not match: {identifier}")
    expected_status = "passed" if all(record.get("result") == "passed" for record in records) else "failed"
    require(result.get("status") == expected_status, "campaign status does not match case records")
    return {"cases": len(records), "qualified": result["status"] == "passed",
            "status": result["status"]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        result = verify(arguments.output)
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
    print(json.dumps(result, sort_keys=True))
    return 0 if result["qualified"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
