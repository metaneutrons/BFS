#!/bin/bash
set -euo pipefail
directory=${1:?run directory required}
[[ ${2:-deep-compare} == deep-compare ]]
root=$(cd "$(dirname "$0")/.." && pwd)
python3 "$root/tools/attribution-consumer/consume.py" "$directory" \
    --header "$root/src/amiga/perf_probe.h" \
    --verifier "$root/emulator-test/verify-bench-results.sh"
