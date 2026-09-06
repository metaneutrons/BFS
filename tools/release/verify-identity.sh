#!/usr/bin/env bash
set -euo pipefail

tag=${1:?release tag is required}

identifier='(0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)'
semver="^v(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-${identifier}(\\.${identifier})*)?$"
if [[ ! "$tag" =~ $semver ]]; then
    printf 'ERROR: %s is not a canonical v-prefixed semantic version.\n' "$tag" >&2
    exit 1
fi

tag_commit=$(git rev-parse "refs/tags/${tag}^{commit}")
head_commit=$(git rev-parse HEAD)
if [[ "$tag_commit" != "$head_commit" ]]; then
    printf 'ERROR: tag %s points to %s, but the checkout is %s.\n' \
        "$tag" "$tag_commit" "$head_commit" >&2
    exit 1
fi

version=${tag#v}
version=${version%%-*}
file_version=$(tr -d '[:space:]' < version.txt)
manifest_version=$(jq -er '.["."]' .release-please-manifest.json)
if [[ "$file_version" != "$version" || "$manifest_version" != "$version" ]]; then
    printf 'ERROR: tag core %s, version.txt %s, and manifest %s do not agree.\n' \
        "$version" "$file_version" "$manifest_version" >&2
    exit 1
fi

jq -e \
    '.packages["."].draft == true and .packages["."]."force-tag-creation" == true' \
    release-please-config.json >/dev/null

printf 'Release identity verified: %s at %s.\n' "$tag" "$head_commit"
