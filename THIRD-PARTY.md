# Release runtime components

BFS source remains MPL-2.0. Amiga executables also contain runtime code selected
from the pinned compiler distribution. `RUNTIME-COMPONENTS.json` records each
selected input's SHA256, archive-member names, and the executable using it.
Library availability alone is not treated as evidence that it was linked.

## libnix

libnix, its startup objects and generated library-base stubs are declared public
domain by upstream. Authors include Matthias Fleischer, Gunther Nikl and Stefan
Franke. Source and declaration:
https://github.com/AmigaPorts/libnix/blob/b6a079d2e2c699b14d06043a2b12b4f7d2ee8505/README.md

The SPDX identifier `LicenseRef-libnix-Public-Domain` refers to that declaration,
not to an invented standard license. See `LICENSE.libnix` in the release archive.

## GCC runtime

The selected 64-bit arithmetic helpers from libgcc are governed by
GPL-3.0-or-later WITH GCC-exception-3.1. The upstream file notice is at
https://github.com/AmigaPorts/gcc/blob/amiga6/libgcc/libgcc2.c . The release carries
the GPL and exception as `LICENSE.libgcc` and `LICENSE.libgcc-exception`.

## Provenance limits

The pinned compiler image and runtime-input hashes identify the supplied bytes.
The compiler distribution does not supply a source-commit manifest for every
runtime object. Source links above identify the upstream projects and license
declarations; they are not assertions that a particular source revision has
been independently rebuilt to reproduce those objects. Component versions that
are not available are omitted from the SBOM, not guessed.

The compiler, NDK, emulator, ROMs and archiver themselves are not distributed in
BFS release archives. AmigaOS libraries remain external operating-system
dependencies, not bundled components. Builds that select an unreviewed runtime
input fail inventory generation before publication.
