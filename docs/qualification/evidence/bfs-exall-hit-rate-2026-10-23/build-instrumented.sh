#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build the handler with the hit-rate counter (-DBFS_HITRATE=1) from a tree that
# has hitrate-handler.patch applied. The compiler command is the one of
# `make amiga` (the release flags) with the CPU and the output replaced.
# Usage: build-instrumented.sh CPU OUTPUT   (CPU: 020 or 040)
set -euo pipefail
cpu=${1:?Usage: build-instrumented.sh CPU OUTPUT}
output=${2:?Usage: build-instrumented.sh CPU OUTPUT}
command=$(make -n amiga | sed -e :a -e '/\\$/N; s/\\\n//; ta' | grep m68k-amigaos-gcc)
command=${command//build\/amiga\/bfshandler/$output}
command=${command//-m68020/-m68$cpu -DBFS_HITRATE=1}
mkdir -p "$(dirname "$output")"
eval "$command"
ls -l "$output"
