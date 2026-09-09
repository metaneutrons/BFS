#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail

binary=${1:?usage: check-conformance-linkage.sh POSIX_BACKEND}
test -x "$binary"
if nm -g "$binary" | awk '{print $NF}' | sed 's/^_//' | grep -Eq '^bfs_'; then
    echo "mounted POSIX backend links a BFS symbol" >&2
    exit 1
fi
