#!/usr/bin/env bash
# Install the morphos-cross toolchain pinned in tools/morphos-cross.pins for Linux x86_64
# and the MorphOS SDK that this release pins. The compiler ends up in DESTINATION/bin.
#
# The release archive is checked against the digest in the pins file. The SDK archive
# comes from morphos-team.net (about 370 MiB) and is checked by the toolchain's own
# helper against the SHA-256 in the release's pins.env. MORPHOS_CROSS_CACHE names the
# directory that holds the downloaded SDK archive (default: ~/.cache/morphos-cross), so
# a restored cache saves the download. The SDK needs bsdtar (libarchive-tools) or lha.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=tools/morphos-cross.pins
. "$root/tools/morphos-cross.pins"

if [[ "$(uname -s):$(uname -m)" != Linux:x86_64 ]]; then
    printf 'ERROR: the morphos-cross archive is pinned only for Linux x86_64.\n' >&2
    exit 2
fi

destination=${1:-"${HOME}/.morphos-cross/${MORPHOS_CROSS_VERSION}"}
version=${MORPHOS_CROSS_VERSION:?the pins file does not set MORPHOS_CROSS_VERSION}
sha256=${MORPHOS_CROSS_LINUX_X86_64_SHA256:?the pins file does not set the archive digest}

if [[ ! "$version" =~ ^v[0-9]{8}-r[0-9]+$ ]]; then
    printf 'ERROR: unexpected morphos-cross release tag: %s\n' "$version" >&2
    exit 2
fi
if [[ -e "$destination" && -n "$(ls -A "$destination")" ]]; then
    printf 'ERROR: %s exists and is not empty.\n' "$destination" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

asset="morphos-cross-${version#v}-linux-x86_64.tar.xz"
curl --fail --silent --show-error --location --retry 3 --output "$tmp/$asset" \
    "https://github.com/metaneutrons/morphos-cross/releases/download/${version}/${asset}"
"$root/tools/verify-sha256.sh" "$tmp/$asset" "$sha256"

mkdir -p "$destination"
tar -xJf "$tmp/$asset" -C "$destination" --strip-components=1
test -x "$destination/bin/ppc-morphos-gcc"

"$destination/bin/morphos-cross-sdk" install
"$destination/bin/morphos-cross-sdk" status
"$destination/bin/ppc-morphos-gcc" --version
printf 'Installed morphos-cross %s in %s.\n' "$version" "$destination"
