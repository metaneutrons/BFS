#!/usr/bin/env python3
"""Model acknowledged and persisted block writes without using BFS code."""

import argparse
import json
from pathlib import Path
import sys


class ModelError(Exception):
    pass


def load_plan(path):
    try:
        plan = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ModelError("invalid persistence plan") from error
    if plan.get("format_version") != 1 or not isinstance(plan.get("writes"), list):
        raise ModelError("unsupported persistence plan")
    seen = set()
    for write in plan["writes"]:
        block = write.get("block")
        data = write.get("data")
        if not isinstance(block, int) or block < 0 or not isinstance(data, str) or block in seen:
            raise ModelError("invalid or duplicate write")
        seen.add(block)
        if write.get("ack") not in ("ok", "error") or write.get("persist") not in (
            "full", "none", "torn"
        ):
            raise ModelError("invalid acknowledgement or persistence state")
    return plan


def model(plan):
    acknowledged = []
    persisted = {}
    for write in sorted(plan["writes"], key=lambda item: item.get("persist_order", 0)):
        if write["ack"] == "ok":
            acknowledged.append(write["block"])
        if write["persist"] == "full":
            persisted[write["block"]] = write["data"]
        elif write["persist"] == "torn":
            persisted[write["block"]] = write["data"][:len(write["data"]) // 2]
    return {
        "format_version": 1,
        "acknowledged_blocks": sorted(acknowledged),
        "persisted_blocks": [{"block": block, "data": persisted[block]}
                             for block in sorted(persisted)],
    }


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("plan", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = model(load_plan(args.plan))
        result["status"] = "ok"
        exit_code = 0
    except ModelError as error:
        result = {"format_version": 1, "status": "error", "code": str(error)}
        exit_code = 3
    output = json.dumps(result, sort_keys=True, separators=(",", ":"))
    print(output)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
