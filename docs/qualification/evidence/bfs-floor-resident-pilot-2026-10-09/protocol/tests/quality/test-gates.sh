#!/usr/bin/env bash
set -euo pipefail

root=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

printf 'fix(core): accept a valid message\n' > "$tmp/message"
sh "$root/scripts/hooks/check-commit-message.sh" "$tmp/message"

printf 'invalid message\n' > "$tmp/message"
if sh "$root/scripts/hooks/check-commit-message.sh" "$tmp/message" >/dev/null 2>&1; then
    printf 'ERROR: invalid commit message passed.\n' >&2
    exit 1
fi

printf 'fix: test\n\nCo-Authored-By: Claude <noreply@anthropic.com>\n' > "$tmp/message"
if sh "$root/scripts/hooks/check-commit-message.sh" "$tmp/message" >/dev/null 2>&1; then
    printf 'ERROR: forbidden attribution passed.\n' >&2
    exit 1
fi

printf 'plain text\n' > "$tmp/text.txt"
"$root/tools/check-no-binaries.sh" "$tmp/text.txt" >/dev/null
# Linux libmagic has classified this ASCII source as a Sega Pico ROM.
"$root/tools/check-no-binaries.sh" "$root/tests/test_fsck.c" >/dev/null
printf '\000\001\002' > "$tmp/binary"
cp "$tmp/binary" "$tmp/disguised.c"
if "$root/tools/check-no-binaries.sh" "$tmp/disguised.c" >/dev/null 2>&1; then
    printf 'ERROR: binary fixture with a source extension passed.\n' >&2
    exit 1
fi
printf '#!/bin/sh\nprintf harmless\n\000\001\002' > "$tmp/disguised.sh"
if "$root/tools/check-no-binaries.sh" "$tmp/disguised.sh" >/dev/null 2>&1; then
    printf 'ERROR: binary-padded script passed.\n' >&2
    exit 1
fi
if "$root/tools/check-no-binaries.sh" "$tmp/binary" >/dev/null 2>&1; then
    printf 'ERROR: binary fixture passed.\n' >&2
    exit 1
fi

printf 'checksum fixture\n' > "$tmp/checksum"
digest=$(shasum -a 256 "$tmp/checksum" | awk '{print $1}')
"$root/tools/verify-sha256.sh" "$tmp/checksum" "$digest" >/dev/null
printf 'tampered\n' >> "$tmp/checksum"
if "$root/tools/verify-sha256.sh" "$tmp/checksum" "$digest" >/dev/null 2>&1; then
    printf 'ERROR: corrupt checksum fixture passed.\n' >&2
    exit 1
fi

mkdir "$tmp/workflows"
printf '%s\n' \
    'steps:' \
    '  - uses: actions/example@0123456789012345678901234567890123456789 # v1.0.0' \
    '  - uses: ./local-action' > "$tmp/workflows/good.yml"
"$root/tools/check-actions-pinned.sh" "$tmp/workflows"

printf '%s\n' 'steps:' '  - uses: actions/example@v1' > "$tmp/workflows/bad.yml"
if "$root/tools/check-actions-pinned.sh" "$tmp/workflows" >/dev/null 2>&1; then
    printf 'ERROR: mutable action reference passed.\n' >&2
    exit 1
fi

base=$(git rev-parse HEAD^)
head=$(git rev-parse HEAD)
jq -n --arg base "$base" --arg head "$head" \
    '{pull_request: {base: {sha: $base}, head: {sha: $head},
      title: "ci: validate repository gates", body: "Local positive probe."}}' \
    > "$tmp/event.json"
GITHUB_EVENT_NAME=pull_request GITHUB_EVENT_PATH="$tmp/event.json" \
    "$root/tools/check-commit-hygiene.sh"

jq '.pull_request.title = "invalid title"' "$tmp/event.json" > "$tmp/bad-event.json"
if GITHUB_EVENT_NAME=pull_request GITHUB_EVENT_PATH="$tmp/bad-event.json" \
    "$root/tools/check-commit-hygiene.sh" >/dev/null 2>&1; then
    printf 'ERROR: invalid pull request title passed.\n' >&2
    exit 1
fi

printf 'fix: test\n\nCo-Authored-By: Codex <codex@example.invalid>\n' > "$tmp/message"
if sh "$root/scripts/hooks/check-commit-message.sh" "$tmp/message" > "$tmp/rejected" 2>&1; then
    printf 'ERROR: Codex attribution passed.\n' >&2
    exit 1
fi
grep -q 'AI attribution trailer' "$tmp/rejected"

git init -q -b quality-probe "$tmp/staged"
(
    cd "$tmp/staged"
    printf 'valid\n' > small.txt
    git add small.txt
    sh "$root/scripts/hooks/check-staged.sh"
    printf 'a staged object larger than the test limit\n' > large.txt
    git add large.txt
    printf 'x' > large.txt
    if MAX_STAGED_BYTES=16 sh "$root/scripts/hooks/check-staged.sh" > "$tmp/rejected" 2>&1; then
        printf 'ERROR: oversized staged object passed after shrinking its working copy.\n' >&2
        exit 1
    fi
    grep -q 'staged blob.*limit 16' "$tmp/rejected"
    git rm --cached -f -q large.txt
    mkdir build
    printf 'harmless build fixture\n' > build/probe.txt
    git add build/probe.txt
    if sh "$root/scripts/hooks/check-staged.sh" > "$tmp/rejected" 2>&1; then
        printf 'ERROR: staged build directory passed.\n' >&2
        exit 1
    fi
    grep -q 'build or dependency directory' "$tmp/rejected"
)

printf 'Quality gate positive and counter-probes passed.\n'
