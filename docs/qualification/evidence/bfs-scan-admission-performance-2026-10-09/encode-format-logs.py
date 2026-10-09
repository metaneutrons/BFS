#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Losslessly encode Amiga Format progress/control bytes as repository text.

Timing TSVs and guest test outputs are untouched. Each encoded log includes
its original byte digest; base64 decoding restores the exact original bytes.
This mechanical conversion is idempotent and verifies every stored digest.
"""

import base64
import hashlib
from pathlib import Path


HEADER = b"BFS_RAW_LOG_BASE64\t1\n"


if __name__ == "__main__":
    converted = 0
    verified = 0
    for path in sorted(Path(__file__).resolve().parent.rglob("format-pfs3.txt")):
        original = path.read_bytes()
        if not original.startswith(HEADER):
            digest = hashlib.sha256(original).hexdigest().encode("ascii")
            encoded = HEADER + b"ORIGINAL_SHA256\t" + digest + b"\nDATA\t"
            encoded += base64.b64encode(original) + b"\n"
            path.write_bytes(encoded)
            original = encoded
            converted += 1
        fields = dict(line.split(b"\t", 1) for line in original.splitlines())
        decoded = base64.b64decode(fields[b"DATA"], validate=True)
        if hashlib.sha256(decoded).hexdigest().encode("ascii") != fields[b"ORIGINAL_SHA256"]:
            raise ValueError(f"raw log digest mismatch: {path}")
        verified += 1
    print(f"Converted {converted} Format logs; verified {verified} original byte digests.")
