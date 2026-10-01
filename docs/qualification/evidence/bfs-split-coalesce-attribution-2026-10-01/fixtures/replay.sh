#!/bin/sh
set -eu

fixture_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH='' cd -- "$fixture_dir/../.." && pwd)
private_root="$repo_root/build/split-coalesce-attribution-source"
cc=${CC:-clang}

core_sources="\
$private_root/src/core/alloc.c \
$private_root/src/core/bootstrap_alloc.c \
$private_root/src/core/btree.c \
$private_root/src/core/cache.c \
$private_root/src/core/crc32.c \
$private_root/src/core/crc32_zero.c \
$private_root/src/core/dir.c \
$private_root/src/core/extent.c \
$private_root/src/core/file.c \
$private_root/src/core/fs.c \
$private_root/src/core/fsck.c \
$private_root/src/core/inode.c \
$private_root/src/core/namespace.c \
$private_root/src/core/refcount.c \
$private_root/src/core/snapshot.c \
$private_root/src/core/superblock.c \
$private_root/src/core/txn.c"

common_flags="-std=c99 -Wall -Wextra -Werror -g -O1 -pthread -DBFS_HOST=1 -DBFS_PERF_PROBE -D_POSIX_C_SOURCE=200809L"
include_flags="-I $fixture_dir/fake -I $private_root/include -idirafter $private_root/src/amiga"

{
    "$cc" --version | head -3
    printf '%s\n' "Private core source: $private_root/src/core/*.c"
    printf '%s\n' "Common flags: $common_flags"
    printf '%s\n' "Include flags: $include_flags"
} > "$fixture_dir/toolchain.txt" 2>&1

# shellcheck disable=SC2086
"$cc" $common_flags $include_flags \
    "$fixture_dir/fixtures.c" "$fixture_dir/probe_stubs.c" $core_sources \
    -o "$fixture_dir/fixtures" > "$fixture_dir/build-normal-replay.log" 2>&1
"$fixture_dir/fixtures" > "$fixture_dir/run-normal-replay.log" 2>&1

# Compile every private core translation unit under sanitizers, not only the fixture.
# shellcheck disable=SC2086
"$cc" $common_flags -fsanitize=address,undefined -fno-omit-frame-pointer \
    $include_flags "$fixture_dir/fixtures.c" "$fixture_dir/probe_stubs.c" $core_sources \
    -o "$fixture_dir/fixtures-sanitize" > "$fixture_dir/build-sanitize-replay.log" 2>&1
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}
export ASAN_OPTIONS UBSAN_OPTIONS
"$fixture_dir/fixtures-sanitize" > "$fixture_dir/run-sanitize-replay.log" 2>&1

cat "$fixture_dir/run-normal-replay.log"
cat "$fixture_dir/run-sanitize-replay.log"
