# Compiler flags

BFS is built by its own Makefile and, later, by other build systems such as an
AROS-NX package. This document separates the flags every build must set from
the policy of BFS's own builds, so that another build system can use its own
optimisation and warning settings without breaking BFS or being broken by it.

## Required flags

Every build sets these; without them BFS does not compile or selects the wrong
platform code.

| Platform | Definitions | Include paths | Language |
| --- | --- | --- | --- |
| Host (Linux, macOS) | `-DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L` | `-I include` | C99 or later |
| AmigaOS m68k | `-DBFS_AMIGA=1`, `-DBFS_VERSION="x.y.z"` for `bfs` | `-Isrc/amiga -I include`, the NDK | C99 or later |
| AROS | `-DBFS_AROS=1 -DBFS_VERSION="x.y.z"` | `-iquote src/amiga -I include`, the AROS SDK | GNU C99 or later |

On m68k `-Isrc/amiga` also supplies the small C library headers that libnix
lacks. On AROS it must be `-iquote`, so that the SDK's own standard headers are
used. The AROS SDK headers rely on GNU extensions; `-std=c99` fails in them,
while `-std=gnu99` through `-std=gnu2x` work.

The Makefile names the AROS part `AROS_REQUIRED_CFLAGS`.

## What BFS does not depend on

Tested on 10 October 2026 with the AROS-NX flags below and the host suites:

- the optimisation level (`-O2`, `-O3`, `-Os`);
- `NDEBUG`: BFS uses no `assert`;
- strict aliasing on or off (`-fno-strict-aliasing`);
- the C dialect from C99 to C2x, within the limit above for AROS;
- the macros `AMIGA`, `_AMIGA` and `__AROS__`, which BFS does not test.

## Policy of BFS's own builds

BFS's own builds and CI reject every warning (`-Wall -Wextra -Werror`); the m68k
handler also limits stack frames to 16 KiB (`-Wframe-larger-than=16384`), since
it runs on a shared 128 KiB stack.

The NDK and the AROS SDK declare the string parameters of exec, dos and
intuition as `unsigned char` pointers (`STRPTR`, `CONST_STRPTR`). The handler
and `bfs` convert C strings only at that boundary, with `DOS_CSTR` and
`DOS_STR` from `src/amiga/dos_string.h`, and build without `-Wno-pointer-sign`.
Test programs, probes and benchmarks pass C strings directly and keep
`-Wno-pointer-sign` (`AMIGA_WARNINGS`, `AMIGA_TEST_TOOL_FLAGS`, and the AROS
`bfs-test` object).

## Foreign builds

A build system that packages BFS sets the required flags and its own policy. It
must not add `-Werror`: a newer compiler brings new warnings, and one harmless
warning would then stop the whole system build. BFS instead checks the foreign
policy itself, with `-Werror`, so a warning is fixed in BFS before a packager
sees it.

`make aros-nx-check AROS_ROOT=<AROS-NX checkout>` compiles the handler and
`bfs` with the flags AROS-NX uses for programs and disk handlers (`-O3
-DNDEBUG -fno-strict-aliasing`, the SDK's `aros/stdc` headers, `-Wall -Wextra
-Wno-unused-parameter`, no `-std`), taken from the `compile_commands.json` of
AROS-NX 74c85fa, plus `-Werror`. It needs a `pc-x86_64` build of that checkout.
The check is not in CI yet, because AROS-NX publishes no SDK with a fixed
identity.

## Targets

| Compiler | Where | Checked by |
| --- | --- | --- |
| Host clang and GCC | macOS, Linux | CI host jobs, `-Werror` |
| GCC 16 | Linux | locally (11 October 2026) |
| m68k-amigaos-gcc 6.5 | AmigaOS handler, `bfs`, tests | CI Amiga job, `-Werror` |
| AROS clang 11, x86_64 | AROS handler, `bfs`, `bfs-test` | `make aros`, `make aros-nx-check`; not in CI |
| AROS clang 11, arm and aarch64 | AROS-NX presets `arm-raspi`, `rpi-aarch64` | core only, locally with approximated headers |

AROS-NX builds every package for `pc-x86_64`, `arm-raspi` and `rpi-aarch64`.
BFS is qualified on none of them natively yet; the x86_64 port comes first
(M8 in the [cross-platform plan](plans/cross-platform-development.md)). An
AROS-NX package should therefore enter the default build of `pc-x86_64` only,
and the ARM targets only after their own qualification.
