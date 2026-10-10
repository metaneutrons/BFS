# SPDX-License-Identifier: MPL-2.0
"""Keep raw B+tree access to inode trees in the places that handle pending inodes.

The live inode tree holds inodes published in the current transaction only in
its pending table until the commit (docs/plans/bfs-inode-write-back-v1.md).
Every reader must go through src/core/inode.c, which returns the pending copy;
a raw bfs_btree_* call on an inode tree would see stale size and extents. The
few raw walks that exist either write the table first or run where it is
empty. A new one must be reviewed and added here. The check matches calls
by the function and the name of their first argument, so helpers that take an
inode tree under another name (fsck's payload scan, snapshot_ref_walk,
bfs_fs_compact_tree) are reviewed with their caller.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RAW_CALL = re.compile(r"\bbfs_btree_\w+\(\s*&?[\w.>-]*inode_tree\b")
ALLOWED = {
    # Snapshot-record trees have no table; the live check overlays the
    # pending copies through bfs_inode_pending_peek.
    "src/core/fsck.c": ["bfs_btree_walk_nodes(&inode_tree",
                        "bfs_btree_walk_nodes(&fs->inode_tree"],
    # bfs_snapshot_create commits, and so empties the table, before the walk.
    "src/core/snapshot.c": ["bfs_btree_scan(inode_tree"],
    # Mount-time scan for unlinked inodes, before any publication.
    "src/core/namespace.c": ["bfs_btree_scan(&fs->inode_tree"],
}


class InodeTreeAccessTest(unittest.TestCase):
    def test_raw_inode_tree_calls_are_reviewed(self):
        found = {}
        for path in sorted((ROOT / "src").rglob("*.c")):
            relative = path.relative_to(ROOT).as_posix()
            if relative == "src/core/inode.c":
                continue
            calls = sorted(RAW_CALL.findall(path.read_text(encoding="utf-8")))
            if calls:
                found[relative] = calls
        self.assertEqual(found, {name: sorted(calls) for name, calls in ALLOWED.items()})


if __name__ == "__main__":
    unittest.main()
