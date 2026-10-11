#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Create the release source archive from one commit, independent of local git settings."""

from pathlib import Path
import re
import subprocess  # nosec B404
import sys

from release_integrity import SOURCE_EXCLUDED, git, require, source_package_name


def main():
    require(len(sys.argv) == 4, "usage: create-source-archive.py TAG COMMIT OUTPUT")
    tag, commit, output = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    require(re.fullmatch(r"[0-9a-f]{40}", commit), "invalid source commit")
    root = source_package_name(tag)
    # Fixed umask and no line-ending conversion keep user configuration out of the bytes.
    tar = git("-c", "tar.umask=0022", "-c", "core.autocrlf=false", "archive", "--format=tar",
              f"--prefix={root}/", commit, "--", ".", *(f":(exclude){path}" for path in SOURCE_EXCLUDED))
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    compressed = subprocess.run(["gzip", "-n"], input=tar, check=True, capture_output=True,  # nosec B603 B607
                                shell=False).stdout
    output.mkdir(parents=True, exist_ok=True)
    target = output / f"{root}.tar.gz"
    target.write_bytes(compressed)
    print(f"Created {target}.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"ERROR: {error}")
