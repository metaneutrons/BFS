#!/usr/bin/env bash
set -euo pipefail

tag=${1:?release tag is required}
directory=${2:-dist}
mode=${3:-local}
case "$mode" in
    local|supply-chain) ;;
    *) printf 'ERROR: invalid candidate verification mode.\n' >&2; exit 2 ;;
esac
package="bfs-${tag}-amiga"
payloads=("${package}.tar.gz" "${package}.lha")
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/release_integrity.py" candidate "$tag" "$directory"

for payload in "${payloads[@]}"; do
    if [[ "$mode" == supply-chain ]]; then
        cosign verify-blob \
            --bundle "$directory/$payload.sigstore.json" \
            --certificate-oidc-issuer https://token.actions.githubusercontent.com \
            --certificate-identity \
              "https://github.com/metaneutrons/BFS/.github/workflows/release.yml@refs/tags/$tag" \
            "$directory/$payload"
        for asset in "$payload" "$payload.spdx.json" "$payload.sigstore.json"; do
            gh attestation verify "$directory/$asset" \
                --repo metaneutrons/BFS \
                --signer-workflow metaneutrons/BFS/.github/workflows/release.yml \
                --source-ref "refs/tags/$tag"
        done
    fi
done

if [[ "$mode" == supply-chain ]]; then
    gh attestation verify "$directory/SHA256SUMS" \
        --repo metaneutrons/BFS \
        --signer-workflow metaneutrons/BFS/.github/workflows/release.yml \
        --source-ref "refs/tags/$tag"
fi

printf 'Release candidate completeness and integrity checks passed.\n'
