# On-disk compatibility contract

The normative byte layout is [BFS v3 on-disk format](on-disk-format.md). This
document defines compatibility and refusal behavior on top of that layout.

The current on-disk format is **v3**, independently of the driver release
version. Version 3 replaced version 2 by extending the inode record with
inline extents and inode flags. This driver neither reads, writes nor migrates
v2 volumes, and the format does not add 64-bit block addresses.

## Recognition and refusal

The superblock is a 240-byte big-endian structure in a 512-byte slot. Magic is
at byte 0, version at byte 4, options at byte 56, and CRC32 at byte 236. CRC32
covers bytes 0 through 235. The remaining slot bytes are zeroed on writes and
are **not** a backward-compatible extension area. Versions 2 and 3 share this
envelope, so each driver recognizes the other's volumes as intact and refuses
them.

After checking magic and CRC, any version other than 3, older or newer, or an
unknown option bit returns
`BFS_ERR_UNSUPPORTED`. An intact incompatible copy vetoes selection of the
other superblock, irrespective of transaction order or device-geometry match.
Block-size probing must preserve that refusal. Mount returns before namespace
loading or snapshot-deletion recovery. Normal superblock writes also refuse an
incompatible input or stored copy, and abort if either slot cannot be read.

A failed CRC remains corruption, including damage to version/options. The
intact v3 copy may still be used for ordinary recovery. CRC32 detects accidental
damage; it is not authentication against deliberate media modification.

Format completion now publishes the finished filesystem to both superblock
slots. Previously a freshly formatted volume could retain a bootstrap-only
backup until a later commit, so losing its primary immediately after formatting
left no mountable namespace. Regression tests damage either fresh copy at
1024-, 4096-, and 65536-byte block sizes and mount the surviving copy.

The Amiga handler retains unsupported-format errors across failed mount and
remount attempts, maps them to `ERROR_NOT_IMPLEMENTED`, and refuses ordinary
file operations on that medium. It refuses `ACTION_FORMAT` unless the medium
carries only older BFS formats (see below). `bfs check`, including `--repair`,
reports the incompatibility without modifying the image. The explicit
low-level format API and the host `bfs format IMAGE` command remain
destructive initialization paths, not migration or recovery paths.

The diagnosis names the detected version and the version supported by this
driver. Older versions, newer versions and unknown option bits have distinct
text; for an older version it advises copying the data with a compatible
driver before formatting.
An interactive Amiga caller receives a one-time requester on an attempted file
operation or format. A caller with `pr_WindowPtr == -1` suppresses that requester;
headless operation does not require Intuition. Packet `BFS_ACTION_FORMAT_ERROR`
returns the same bounded text, including while unmounted. `bfs format` queries it
before attempting a format. See [packet contracts](amiga-packets.md).

## Replacing an older format

Volumes formatted with on-disk format v2 are readable by v0.1 drivers only. To
move such a volume to v3, copy its data with a v0.1 driver, install this
driver, and format the volume.

Formatting is possible on the Amiga because the handler accepts
`ACTION_FORMAT` on a medium that holds only older BFS formats: at least one
intact superblock copy carries an older version, and no intact copy in slot A
or in slot B at any representable geometry carries a newer version or an
unknown option bit. Damaged copies do not count either way. For such a medium
`BFS_ACTION_FORMAT_ERROR` returns `BFS_FORMAT_REPLACEABLE` as its secondary
result, so `bfs format` prints the diagnosis and then formats. A newer format
is never overwritten through the handler. Formatting stays an explicit,
destructive command, as it is for a foreign filesystem on the same partition.

## Geometry

Filesystem geometry is calculated in 64 bits and rejected with
`BFS_ERR_OVERFLOW` if the chosen block size needs more than `UINT32_MAX` blocks.
Failed selection leaves the previous geometry untouched. Probing skips block
sizes that cannot represent the partition, so large partitions may still be
mounted with a supported larger block size. A trailing partial filesystem block
remains unused. Format checks the proposed geometry
before unmounting or changing the cache.

These are representability checks, not qualification of maximum-size volumes.
Memory use, checker scalability, AmigaDOS interfaces, and device capabilities
remain separate limits.

## Requirements for a future format

- Freeze the v3 layout. New block-width or tree layouts require a new format.
- To obtain the explicit unsupported-format refusal of v2 and v3 drivers,
  retain the recognition envelope at the superblock locations. A different
  checksum/header scheme is not covered by this guarantee and needs a separate
  downgrade-safety design before publication.
- v3 keeps the option word as its only feature mechanism; every unknown bit is
  incompatible, and unknown inode flag bits are corruption. Version 3 did not
  add compatible or read-only-compatible feature masks because no feature
  exists that an older v3 driver could safely ignore. A format that needs them
  defines them in a new version; they must not be retrofitted into padding.
- A future driver may support several formats through separate codecs.
  Ordinary mounts must not silently upgrade an existing volume.
- Prefer copying to a new volume for migration, as v2 to v3 does. Published
  drivers through v0.1.1 cannot acquire these checks retroactively: any future
  in-place migration must prevent both superblock locations from exposing a
  usable stale filesystem, including after interruption.

Regression coverage includes mixed copies in both directions, unknown option
bits, checksum-damaged version/options, zero-write mount/commit refusal,
byte-preserving checker refusal for older and newer versions, the v3 golden
superblock, the replaceable classification of older, newer, mixed and damaged
copies, and sparse-device geometry boundaries without allocating
multi-terabyte images.

`make compatibility-test` boots isolated AROS/FS-UAE media for valid v3, a v2
medium that `bfs format` replaces, newer versions in either slot, unknown
options in either slot, and a damaged version with a recoverable v3 peer. It
checks the packet diagnosis, suppressed requesters, write/format refusal,
byte-identical incompatible images after shutdown, and the replaced medium.
Evidence is retained under `build/emulator/run.compat-*`.
