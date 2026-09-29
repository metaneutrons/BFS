# Fault and corruption qualification

This is the executable, first software stage of [issue #28](https://github.com/metaneutrons/BFS/issues/28). It implements the bounded image campaign defined in [the fault and corruption qualification plan](../plans/fault-corruption-qualification.md). It is neither a general durability claim nor physical-storage qualification.

## Scope

`tests/qualification/fault-campaign-v1.json` is the versioned source of truth. The initial direct-host campaign creates only disposable regular BFS images and covers:

- a clean committed baseline;
- primary-superblock CRC fallback, clean or with explicitly permitted free-space-leak repair;
- dual-superblock rejection;
- a CRC-valid but structurally invalid directory-tree header;
- a byte-level corruption of checksummed file data;
- unsupported format-option refusal, including refusal of `bfs check --repair` without a write;
- a deliberately unreachable allocation, for which `bfs check --repair` is permitted to reclaim space only after the structural scan is clean;
- the existing bounded write-cut-point crash suite; and
- POSIX `EINTR`, short-transfer, I/O-error, geometry, and allocation fault probes.

Every case declares its fault model, fault point, mutation location, expected outcome class, expected states, observer set, timeout, and seed before it runs. The runner rejects incomplete manifests, duplicate identifiers, unknown outcome classes, unowned evidence paths, and existing evidence directories.

The primary-superblock fallback is classified as `recoverable`: the backup root must be independently oracle-consistent. Depending on the allocator's working-root layout, the checker may find it clean or report an unreachable allocation from the newer, rejected superblock. In the latter case, repair must reclaim the leak and a second checker and oracle must pass. The separate deliberate-leak case still requires the strict `repairable-leak` outcome.

## Running and retaining evidence

Build the host tools and run an evidence-producing campaign in a new directory:

```sh
make fault-qualification OUTPUT=/var/tmp/bfs-fault-campaign-<commit>
```

The target refuses to replace an evidence directory. It copies the manifest and writes a per-case `baseline.bfs`, final `image.bfs`, append-only `cases.jsonl`, and `result.json`. Each record identifies the source commit; hashes every invoked binary, the manifest, baseline and final image; records every command, exit status, elapsed time, and bounded output tails; and records declared plus observed outcomes.

Run the verifier separately when inspecting retained evidence:

```sh
make fault-qualification-verify OUTPUT=/var/tmp/bfs-fault-campaign-<commit>
```

The verifier rejects altered manifest, case record, evidence summary, baseline or final image digests, incomplete case records, changed declared contracts, and a result whose summary does not agree with its case results. Campaign tests include positive execution and counter-probes for duplicate identifiers, unknown outcomes, missing observers, overwriting evidence, and tampered case records.

Pull requests run this direct-image campaign in the `Fault and Corruption Qualification` CI job and retain its evidence artifact even when the campaign fails.

## Explicit limits

This stage does not exercise a mounted FUSE surface, real power cuts, controller write-cache lies, bridges, or physical media. Those remain separate bounded additions under #28. The regular-image-only host CLI guard remains unchanged; no raw-device access is introduced here. A passed record must therefore be described as a bounded modelled-image and adapter result only.
