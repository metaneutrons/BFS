# Performance evidence packaging for push

This record repairs packaging of 68 retained Amiga formatter logs so the
repository asset gate can check them as text. It does not change a measurement,
test fixture, handler, filesystem policy or performance conclusion. The input
checkpoint is `3cbe7f1fc6dac6516cd8e6876dce75470893ee45`.

Each existing `format-pfs3.txt` path is an ASCII display view: the Amiga C1 CSI
byte `0x9b` becomes the equivalent ASCII `ESC [` sequence. The adjacent
`format-pfs3.txt.b64` preserves the exact original bytes. The packaging index
records both identities. Original manifests are preserved losslessly under
`original-manifests`; current manifests describe the ASCII views and added
raw-byte packages. Historical run logs and identity logs retain their original
checkpoint meanings and are not rewritten as new measurement receipts.

Run `node docs/qualification/evidence/bfs-performance-push-packaging-2026-10-02/package-formatter-records.mjs`
from the repository to check all raw and view identities, original manifests,
current manifest contents and the unchanged TSV/source scope. This verification
uses local Git history but needs no network, emulator, handler build or licensed
runtime asset. The `--package` mode is a one-time mechanical conversion that
requires the exact checkpoint with no tracked changes and refuses to overwrite
sidecars. Unrelated untracked files are not part of that cleanliness check.

The independent fixture inventory and packaging checks are retained separately.
Push completion is established by comparing the remote branch identity with
local HEAD, not by this packaging record or a pre-push command alone.
