#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail

jq -e '
    keys == ["commit-hygiene", "coverage", "host-tests", "integration-test",
             "linux-build", "quality", "sanitizers", "static-analysis"] and
    all(.[]; .result == "success")
' <<< "${NEEDS:?NEEDS must contain the complete GitHub job result object}"
