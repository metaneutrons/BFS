# Target Matrix

This matrix fixes the environments that later milestones must test. A host used
only to build an artifact is not evidence that the artifact can run there.

| Surface | Reference target | Build and runtime rule | Status |
| --- | --- | --- | --- |
| Host core tests | macOS arm64, current Xcode toolchain | Build and run native host tests | Available on the development host |
| Linux FUSE reference | Debian 12 amd64, Linux 6.1, fuse3/libfuse3 3.14.0-4 | Compile with `FUSE_USE_VERSION 30`; require the matching `fuse3` and `libfuse3-dev` packages plus a real `/dev/fuse` mount environment | M5 target |
| Linux build container | Debian 12 amd64 | Pin the Debian 12 image digest in CI before M3; use it for reproducible build-only checks | M3 action |
| Linux FUSE runtime | Dedicated Linux VM or runner with `/dev/fuse` and `CAP_SYS_ADMIN` | Docker Desktop on macOS is not FUSE-mount evidence | External CI/runtime prerequisite |
| Amiga driver | 68080-capable hardware plus the existing 68k build path | Keep current integration suite; hardware evidence remains a separate issue | Existing scope |
| AROS first target | non-SMP `pc-x86_64`, `x86_64-unknown-aros` | Use the checked AROS-NG toolchain manifest and SDK produced by that tree | Available locally |
| AROS runtime | AROS-NG `pc-x86_64` image or bootable target | Packet-level adapter test after core extraction | Deferred after M6/M7 design |
| MorphOS | PowerPC MorphOS with an officially licensed SDK and hardware or supported emulator | Native ABI, SDK, runtime and test media must be supplied before implementation | Blocked: none is present in this workspace |

The local AROS-NG checkout records a macOS-aarch64 host artifact for
`pc-x86_64`, target triple `x86_64-unknown-aros`, LLVM 11.0.0, and a SHA-256
locked toolchain archive. M7 must cite the exact lock entry and the AROS source
revision in its evidence. It must not reuse the `BFS_AMIGA` preprocessor branch:
that branch assumes 68k big-endian execution, while the selected AROS target is
little-endian x86_64.

MorphOS is intentionally a blocker, not a guessed target. Before M8, obtain
and record the MorphOS SDK version, target compiler, ABI conventions, SDK
licence basis, runtime version, hardware or emulator access, and a fixture
transfer method. Without those inputs, only portable-core preparation may be
merged.

The Linux baseline uses the libfuse 3 low-level API and the API version constant
`30`. Debian 12's pinned package revision is `3.14.0-4`; a later package may be
used only when it preserves that ABI selection and the CI lock is deliberately
updated. M5 test evidence must come from an actual Linux FUSE mount, not from a
mocked libfuse callback or macFUSE.
