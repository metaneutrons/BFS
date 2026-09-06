# AmigaDOS packet contracts

The assembler entry allocates and installs the handler stack before calling C.
The stack descriptor remains on the original stack, addressed through a
callee-preserved register, until return and restoration. Switching inside a C
function is invalid here: generated SP-relative locals otherwise refer outside
the new stack allocation. Allocation failure replies to the startup packet on
the original stack with `ERROR_NO_FREE_STORE`.

The handler uses classic DOS packets for ordinary file operations. Shared input
and read/write opens keep independent positions and observe completed changes
through the core's inode refresh. A new-file open is exclusive. Read, write and
delete protection bits are low-active, and content changes clear the archive bit.
Resizing preserves the caller's position except when truncation moves EOF below
it; other open handles constrain how far a file can shrink.

64-bit DOS extensions have two distinct wire contracts. They must not be decoded
as pairs of ordinary packet arguments:

- MorphOS seek/resize use packets 26400/26401 with an input 64-bit pointer in
  argument 2, mode in argument 3, and output 64-bit pointer in argument 4.
  Success is a Boolean in `dp_Res1`, with `dp_Res2` carrying errors.
- MorphOS examine packets are 26408 (lock), 26409 (next) and 26410 (handle).
  Sizes and block counts occupy the first 16 reserved FIB bytes at offsets
  228 and 236. Packet 26407 is an attribute query, not an examine operation.
- OS4-style packets 8001 through 8004 use a separate 64-byte packet with an
  acknowledgement marker of -3, an error at offset 16, and a 64-bit result at
  offset 24. Change operations return a Boolean; getters return the value.

`src/amiga/dos_packets.h` asserts the m68k layout at compile time. Direct guest
packet tests cover the boundaries independently of the installed dos.library's
choice of protocol. This is wire-level testing under AROS, not qualification of
native MorphOS, OS4 or physical Apollo hardware.

## References

- [AmigaOS SetProtection](https://developer.amigaos3.net/autodocs/dos.library/SetProtection.html)
- [AmigaOS SetFileSize](https://developer.amigaos3.net/autodocs/dos.library/SetFileSize.html)
- [AROS DOS64 definitions](https://github.com/aros-development-team/AROS/blob/master/compiler/include/dos/dos64.h)
- [PFS3 OS4 packet handling](https://github.com/aros-development-team/AROS/blob/master/rom/filesys/pfs3/fs/dd_funcs.c)
- [WinUAE packet identifiers and FIB layout](https://github.com/tonioni/WinUAE/blob/master/filesys.cpp)
- [Free Pascal MorphOS packet bindings](https://gitlab.com/freepascal.org/fpc/source/-/blob/main/packages/morphunits/src/amigados.pas)
