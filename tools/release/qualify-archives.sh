#!/usr/bin/env bash
set -euo pipefail

tag=${1:?release tag is required}
epoch=${2:?source date epoch is required}
output=${3:-dist}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

tools/release/create-archives.sh "$tag" "$epoch" "$output"
tools/release/verify-archives.sh "$tag" "$output"
tools/release/create-archives.sh "$tag" "$epoch" "$temporary/repeated"
for suffix in tar.gz lha; do
    cmp "$output/bfs-${tag}-amiga.$suffix" "$temporary/repeated/bfs-${tag}-amiga.$suffix"
done

printf 'tamper\n' >> "$temporary/repeated/bfs-${tag}-amiga.tar.gz"
set +e
tools/release/verify-archives.sh "$tag" "$temporary/repeated" > "$temporary/negative.log" 2>&1
status=$?
set -e
[[ "$status" -eq 1 ]]
grep -q 'ERROR: invalid gzip archive' "$temporary/negative.log"
printf 'Archive qualification passed: identical rebuilds and expected gzip tamper rejection.\n'
