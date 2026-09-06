#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Inventory the runtime objects actually selected by the Amiga GNU linker."""

import hashlib
import json
from pathlib import Path
import re
import sys

from release_integrity import BINARIES, require

LIBNIX = {"libnix.a", "libnix20.a", "libnixmain.a", "libstubs.a", "ncrt0.o"}
LICENSES = {"libnix": "LicenseRef-libnix-Public-Domain",
            "libgcc": "GPL-3.0-or-later WITH GCC-exception-3.1"}
SOURCES = {"libnix": "https://github.com/AmigaPorts/libnix",
           "libgcc": "https://github.com/AmigaPorts/gcc/tree/amiga6/libgcc"}


def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def selected_inputs(text):
    require(text.startswith("Archive member included to satisfy reference by file (symbol)\n"),
            "unsupported linker map format")
    header, separator, _ = text.partition("Memory Configuration\n")
    require(separator, "truncated linker map")
    selected = {}
    for line in header.splitlines():
        match = re.fullmatch(r"(/[^()\n]+\.a)\(([^()/]+\.o)\)", line)
        if match:
            selected.setdefault(Path(match[1]), set()).add(match[2])
        elif line.startswith("/"):
            raise ValueError("unrecognized runtime archive member")
    # Driver-provided startup objects are direct inputs, not archive members.
    for line in text.splitlines():
        if line.startswith("LOAD "):
            path = Path(line[5:])
            if path.name.endswith("crt0.o"):
                require(path.name == "ncrt0.o", "unexpected runtime startup object")
                selected.setdefault(path, set())
    require(selected, "linker map has no runtime input evidence")
    return selected


def component(path):
    if path.name == "libgcc.a":
        name = "libgcc"
    else:
        require(path.name in LIBNIX, f"unreviewed runtime input: {path.name}")
        name = "libnix"
    require(path.is_file(), f"missing runtime input: {path.name}")
    sha256 = checksum(path)
    return {"id": "runtime-" + sha256, "name": name, "input": path.name,
            "sha256": sha256, "license": LICENSES[name], "source_url": SOURCES[name]}


def inventory(directory, maps, toolchain):
    require(toolchain, "toolchain identity is required")
    components, binaries = {}, {}
    for name in BINARIES:
        map_name = "bfshandler.020" if name == "bfshandler" else name
        selected = selected_inputs((maps / f"{map_name}.map").read_text(encoding="utf-8"))
        inputs = []
        for path, members in selected.items():
            record = component(path)
            identifier = record["id"]
            require(identifier not in components or components[identifier] == record,
                    "ambiguous runtime input identity")
            components[identifier] = record
            inputs.append({"component": identifier, "members": sorted(members)})
        binaries[name] = {"sha256": checksum(directory / name),
                          "inputs": sorted(inputs, key=lambda entry: entry["component"])}
    return {"schema": 1, "toolchain": toolchain, "binaries": binaries,
            "components": sorted(components.values(), key=lambda entry: entry["id"])}


def main():
    require(len(sys.argv) == 5, "usage: runtime_inventory.py BINARIES MAPS TOOLCHAIN OUTPUT")
    data = inventory(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
    Path(sys.argv[4]).write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(f"ERROR: {error}")
