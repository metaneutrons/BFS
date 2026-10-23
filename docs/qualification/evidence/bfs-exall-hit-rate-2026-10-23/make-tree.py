#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Assemble the directory tree that populates the BFS volume of the usage kit.

A few thousand files in nested directories, taken from the Workbench and tool
files under emulator-test/.assets and from this repository (sources, documents
and the raw results of earlier measurements), with the names made safe for
AmigaDOS. Nothing licensed goes into git: the tree is built under build/ from
the files of the checkout.

    make-tree.py OUTPUT_DIR [--assets DIR] [--limit FILES]
"""
import argparse
from pathlib import Path
import shutil
import subprocess  # nosec B404 - fixed git query, no shell
import sys

ROOT = Path(__file__).resolve().parents[4]
MAX_BYTES = 300 * 1024
MAX_NAME = 100


def safe(name):
    name = name.replace(":", "_").replace("/", "_").rstrip(" .") or "_"
    return name[:MAX_NAME]


class Tree:
    def __init__(self, root, limit):
        self.root, self.limit, self.count, self.bytes = root, limit, 0, 0
        self.names = {}  # directory -> lower-case names already used

    def add(self, source, relative):
        if self.count >= self.limit or not source.is_file() or source.stat().st_size > MAX_BYTES:
            return False
        parts = [safe(p) for p in relative.parts]
        if any(p.startswith(".") for p in parts) or parts[-1].endswith(".uaem"):
            return False
        directory = self.root.joinpath(*parts[:-1])
        used = self.names.setdefault(directory, set())
        if parts[-1].lower() in used:
            return False
        used.add(parts[-1].lower())
        directory.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, directory / parts[-1])
        self.count += 1
        self.bytes += source.stat().st_size
        return True


def tracked(prefix):
    git = shutil.which("git")
    output = subprocess.run([git, "-C", str(ROOT), "ls-files", "-z", "--", prefix],  # nosec B603
                            capture_output=True, check=True).stdout
    return [Path(p) for p in output.decode("utf-8").split("\0") if p]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--assets", type=Path, default=ROOT / "emulator-test/.assets")
    parser.add_argument("--limit", type=int, default=3600)
    arguments = parser.parse_args()
    if arguments.output.exists():
        sys.exit(f"{arguments.output} exists")
    tree = Tree(arguments.output, arguments.limit)

    # Tools: the assets' commands and libraries.
    for source in sorted(arguments.assets.rglob("*")):
        if source.is_file() and source.suffix != ".rom":
            tree.add(source, Path("Werkzeuge") / source.relative_to(arguments.assets))

    # Projects: sources and documents with their directory structure.
    for prefix in ("src", "include", "tests", "tools", "emulator-test", "docs"):
        for path in tracked(prefix):
            text = path.as_posix()
            if text.startswith("docs/qualification/evidence/") or "/.assets/" in text:
                continue
            tree.add(ROOT / path, Path("Projekte") / "BFS" / path)

    # Reports: one directory with many files.
    for path in sorted(tracked("docs/qualification")):
        if len(path.parts) == 3:
            tree.add(ROOT / path, Path("Berichte") / path.name)

    # Measurement data: the raw results of earlier runs, nested several levels deep.
    evidence = sorted(set(p.parts[3] for p in tracked("docs/qualification/evidence") if len(p.parts) > 4))
    per_directory = max(1, (arguments.limit - tree.count - 600) // max(1, len(evidence)))
    for name in evidence:
        taken = 0
        for path in sorted(tracked(f"docs/qualification/evidence/{name}")):
            if taken >= per_directory:
                break
            if tree.add(ROOT / path, Path("Messdaten") / path.relative_to("docs/qualification/evidence")):
                taken += 1

    # A large flat directory, such as a download or an archive folder.
    flat = [p for p in sorted(tracked("docs/qualification/evidence")) if p.suffix in (".log", ".tsv", ".txt")]
    index = 0
    for path in flat:
        if index >= 600 or tree.count >= arguments.limit:
            break
        if tree.add(ROOT / path, Path("Archiv") / f"lauf-{index + 1:04d}-{path.name}"):
            index += 1

    print(f"{tree.count} Dateien, {tree.bytes / 1e6:.1f} MB in {arguments.output}")
    directories = sum(1 for d in arguments.output.rglob("*") if d.is_dir())
    print(f"{directories} Verzeichnisse")
    return 0


if __name__ == "__main__":
    sys.exit(main())
