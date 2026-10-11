#!/usr/bin/env bash
set -euo pipefail

tag=${1:?release tag is required}
epoch=${2:?source date epoch is required}
output=${3:-dist}
commit=$(git rev-parse HEAD)
source_archive="bfs-${tag}-source.tar.gz"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

tools/release/create-archives.sh "$tag" "$epoch" "$output"
tools/release/verify-archives.sh "$tag" "$output"
tools/release/create-archives.sh "$tag" "$epoch" "$temporary/repeated"
for suffix in tar.gz lha; do
    cmp "$output/bfs-${tag}-amiga.$suffix" "$temporary/repeated/bfs-${tag}-amiga.$suffix"
done

tools/release/create-source-archive.py "$tag" "$commit" "$output"
python3 tools/release/release_integrity.py source "$tag" "$output" "$commit"
tools/release/create-source-archive.py "$tag" "$commit" "$temporary/repeated"
cmp "$output/$source_archive" "$temporary/repeated/$source_archive"

printf 'tamper\n' >> "$temporary/repeated/bfs-${tag}-amiga.tar.gz"
set +e
tools/release/verify-archives.sh "$tag" "$temporary/repeated" > "$temporary/negative.log" 2>&1
status=$?
set -e
[[ "$status" -eq 1 ]]
grep -q 'ERROR: invalid gzip archive' "$temporary/negative.log"

printf 'tamper\n' >> "$temporary/repeated/$source_archive"
set +e
python3 tools/release/release_integrity.py source "$tag" "$temporary/repeated" "$commit" \
    > "$temporary/negative-source.log" 2>&1
status=$?
set -e
[[ "$status" -eq 1 ]]
grep -q 'ERROR: invalid gzip archive' "$temporary/negative-source.log"
printf 'Archive qualification passed: identical rebuilds and expected gzip tamper rejection.\n'
