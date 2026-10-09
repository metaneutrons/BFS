#!/usr/bin/env bash
set -euo pipefail

tag=${1:?release tag is required}
directory=${2:-dist}
commit=${3:-$(git rev-parse HEAD)}
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/release_integrity.py" archives "$tag" "$directory" "$commit"
