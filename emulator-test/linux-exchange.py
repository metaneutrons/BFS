#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check an image that the Linux FUSE qualification wrote after the Amiga
handler ran an image test on it: compare the oracle's view after the run with
the image as Linux left it, and require a clean bfs check."""

import argparse
import hashlib
import json
from pathlib import Path
# This local check runs the repository's own oracle and bfs command.
import subprocess  # nosec B404
import sys

ROOT = Path(__file__).resolve().parents[1]
MASK = 0xFFFFFFFF


class ExchangeError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ExchangeError(message)


def run(argv):
    # Trusted local argv: the repository's oracle and bfs command, no shell.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args, python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    return subprocess.run([str(value) for value in argv], capture_output=True, text=True,
                          check=False, shell=False)  # nosec B603


def oracle(image):
    completed = run([sys.executable, ROOT / "tools/bfs-format-oracle.py", image])
    result = json.loads(completed.stdout) if completed.stdout else {}
    require(completed.returncode == 0 and result.get("status") == "ok",
            f"oracle rejects {image}: {completed.stdout or completed.stderr}")
    return result


def seeded(size, seed):
    """The bytes bfs-test's write_seeded writes."""
    data = bytearray()
    state = seed
    for _ in range(size):
        state = (state ^ (state << 13)) & MASK
        state ^= state >> 17
        state = (state ^ (state << 5)) & MASK
        data.append(state & 0xFF)
    return bytes(data)


def file_item(path, size, content, inode=None):
    item = {"path": path, "type": 0, "size": size, "links": 1,
            "sha256": hashlib.sha256(content).hexdigest()}
    if inode is not None:
        item["inode"] = inode
    return item


def expected_after_read(before):
    """The namespace and comments after linuxread_56 changed the image."""
    items = {item["path"]: dict(item) for item in before["namespace"]}
    renamed = items.pop("/rw/open-target")
    renamed["path"] = "/rw/amiga-renamed"
    items[renamed["path"]] = renamed
    del items["/rw/removable-comment"]
    positions = items["/rw/positions"]
    items[positions["path"]] = file_item(positions["path"], 9, b"abcdefghi", positions["inode"])
    items["/rw/amiga-new"] = file_item("/rw/amiga-new", 5000, seeded(5000, 0x4C58))
    items["/rw/amiga-dir"] = {"path": "/rw/amiga-dir", "type": 1, "size": 0, "links": 1}
    comments = dict(before["comments"])
    comments["/rw/mknod-file"] = "amiga comment"
    return items, comments


def compare_namespace(expected, after):
    """New entries have new inode numbers; every other field must match."""
    actual = {item["path"]: item for item in after["namespace"]}
    require(set(actual) == set(expected),
            f"paths differ: missing {sorted(set(expected) - set(actual))}, "
            f"unexpected {sorted(set(actual) - set(expected))}")
    for path, item in expected.items():
        fields = item.keys() if "inode" in item else set(item) - {"inode"}
        got = {key: actual[path].get(key) for key in fields}
        require(got == {key: item[key] for key in fields},
                f"{path} differs: expected {item}, found {actual[path]}")


def check(case, before_image, after_image, bfs):
    before, after = oracle(before_image), oracle(after_image)
    if case == "read":
        require(not before["unreachable_inodes"], "the input image holds unreachable inodes")
        expected, comments = expected_after_read(before)
    else:
        require(before["unreachable_inodes"], "the interrupted image retains no inode")
        expected = {item["path"]: item for item in before["namespace"]}
        comments = before["comments"]
    compare_namespace(expected, after)
    require(after["comments"] == comments,
            f"comments differ: expected {comments}, found {after['comments']}")
    require(after["snapshots"] == before["snapshots"], "snapshots differ")
    require(not after["unreachable_inodes"],
            f"unreachable inodes remain: {after['unreachable_inodes']}")
    checked = run([bfs, "check", after_image])
    require(checked.returncode == 0 and "CLEAN" in checked.stdout,
            f"bfs check: {checked.stdout}{checked.stderr}")
    return {"case": case, "status": "pass", "entries": len(after["namespace"]),
            "unreachable_before": before["unreachable_inodes"],
            "free_blocks": [before["superblock"]["free_blocks"],
                            after["superblock"]["free_blocks"]]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", choices=("read", "reap"), required=True)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--bfs", type=Path, default=ROOT / "build/host/bfs")
    args = parser.parse_args()
    try:
        summary = check(args.case, args.before, args.after, args.bfs)
    except (ExchangeError, OSError, KeyError, ValueError) as error:
        print(json.dumps({"case": args.case, "status": "fail", "error": str(error)}))
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
