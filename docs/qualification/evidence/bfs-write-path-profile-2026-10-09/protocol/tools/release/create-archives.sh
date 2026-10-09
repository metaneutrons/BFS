#!/usr/bin/env bash
set -euo pipefail
export TZ=UTC LC_ALL=C

tag=${1:?release tag is required}
source_date_epoch=${2:?SOURCE_DATE_EPOCH is required}
output_dir=${3:-dist}
source_dir=build/release
package="bfs-${tag}-amiga"
python3 tools/release/build_identity.py verify

[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]
[[ "$source_date_epoch" =~ ^[0-9]+$ ]]
command -v lha >/dev/null 2>&1 || { printf 'ERROR: lha is required.\n' >&2; exit 2; }
if ! lha_version=$(lha --version 2>&1); then
    printf 'ERROR: installed LHa could not execute.\n' >&2
    exit 2
fi
[[ "$lha_version" == *'LHa for UNIX'* ]] || {
    printf 'ERROR: the pinned LHa archiver is required; Lhasa cannot create archives.\n' >&2
    exit 2
}

required=(
    bfshandler
    bfshandler.020
    bfshandler.030
    bfshandler.040
    bfshandler.060
    bfshandler.080
    bfs-test
    bfs
)
for file in "${required[@]}"; do
    [[ -s "$source_dir/$file" ]] || { printf 'ERROR: missing %s.\n' "$source_dir/$file" >&2; exit 1; }
done

staging=build/package
rm -rf "$staging"
mkdir -p "$staging/$package" "$output_dir"
output_dir=$(cd -P "$output_dir" && pwd)
for file in "${required[@]}"; do
    cp "$source_dir/$file" "$staging/$package/$file"
done
cp LICENSE README.md "$staging/$package/"
cp THIRD-PARTY.md "$staging/$package/"
cp licenses/libnix-Public-Domain.txt "$staging/$package/LICENSE.libnix"
cp licenses/GPL-3.0.txt "$staging/$package/LICENSE.libgcc"
cp licenses/GCC-exception-3.1.txt "$staging/$package/LICENSE.libgcc-exception"
python3 tools/release/runtime_inventory.py "$source_dir" build/link-maps \
    "${TOOLCHAIN_ID:-local-amiga-gcc}" "$staging/$package/RUNTIME-COMPONENTS.json"

jq -n \
    --arg tag "$tag" \
    --arg commit "$(git rev-parse HEAD)" \
    --arg toolchain "${TOOLCHAIN_ID:-local-amiga-gcc}" \
    --argjson epoch "$source_date_epoch" \
    '{schema: 1, tag: $tag, source_commit: $commit, source_date_epoch: $epoch,
      toolchain: $toolchain,
      targets: [
        {file: "bfshandler.020", flags: ["-m68020", "-Os"]},
        {file: "bfshandler.030", flags: ["-m68030", "-Os"]},
        {file: "bfshandler.040", flags: ["-m68040", "-Os"]},
        {file: "bfshandler.060", flags: ["-m68060", "-Os"]},
        {file: "bfshandler.080", flags: ["-m68080", "-Os"]}
      ]}' > "$staging/$package/BUILD-METADATA.json"

python3 - "$staging/$package" "$source_date_epoch" <<'PY'
import os
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
epoch = int(sys.argv[2])
metadata_path = root / "BUILD-METADATA.json"
metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
metadata["files"] = {
    path.name: hashlib.sha256(path.read_bytes()).hexdigest()
    for path in sorted(root.iterdir()) if path != metadata_path
}
metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
for path in [root, *sorted(root.rglob("*"))]:
    path.chmod(0o755 if path.is_dir() or path.name in ("bfs-test", "bfs")
               or path.name.startswith("bfshandler") else 0o644)
    os.utime(path, (epoch, epoch), follow_symlinks=False)
PY

tar_bin=tar
if command -v gtar >/dev/null 2>&1; then tar_bin=gtar; fi
if ! "$tar_bin" --version 2>/dev/null | grep -q 'GNU tar'; then
    printf 'ERROR: GNU tar is required for deterministic release archives.\n' >&2
    exit 2
fi

tar_file="$output_dir/${package}.tar.gz"
lha_file="$output_dir/${package}.lha"
rm -f "$tar_file" "$lha_file"
"$tar_bin" --directory "$staging" --sort=name --format=ustar \
    --owner=0 --group=0 --numeric-owner --mtime="@${source_date_epoch}" \
    -cf - "$package" | gzip -n > "$tar_file"

lha_inputs=()
for file in "${required[@]}" BUILD-METADATA.json LICENSE README.md THIRD-PARTY.md \
    RUNTIME-COMPONENTS.json LICENSE.libnix LICENSE.libgcc LICENSE.libgcc-exception; do
    lha_inputs+=("$package/$file")
done
(
    cd "$staging"
    # Generic mode resets the header level; select level 2 afterwards for paths.
    # This retains portable headers without host UID/GID or Unix-specific metadata.
    lha cq2g2 "$lha_file" "${lha_inputs[@]}"
)

[[ -s "$tar_file" && -s "$lha_file" ]]
printf 'Created %s and %s.\n' "$tar_file" "$lha_file"
