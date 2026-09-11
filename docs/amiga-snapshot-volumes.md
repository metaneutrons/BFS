# AmigaOS snapshot volumes

An existing BFS snapshot can be exposed as a separate, permanently read-only
DOS volume:

```text
bfs snapshot mount DRIVE: SNAPSHOT-NAME TARGET:
bfs snapshot unmount TARGET:
```

For example, `bfs snapshot mount Work: before-upgrade SNAP:` makes the
snapshot root available below `SNAP:`. It never exposes the live root below
that name. `unmount` sends `ACTION_DIE` to the mounted snapshot handler and
does not return until its source-handler pin has been released.

## Safety and lifetime

`mount` resolves the requested name to its immutable on-disk record before it
creates the target DOS device. The snapshot handler opens its namespace from
that record's directory and inode roots, not from the current live
superblock. Its block I/O is read-only and it rejects mutation packets,
formatting, write-protection changes, and snapshot-administration packets
with `ERROR_DISK_WRITE_PROTECTED`.

The source handler owns one pin for each mounted record. While a pin exists,
deleting that snapshot and shutting down the source handler return
`ERROR_OBJECT_IN_USE`. A clean snapshot-handler exit releases the pin, removes
the temporary DOS device node, and permits deletion. If the mounted handler is
terminated unexpectedly, the source retains the pin conservatively; it will
not reclaim or delete the snapshot merely because the worker is no longer
responsive.

The mount path creates all resources before publishing the target volume. A
failed start removes its private DeviceNode and startup allocation; no
live-root fallback or implicit handler respawn exists.

## Packet and startup contract

`src/amiga/snapshot_protocol.h` is the single packet-number definition shared
by the handler and `bfs` command. `BFS_ACTION_SNAPSHOT_CAPABILITY` (3006)
returns protocol version 1 and one of:

- `BFS_SNAPSHOT_CAP_MOUNT_SOURCE` for a live BFS handler;
- `BFS_SNAPSHOT_CAP_MOUNTED_VIEW` for a mounted snapshot handler.

The command queries this capability before mounting or unmounting, so older
handlers fail cleanly rather than receiving an incompatible packet.

`src/amiga/snapshot_mount.h` defines the versioned private startup extension.
The regular `FileSysStartupMsg` keeps its documented DOS meanings. A private
`ACTION_STARTUP` packet identifies the extension through its `dp_Arg5` magic
and uses `dp_Arg4` only for the versioned extension pointer; it does not
repurpose a DOS node or filesystem-startup field. The source owns this memory
until the snapshot handler has stopped and released its pin.

## Verification

`emulator-test/compatibility-test.py` starts the public `bfs` command under
AROS, creates a snapshot, mounts it as `SNAP:`, unmounts it, and then deletes
it. Its AmigaDOS probe locks, examines, opens, reads, validates, and closes a
snapshot file before and after the live copy is deleted. It also proves that
deletion remains blocked while mounted, write opens and format packets are
rejected with `ERROR_DISK_WRITE_PROTECTED`, and the successful final delete
demonstrates that handler shutdown released the pin. The test checks the exact
public mount command output.
