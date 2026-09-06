#!/usr/bin/env bash
set -euo pipefail

destination=${1:-"${HOME}/.local/bin"}
version=0.6.0

case "$(uname -s):$(uname -m)" in
    Linux:x86_64)
        asset=lha-x86_64-linux-musl.tar.xz
        sha256=8a314019a30d15dfedcfb6346d90ea7564b468a167a374caa651ba23c1353146
        ;;
    Darwin:arm64)
        asset=lha-aarch64-macos.tar.xz
        sha256=0d2b4af04d02cff6ef73e59c1c4aa34e2e92594d3744813445d0f656c02d8af7
        ;;
    Darwin:x86_64)
        asset=lha-x86_64-macos.tar.xz
        sha256=5628706fc96733000d8dd04d9c75b758651bfa6f8b466a3ef6e59261be9fc158
        ;;
    *)
        printf 'ERROR: no pinned LHA binary is configured for %s/%s.\n' \
            "$(uname -s)" "$(uname -m)" >&2
        exit 2
        ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
archive="$tmp/$asset"
curl --fail --silent --show-error --location --retry 3 \
    --output "$archive" \
    "https://github.com/ljh-sh/lha/releases/download/v${version}/${asset}"
tools/verify-sha256.sh "$archive" "$sha256"
tar -xJf "$archive" -C "$tmp"

mapfile_supported=false
if [[ -n "${BASH_VERSION:-}" ]] && (( BASH_VERSINFO[0] >= 4 )); then
    mapfile_supported=true
fi
if [[ "$mapfile_supported" == true ]]; then
    mapfile -t binaries < <(find "$tmp" -type f -path '*/bin/lha')
else
    binaries=()
    while IFS= read -r binary; do binaries+=("$binary"); done \
        < <(find "$tmp" -type f -path '*/bin/lha')
fi
if [[ "${#binaries[@]}" -ne 1 ]]; then
    printf 'ERROR: expected one lha binary, found %s.\n' "${#binaries[@]}" >&2
    exit 1
fi

mkdir -p "$destination"
install -m 0755 "${binaries[0]}" "$destination/lha"
lha_version=$("$destination/lha" 2>&1)
grep -q 'LHa for UNIX' <<< "$lha_version"
printf 'Installed pinned LHA %s in %s.\n' "$version" "$destination"
