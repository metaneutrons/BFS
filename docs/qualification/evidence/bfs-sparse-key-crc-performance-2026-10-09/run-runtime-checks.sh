#!/usr/bin/env bash
# Local isolated verification only: this does not dispatch or start remote CI.
set -euo pipefail
[[ $# == 1 ]] || { printf 'Usage: %s WORKSPACE\n' "$0" >&2; exit 2; }
workspace=$1
export GIT_DIR="$workspace/source-metadata.git"
[[ $(git rev-parse HEAD) == 07216b7f17912c9f28b9b4a920caaa6828b00ccb ]] || {
    printf 'ERROR: source base identity unavailable or wrong\n' >&2
    exit 1
}
cd "$workspace/candidate"
if pgrep -x fs-uae >/dev/null; then
    printf 'ERROR: another emulator is running\n' >&2
    exit 1
fi
mkdir -p build/amiga
for binary in bfshandler bfs-test bfs compatibility-probe cli-fixture; do
    cp "$workspace/build/inputs/sparse/$binary" "build/amiga/$binary"
done
make -j8 qualification-tests conformance-test HOST_CC=clang BFS_WITH_FUSE=0 \
    >build/sparse-linux-contracts.log 2>&1
BFS_HANDLER="$workspace/build/inputs/sparse/bfshandler" \
    BFS_TEST_BINARY="$workspace/build/inputs/sparse/bfs-test" \
    BFS_CLI="$PWD/build/host/bfs" BFS_EMULATOR_RUNTIME="$PWD/build/emulator-sparse" \
    BFS_AROS_DIR=/home/fabian/.cache/bfs-performance/listing-upgrade-2026-10-08.29qKRc/build/emulator-identity/aros \
    bash emulator-test/ci-test.sh >build/sparse-amiga-integration.log 2>&1
python3 emulator-test/compatibility-test.py \
    --handler "$workspace/build/inputs/sparse/bfshandler" \
    --kickstart /home/fabian/Amiga/kick.a1200.47.102.rom \
    --workbench /home/fabian/.cache/bfs-performance/work-2026-10-03/emulator-test/.assets \
    >build/sparse-compatibility.log 2>&1
printf 'Local runtime checks complete.\n'
