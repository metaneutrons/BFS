#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Losslessly retain Format control bytes as text; never change timing TSVs."""

import base64
import hashlib
from pathlib import Path

HEADER = b"BFS_RAW_LOG_BASE64\t1\n"

if __name__ == "__main__":
    converted = verified = 0
    for path in sorted(Path(__file__).resolve().parent.rglob("format-pfs3.txt")):
        original = path.read_bytes()
        if not original.startswith(HEADER):
            digest = hashlib.sha256(original).hexdigest().encode("ascii")
            original = HEADER + b"ORIGINAL_SHA256\t" + digest + b"\nDATA\t"
            original += base64.b64encode(path.read_bytes()) + b"\n"
            path.write_bytes(original)
            converted += 1
        fields = dict(line.split(b"\t", 1) for line in original.splitlines())
        decoded = base64.b64decode(fields[b"DATA"], validate=True)
        if hashlib.sha256(decoded).hexdigest().encode("ascii") != fields[b"ORIGINAL_SHA256"]:
            raise ValueError(f"raw log digest mismatch: {path}")
        verified += 1
    print(f"Converted {converted} Format logs; verified {verified} original byte digests.")
