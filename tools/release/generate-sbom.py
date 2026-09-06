#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate a file-complete SBOM from the exact release payload."""

import json
from pathlib import Path
import sys

from release_integrity import digest, read_archive, require, verify_metadata
from sbom_document import document


def main():
    require(len(sys.argv) == 6, "usage: generate-sbom.py PAYLOAD OUTPUT TAG COMMIT EPOCH")
    payload, output = Path(sys.argv[1]), Path(sys.argv[2])
    tag, commit, epoch = sys.argv[3], sys.argv[4], int(sys.argv[5])
    files = read_archive(payload, tag)
    verify_metadata(files, tag, commit)
    require(json.loads(files["BUILD-METADATA.json"])["source_date_epoch"] == epoch,
            "SBOM and payload timestamps differ")
    data = document(payload.name, digest(payload.read_bytes()), files)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
