# Native MorphOS build

`make morphos` cross-compiles BFS for native MorphOS (PowerPC) and produces
`build/morphos/bfshandler`, `bfs` and `bfs-test`. This page records what that
build shows and what only a MorphOS system can show. The port is not
qualified: no MorphOS runtime or hardware has run any of it, and M9 stays open
(#44).

## Build

The compiler is `ppc-morphos-gcc` of
[morphos-cross](https://github.com/metaneutrons/morphos-cross), installed with
the MorphOS SDK it pins. Neither the SDK nor any file from it belongs in this
repository.

```sh
export PATH="$HOME/.morphos-cross/current/bin:$PATH"
make morphos
```

The flags are `-std=c99 -Wall -Wextra -Werror -Wframe-larger-than=16384 -O2
-noixemul`, with the include path of `src/amiga` for quoted includes only, so
the SDK's own `<stdint.h>`, `<string.h>` and `<stdlib.h>` apply. `-DBFS_MORPHOS=1`
selects the native branches. `BFS_AMIGA` is not defined: the byte order comes
from the compiler (`__BYTE_ORDER__`), and the portable `src/core/crc32.c`
replaces the 68k assembly.

The handler is linked with `-nostartfiles`. `src/amiga/startup_morphos.c`
provides the ELF entry point `__start` and is linked first, so the entry leads
the text. The heap is `src/amiga/heap_aros.c`, exec's `AllocVec`, as on AROS.
`bfs` and `bfs-test` use the standard startup of the libnix flavour.

The `MorphOS Cross Build` job in `.github/workflows/ci.yml` runs `make morphos`
with these flags on Ubuntu x86-64 for every pull request and push to `main`, and
the required `CI Success` check includes it. The job installs the morphos-cross
release pinned in `tools/morphos-cross.pins` (archive digest verified) and the
SDK that release pins, caches the SDK archive between runs and verifies it
against the SHA-256 of the release. Then `tools/check-morphos-elf.sh` confirms
with `readelf -h` that the three outputs are 32-bit big-endian PowerPC ELF
files. A green job shows that the sources compile and link for MorphOS. It is
no runtime evidence.

## What the build shows

- All sources of the handler, the core and the commands compile without
  warnings under the flags above. `bfs-test` keeps `-Wno-pointer-sign`, as on
  the other targets.
- The static assertions on on-disk structure sizes, on the `FileInfoBlock`
  offset and on the 64-bit packet layout hold for the PowerPC ABI.
- The handler links without unresolved symbols. From the libraries it takes
  `memcpy`, `memmove`, `memset`, `memcmp`, `strcmp`, `strlen` and their
  helpers from libnix, `__udivdi3` and `__umoddi3` from libgcc and the
  `CreateNewProcTags` stub.
- The output is the relocatable ELF that the MorphOS linker writes for
  executables, and `__start` is at offset 0 of the text.
- libnix chooses between scalar and AltiVec string functions through the
  variable `libnix_use_altivec`. The handler has no startup code that could set
  it, so it stays zero in the BSS, and the disassembly of `strlen` branches to
  the AltiVec variant only for a non-zero value. The handler therefore runs the
  scalar functions and needs no vector context.
- The two interrupt handlers (disk change and keyboard reset) have a native
  form. Exec calls interrupts with 68k registers, so each is reached through a
  gate (`struct EmulLibEntry`, `TRAP_LIB`) and reads its server data from the
  emulated register a1.

## What the build does not show

Each point needs a MorphOS system, and M9-A2 and M9-A3 name them.

- How DOS starts the handler: the startup packet on the process port, the
  entry through the 68k shim in front of the PowerPC code, and the effect of
  the Mountlist and the `DosEnvec` on a native handler.
- The stack. The m68k handler switches to a private 128 KiB stack. The native
  handler runs on the stack the system gives a PowerPC process, and its size
  is not verified. Frames stay below 16 KiB, but the whole call chain of the
  core has no measured bound here.
- The interrupt gates: that the disk-change and reset gates run, that `Signal`
  from them wakes the handler, and that devices accept the gate as `is_Code`
  for `TD_ADDCHANGEINT`.
- Packet serialization. The core's native locks are no-ops, which is sound
  only while one task calls the core.
- Alignment. The core reads and writes on-disk structures through typed
  pointers. The 68020 and later tolerate misaligned words, and the PowerPC
  handles most but not all of them in hardware, so an unaligned access may be
  slow or trap. No alignment audit of the core exists for this target.
- Cache and DMA ownership of the buffers that go to the device, and the device
  commands (`TD64`, NSD, SCSI direct) of the MorphOS drivers.
- That MorphOS `dos.library` sends the 64-bit packets (8001 to 8004 and 26400
  to 26410) and the `FileInfoBlock` extension as `docs/amiga-packets.md`
  describes. The numbers follow the MorphOS conventions, and no MorphOS DOS
  has confirmed them.
- The OS minimum, which M2-A3 asks to name, and the hardware configuration
  for M9-A3.

## Source changes

`BFS_MORPHOS` adds `src/amiga/startup_morphos.c` and the interrupt gates in
`src/amiga/handler.c`. The MorphOS SDK declares `STRPTR` as `char *` while
the string fields of its structures (`EasyStruct`, `ExAllControl`,
`DateTime`) stay pointers to `UBYTE`. `DOS_UBYTES` in `src/amiga/dos_string.h`
marks a C string stored in such a field, and the three call sites that passed a
`UBYTE` pointer to a `CONST_STRPTR` parameter convert with `DOS_CSTR`. These
casts change nothing on the NDK and AROS.
