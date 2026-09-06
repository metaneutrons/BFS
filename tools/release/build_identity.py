#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bind release binaries to a clean source commit and unchanged build inputs."""

import json
from pathlib import Path
import shutil
import subprocess  # nosec B404 - fixed local Git queries, no shell
import sys

from release_integrity import BINARIES, digest, require

INPUTS = ("Makefile", "src", "include", "tools/bfs-test.c", "tools/bfs-test-cases.def",
          "tools/bfsformat.c", "tools/bfssnapshot.c", "tools/release/build_identity.py")
RECORD = Path("build/link-maps/build-identity.json")


def git(*arguments):
    executable = shutil.which("git")
    require(executable is not None, "git is required")
    return subprocess.run([executable, *arguments], check=True, capture_output=True).stdout  # nosec B603


def source_identity():
    require(not git("status", "--porcelain", "--untracked-files=all", "--", *INPUTS),
            "release build inputs must be committed and clean")
    names = git("ls-files", "-z", "--", *INPUTS).decode("utf-8").split("\0")
    return {"source_commit": git("rev-parse", "HEAD").decode("ascii").strip(),
            "sources": {name: digest(Path(name).read_bytes()) for name in names if name}}


def verify(record, sources, binaries):
    require(record.get("state") == "complete", "release build did not finish")
    require(record.get("source") == sources, "source changed since release compilation")
    require(record.get("binaries") == binaries, "release binaries changed since compilation")


def main():
    require(len(sys.argv) == 2 and sys.argv[1] in ("begin", "finish", "verify"),
            "usage: build_identity.py begin|finish|verify")
    mode = sys.argv[1]
    source = source_identity()
    if mode == "begin":
        RECORD.parent.mkdir(parents=True, exist_ok=True)
        RECORD.write_text(json.dumps({"state": "building", "source": source}), encoding="utf-8")
        for name in BINARIES:
            (Path("build/release") / name).unlink(missing_ok=True)
            (RECORD.parent / f"{name}.map").unlink(missing_ok=True)
        return
    record = json.loads(RECORD.read_bytes())
    binaries = {name: digest((Path("build/release") / name).read_bytes()) for name in BINARIES}
    if mode == "finish":
        require(record.get("state") == "building" and record.get("source") == source,
                "source changed during release compilation")
        record.update(state="complete", binaries=binaries)
        RECORD.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    verify(record, source, binaries)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"ERROR: {error}")
