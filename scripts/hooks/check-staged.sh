#!/bin/sh
set -eu

max_bytes=${MAX_STAGED_BYTES:-5242880}
status=0

fail() {
    printf 'Commit rejected: %s\n' "$1" >&2
    status=1
}

branch=''
if branch_name=$(git symbolic-ref --quiet --short HEAD 2>/dev/null); then
    branch=$branch_name
fi
default=main
if head_ref=$(git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null); then
    default=${head_ref#origin/}
fi
if [ "$branch" = "$default" ] && [ "${ALLOW_COMMIT_ON_DEFAULT:-}" != '1' ]; then
    fail "Direct commit to '$default'. Create a branch, or set ALLOW_COMMIT_ON_DEFAULT=1."
fi

git diff --cached --raw --no-abbrev --diff-filter=AM |
awk '{print $4}' |
git cat-file --batch-check='%(objectname) %(objecttype) %(objectsize)' |
while read -r object type size; do
    [ "$type" = blob ] || continue
    if [ "$size" -gt "$max_bytes" ]; then
        printf 'Commit rejected: staged blob %s is %s bytes (limit %s).\n' \
            "$object" "$size" "$max_bytes" >&2
        exit 1
    fi
done || status=1

if git -c core.quotePath=false diff --cached --name-only |
    grep -Eq '(^|/|")(node_modules|target|dist|build|\.next|coverage)/'; then
    fail "A build or dependency directory is staged. Check .gitignore."
fi

exit "$status"
