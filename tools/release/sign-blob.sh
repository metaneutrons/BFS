#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Sign one release payload keylessly and write PAYLOAD.sigstore.json.
#
# A transient Rekor failure can end a signing run although the log already
# holds its entry; cosign's own HTTP retry then reports a conflict (HTTP 409).
# Every cosign run signs with a fresh ephemeral key, so a new run creates a new
# log entry and cannot collide with the earlier one. The bundle of the last
# run is self-contained; an orphaned entry of a failed run is harmless.
set -euo pipefail

payload=${1:?payload path is required}
attempts=${BFS_SIGN_ATTEMPTS:-3}
delay=${BFS_SIGN_RETRY_DELAY:-15}
bundle="$payload.sigstore.json"

if [[ ! -f "$payload" || -L "$payload" ]]; then
    printf 'ERROR: payload is not a regular file: %s\n' "$payload" >&2
    exit 2
fi
if [[ ! "$attempts" =~ ^[1-9]$ || ! "$delay" =~ ^[0-9]{1,3}$ ]]; then
    printf 'ERROR: invalid signing retry settings.\n' >&2
    exit 2
fi

for ((attempt = 1; attempt <= attempts; attempt++)); do
    rm -f -- "$bundle"
    if cosign sign-blob --yes --bundle "$bundle" "$payload"; then
        exit 0
    fi
    if ((attempt < attempts)); then
        printf '::warning::Signing %s failed (attempt %d of %d); retrying with a fresh key.\n' \
            "$payload" "$attempt" "$attempts"
        sleep $((delay * attempt))
    fi
done

rm -f -- "$bundle"
printf 'ERROR: signing %s failed after %d attempts.\n' "$payload" "$attempts" >&2
exit 1
