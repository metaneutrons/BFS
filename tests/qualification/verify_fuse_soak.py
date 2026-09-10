#!/usr/bin/env python3
"""Verify the complete, durable record from an M7 FUSE soak."""

import argparse
import hashlib
import json
from pathlib import Path

import fuse_soak


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def positive_integer(value, name):
    require(isinstance(value, int) and not isinstance(value, bool) and value > 0,
            f"invalid {name}")


def nonnegative_number(value, name):
    require(isinstance(value, (int, float)) and not isinstance(value, bool) and value >= 0,
            f"invalid {name}")


def positive_number(value, name):
    nonnegative_number(value, name)
    require(value > 0, f"invalid {name}")


def load_json(path):
    try:
        value = json.loads(path.read_text(encoding="ascii"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read {path.name}: {error}") from error
    require(isinstance(value, dict), f"{path.name} must contain an object")
    return value


def load_events(path):
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except OSError as error:
        raise RuntimeError(f"cannot read {path.name}: {error}") from error
    require(lines, "events.jsonl is empty")
    events = []
    for line_number, line in enumerate(lines, start=1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            raise RuntimeError(f"invalid events.jsonl line {line_number}: {error}") from error
        require(isinstance(event, dict), f"events.jsonl line {line_number} is not an object")
        events.append(event)
    return events


def verify_event(event, cycle, limits):
    require(event.get("cycle") == cycle, "event cycles are not sequential")
    require(event.get("status") == "passed", f"cycle {cycle} did not pass")
    positive_integer(event.get("operations"), f"cycle {cycle} operations")
    positive_integer(event.get("pressure_writes"), f"cycle {cycle} pressure writes")
    nonnegative_number(event.get("elapsed_seconds"), f"cycle {cycle} elapsed time")
    positive_number(event.get("cycle_seconds"), f"cycle {cycle} cycle time")
    require(event.get("checks") == {check: "passed" for check in fuse_soak.CYCLE_CHECKS},
            f"cycle {cycle} checks are incomplete")
    resources = event.get("resources")
    require(isinstance(resources, dict), f"cycle {cycle} resources are missing")
    nonnegative_number(resources.get("rss_kib"), f"cycle {cycle} RSS")
    nonnegative_number(resources.get("open_descriptors"), f"cycle {cycle} descriptors")
    require(event["cycle_seconds"] <= limits["cycle_seconds"],
            f"cycle {cycle} exceeded its time limit")
    require(resources["rss_kib"] <= limits["maximum_rss_kib"], f"cycle {cycle} exceeded RSS limit")
    require(resources["open_descriptors"] <= limits["maximum_open_descriptors"],
            f"cycle {cycle} exceeded descriptor limit")


def verify_result(result):
    require(result.get("status") == "passed", "soak result did not pass")
    limits = result.get("limits")
    require(isinstance(limits, dict), "soak limits are missing")
    for field in ("target_duration_seconds", "cycle_seconds", "maximum_rss_kib",
                  "maximum_open_descriptors"):
        positive_integer(limits.get(field), f"limit {field}")
    require(limits["target_duration_seconds"] == 72 * 60 * 60,
            "M7 target duration is not 72 hours")
    preflight = result.get("preflight")
    require(isinstance(preflight, bool), "preflight marker is invalid")
    approval = result.get("approval_reference")
    if preflight:
        require(approval is None or fuse_soak.approval_reference_valid(approval),
                "preflight approval reference is invalid")
    else:
        require(fuse_soak.approval_reference_valid(approval), "target approval reference is invalid")
    positive_integer(result.get("requested_duration_seconds"), "requested duration")
    nonnegative_number(result.get("completed_duration_seconds"), "completed duration")
    if preflight:
        require(result["requested_duration_seconds"] < limits["target_duration_seconds"],
                "preflight duration is not shorter than the target")
    else:
        require(result["requested_duration_seconds"] == limits["target_duration_seconds"],
                "target request duration is inconsistent")
    require(result["completed_duration_seconds"] >= result["requested_duration_seconds"],
            "completed duration is shorter than requested")
    return limits, preflight


def verify_events(events, result, limits):
    previous_elapsed = -1
    for cycle, event in enumerate(events):
        verify_event(event, cycle, limits)
        require(event["elapsed_seconds"] >= previous_elapsed,
                "event elapsed times are not monotonic")
        previous_elapsed = event["elapsed_seconds"]
    require(result.get("completed_cycles") == len(events), "completed cycle count differs from events")
    minimum_cycles = ((result["requested_duration_seconds"] + limits["cycle_seconds"] - 1) //
                      limits["cycle_seconds"])
    require(len(events) >= minimum_cycles, "soak has too few completed cycles")


def verify_evidence_summary(result, events_path, events):
    evidence = result.get("evidence")
    require(isinstance(evidence, dict), "evidence summary is missing")
    expected = {
        "events_sha256": hashlib.sha256(events_path.read_bytes()).hexdigest(),
        "event_count": len(events),
        "total_operations": sum(event["operations"] for event in events),
        "total_pressure_writes": sum(event["pressure_writes"] for event in events),
        "peak_rss_kib": max(event["resources"]["rss_kib"] for event in events),
        "peak_open_descriptors": max(event["resources"]["open_descriptors"] for event in events),
    }
    require(evidence == expected, "evidence summary differs from event log")


def verify(output):
    output = Path(output)
    result = load_json(output / "result.json")
    limits, preflight = verify_result(result)
    events_path = output / "events.jsonl"
    events = load_events(events_path)
    verify_events(events, result, limits)
    verify_evidence_summary(result, events_path, events)
    qualified = not preflight and result["completed_duration_seconds"] >= limits["target_duration_seconds"]
    require(result.get("qualified") is qualified, "qualification marker is inconsistent")
    return {"qualified": qualified, "events": len(events), "status": "passed"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        print(json.dumps(verify(arguments.output), sort_keys=True))
    except RuntimeError as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
