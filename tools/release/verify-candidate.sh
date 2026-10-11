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
source_archive="bfs-${tag}-source.tar.gz"
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/release_integrity.py" candidate "$tag" "$directory"

verify_signature() {
    cosign verify-blob \
        --bundle "$directory/$1.sigstore.json" \
        --certificate-oidc-issuer https://token.actions.githubusercontent.com \
        --certificate-identity \
          "https://github.com/metaneutrons/BFS/.github/workflows/release.yml@refs/tags/$tag" \
        "$directory/$1"
}

verify_attestation() {
    gh attestation verify "$directory/$1" \
        --repo metaneutrons/BFS \
        --signer-workflow metaneutrons/BFS/.github/workflows/release.yml \
        --source-ref "refs/tags/$tag"
}

if [[ "$mode" == supply-chain ]]; then
    for payload in "${payloads[@]}"; do
        verify_signature "$payload"
        for asset in "$payload" "$payload.spdx.json" "$payload.sigstore.json"; do
            verify_attestation "$asset"
        done
    done
    verify_signature "$source_archive"
    verify_attestation "$source_archive"
    verify_attestation "$source_archive.sigstore.json"
    verify_attestation SHA256SUMS
fi

printf 'Release candidate completeness and integrity checks passed.\n'
