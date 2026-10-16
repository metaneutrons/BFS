#!/usr/bin/env bash
# Check that every FILE is a 32-bit big-endian PowerPC ELF object (readelf -h).
# The check only reads the ELF header: it says what the cross compiler produced, not
# that the file runs on MorphOS. READELF selects another readelf, such as
# ppc-morphos-readelf on a host that has no readelf of its own.
set -euo pipefail

if [[ $# -eq 0 ]]; then
    printf 'usage: %s FILE...\n' "$0" >&2
    exit 2
fi

readelf=${READELF:-readelf}
header=

# expect FIELD VALUE: the readelf header must contain "FIELD: VALUE" on a line of its own.
expect() {
    if ! grep -Eq "^[[:space:]]*$1:[[:space:]]+$2[[:space:]]*\$" <<<"$header"; then
        printf 'ERROR: %s: the ELF header does not say "%s: %s".\n' "$file" "$1" "$2" >&2
        return 1
    fi
}

status=0
for file in "$@"; do
    if [[ ! -f "$file" ]]; then
        printf 'ERROR: %s does not exist.\n' "$file" >&2
        status=1
        continue
    fi
    if ! header=$(LC_ALL=C "$readelf" -h "$file" 2>&1); then
        printf 'ERROR: %s is not an ELF file readelf can read:\n%s\n' "$file" "$header" >&2
        status=1
        continue
    fi

    ok=true
    expect Class 'ELF32' || ok=false
    expect Data "2's complement, big endian" || ok=false
    expect Machine 'PowerPC' || ok=false
    if [[ "$ok" == true ]]; then
        printf '%s: ELF32, big endian, PowerPC\n' "$file"
    else
        printf '%s\n' "$header" >&2
        status=1
    fi
done
exit "$status"
