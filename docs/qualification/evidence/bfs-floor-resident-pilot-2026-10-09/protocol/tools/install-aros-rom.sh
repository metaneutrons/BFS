#!/usr/bin/env bash
set -euo pipefail

destination=${1:-build/emulator/aros}
source_commit=9d76f9ec8145239e08a2b73709ddfdc070d05b6a
base_url="https://raw.githubusercontent.com/CopperlineHQ/Copperline/${source_commit}/assets/aros"

mkdir -p "$destination"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

download() {
    local name=$1
    local sha256=$2
    local output="$destination/$name"

    if [[ -f "$output" ]] && tools/verify-sha256.sh "$output" "$sha256" >/dev/null 2>&1; then
        return
    fi

    curl --fail --silent --show-error --location --retry 3 \
        --output "$tmp/$name" "$base_url/$name"
    tools/verify-sha256.sh "$tmp/$name" "$sha256"
    [[ "$(wc -c < "$tmp/$name" | tr -d ' ')" == 524288 ]]
    mv "$tmp/$name" "$output"
}

download aros-amiga-m68k-rom.bin \
    eef8edc2bdede6d9e7d3ab57cc6a02d4c65c190666a496e930e3d9c447bdac59
download aros-amiga-m68k-ext.bin \
    a3520cb8482b5475611386cf3e68f84f66534752a3c78e5e88d39c5dee89db54

printf 'Installed checksum-verified AROS m68k ROMs in %s.\n' "$destination"
