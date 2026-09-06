#!/bin/sh
set -eu

msg_file=${1:?path to the commit message is missing}
msg=$(cat "$msg_file")
body=$(printf '%s\n' "$msg" | sed -e '/^#/d' -e '/^diff --git /,$d')
subject=$(printf '%s\n' "$body" | sed -e '/^[[:space:]]*$/d' -e 1q)

fail() {
    printf 'Commit rejected: %s\n' "$1" >&2
    shift
    for line in "$@"; do printf '  %s\n' "$line" >&2; done
    exit 1
}

types='feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert'
if ! printf '%s' "$subject" |
    grep -Eq "^($types)(\([a-z0-9._/-]+\))?!?: .+"; then
    fail "The subject line does not follow Conventional Commits." \
        "Is:    $subject" \
        "Want:  <type>[(scope)][!]: <description>" \
        "Types: feat fix docs style refactor perf test build ci chore revert"
fi

if [ "${#subject}" -gt 100 ]; then
    fail "The subject line is ${#subject} characters long; 100 are allowed."
fi

if printf '%s\n' "$body" |
    grep -Eiq '^[[:space:]]*co-authored-by:.*(claude|anthropic|codex|openai)'; then
    fail "The message contains an AI attribution trailer."
fi

if printf '%s\n' "$body" | grep -Eiq 'generated with .*(claude code|codex)'; then
    fail "The message contains an AI generation line."
fi
