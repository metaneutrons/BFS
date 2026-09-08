# On-disk compatibility contract

The normative byte layout is [BFS v2 on-disk format](on-disk-format.md). This
document defines compatibility and refusal behavior on top of that layout.

The current on-disk format remains **v2**, independently of the driver release
version. This hardening does not add 64-bit block addresses or migrate volumes.

## Recognition and refusal

The v2 superblock is a 240-byte big-endian structure in a 512-byte slot. Magic is
at byte 0, version at byte 4, options at byte 56, and CRC32 at byte 236. CRC32
covers bytes 0 through 235. The remaining slot bytes are zeroed on writes and
are **not** a backward-compatible extension area.

After checking magic and CRC, an unknown version or unknown option bit returns
`BFS_ERR_UNSUPPORTED`. An intact incompatible copy vetoes selection of the
other superblock, irrespective of transaction order or device-geometry match.
Block-size probing must preserve that refusal. Mount returns before namespace
loading or snapshot-deletion recovery. Normal superblock writes also refuse an
incompatible input or stored copy, and abort if either slot cannot be read.

A failed CRC remains corruption, including damage to version/options. The
intact v2 copy may still be used for ordinary recovery. CRC32 detects accidental
damage; it is not authentication against deliberate media modification.

Format completion now publishes the finished filesystem to both superblock
slots. Previously a freshly formatted volume could retain a bootstrap-only
backup until a later commit, so losing its primary immediately after formatting
left no mountable namespace. Regression tests damage either fresh copy at
1024-, 4096-, and 65536-byte block sizes and mount the surviving copy.

The Amiga handler retains unsupported-format errors across failed mount and
remount attempts, maps them to `ERROR_NOT_IMPLEMENTED`, and refuses ordinary
file operations and `ACTION_FORMAT` on that medium. `bfsfsck`, including
`--fix`, reports the incompatibility without modifying the image. The explicit
low-level format API and `mkbfs` remain destructive initialization tools; they
are not migration or recovery paths.

The diagnosis names the detected version and the version supported by this
driver. Older unsupported versions and unknown option bits have distinct text.
An interactive Amiga caller receives a one-time requester on an attempted file
operation or format. A caller with `pr_WindowPtr == -1` suppresses that requester;
headless operation does not require Intuition. Packet `BFS_ACTION_FORMAT_ERROR`
returns the same bounded text, including while unmounted. `bfsformat` queries it
before attempting a format. See [packet contracts](amiga-packets.md).

## Geometry

Filesystem geometry is calculated in 64 bits and rejected with
`BFS_ERR_OVERFLOW` if the chosen block size needs more than `UINT32_MAX` blocks.
Failed selection leaves the previous geometry untouched. Probing skips block
sizes that cannot represent the partition, so large partitions may still be
mounted with a supported larger block size. A trailing partial filesystem block
remains unused, preserving v2 behavior. Format checks the proposed geometry
before unmounting or changing the cache.

These are representability checks, not qualification of maximum-size volumes.
Memory use, checker scalability, AmigaDOS interfaces, and device capabilities
remain separate limits.

## Requirements for a future format

- Freeze the v2 layout. New block-width or tree layouts require a new format.
- To obtain this driver's explicit unsupported-format refusal, retain a valid
  v2 recognition envelope at the legacy superblock locations. A different
  checksum/header scheme is not covered by this guarantee and needs a separate
  downgrade-safety design before publication.
- Specify a stable header length and compatible, read-only-compatible, and
  incompatible feature masks for the future format; do not retrofit them into
  v2 padding.
- A future driver may support v2 and the new format through separate codecs.
  Ordinary mounts must not silently upgrade an existing volume.
- Prefer copying to a new volume for the first migration implementation.
  Published drivers through v0.1.1 cannot acquire these new checks retroactively:
  any future in-place migration must prevent both legacy superblock locations
  from exposing a usable stale v2 filesystem, including after interruption.

Regression coverage includes mixed copies in both directions, unknown option
bits, checksum-damaged version/options, zero-write mount/commit refusal,
byte-preserving checker refusal, the v2 golden superblock, and sparse-device
geometry boundaries without allocating multi-terabyte images.

`make compatibility-test` boots isolated AROS/FS-UAE media for valid v2,
newer versions in either slot, unknown options in either slot, and a damaged
version with a recoverable v2 peer. It checks the packet diagnosis, suppressed
requesters, write/format refusal, and byte-identical incompatible images after
shutdown. Evidence is retained under `build/emulator/run.compat-*`.
