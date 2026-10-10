#!/usr/bin/env python3
"""Read-only independent inspector for committed BFS v3 images.

This intentionally duplicates only documented byte rules. It imports no BFS
module, has no writer path, and does not reuse the production tree engine.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys
import zlib


MAGIC = 0x42465300
NODE_MAGIC = 0x42544E44
FORMAT_VERSION = 3
SUPPORTED_OPTIONS = 0x7
INODE_INLINE_EXTENT = 0x1
INODE_HAS_COMMENT = 0x2
INODE_LIMIT = 0x80000000
DIR_ENTRY, DIR_PARENT, DIR_COMMENT = 0, 1, 2
DIR_INLINE_NAME = 33
DIR_PART_BYTES = 40
DIR_COMMENT_INLINE = 39
MIN_BLOCK = 1024
MAX_BLOCK = 65536
MAX_WALK_NODES = 1_000_000
ZERO_CHUNK = b"\0" * 65536
LAYOUTS = {
    "directory": (12, 40),
    "inode": (4, 56),
    "extent": (4, 12),
    "free": (4, 4),
    "refcount": (4, 4),
    "snapshot": (4, 52),
}


class OracleError(Exception):
    pass


def be16(data, offset):
    return int.from_bytes(data[offset:offset + 2], "big")


def be32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def be64(data, offset):
    return int.from_bytes(data[offset:offset + 8], "big")


def crc32(data):
    return zlib.crc32(data) & 0xffffffff


def valid_block_size(value):
    return MIN_BLOCK <= value <= MAX_BLOCK and value & (value - 1) == 0


def parse_superblock(slot, image_size):
    if len(slot) != 512 or be32(slot, 0) != MAGIC:
        return None
    if crc32(slot[:236]) != be32(slot, 236):
        return None
    version = be32(slot, 4)
    block_size = be32(slot, 8)
    block_count = be32(slot, 12)
    options = be32(slot, 56)
    if version != FORMAT_VERSION:
        raise OracleError(f"unsupported BFS version {version}")
    if options & ~SUPPORTED_OPTIONS:
        raise OracleError(f"unsupported BFS options 0x{options & ~SUPPORTED_OPTIONS:08x}")
    if not valid_block_size(block_size) or block_count == 0:
        return None
    if block_count * block_size != image_size:
        return None
    backup = be64(slot, 96)
    if backup != image_size // 2:
        return None
    if be64(slot, 16) == 0 or not (2 <= be32(slot, 60) <= 0x80000000):
        return None
    if any(slot[240:]):
        return None
    return {
        "transaction_id": be64(slot, 16),
        "block_size": block_size,
        "block_count": block_count,
        "directory_root": be32(slot, 24),
        "free_root": be32(slot, 32),
        "inode_root": be32(slot, 36),
        "refcount_root": be32(slot, 40),
        "snapshot_root": be32(slot, 44),
        "free_blocks": be32(slot, 48),
        "options": options,
        "backup_offset": backup,
    }


def select_superblock(image):
    if len(image) < 1024:
        raise OracleError("image is too small")
    first = parse_superblock(image[:512], len(image))
    backup_offset = first["backup_offset"] if first else len(image) // 2
    if backup_offset + 512 > len(image):
        raise OracleError("backup slot is outside image")
    second = parse_superblock(image[backup_offset:backup_offset + 512], len(image))
    if first is None and second is None:
        raise OracleError("no valid compatible superblock")
    if first is None:
        return second
    if second is None:
        return first
    if (first["block_size"], first["block_count"], first["backup_offset"]) != \
       (second["block_size"], second["block_count"], second["backup_offset"]):
        raise OracleError("superblock geometry disagreement")
    return second if second["transaction_id"] > first["transaction_id"] else first


def block_at(image, superblock, block):
    if not 0 < block < superblock["block_count"]:
        raise OracleError("tree pointer is outside geometry")
    start = block * superblock["block_size"]
    return image[start:start + superblock["block_size"]]


def fold_name(name):
    return bytes(byte - 32 if 97 <= byte <= 122 or 0xe0 <= byte <= 0xfe and byte != 0xf7
                 else byte for byte in name)


def fnv1a(name):
    result = 0x811c9dc5
    for byte in fold_name(name):
        result = (result ^ byte) * 0x01000193 & 0xffffffff
    return result


def validate_key(kind, key):
    if kind != "directory":
        return
    owner, record, part = be32(key, 0), key[4], key[11]
    if owner >= INODE_LIMIT or record > DIR_COMMENT:
        raise OracleError("invalid directory key")
    if record == DIR_ENTRY and part > 6 or \
            record != DIR_ENTRY and (be32(key, 5) or be16(key, 9)) or \
            record == DIR_PARENT and part or record == DIR_COMMENT and part > 1:
        raise OracleError("invalid directory key")


def key_sort_key(kind, key):
    # Every key field is big-endian, so byte order is the comparator of every
    # tree, the directory tree included.
    return key


def walk_tree(image, superblock, root, kind):
    if root == 0:
        return []
    key_size, value_size = LAYOUTS[kind]
    seen = set()
    leaves = []

    def descend(block, expected_level=None):
        if block in seen:
            raise OracleError(f"{kind} tree cycle")
        if len(seen) >= MAX_WALK_NODES:
            raise OracleError(f"{kind} tree walk limit exceeded")
        seen.add(block)
        node = block_at(image, superblock, block)
        if be32(node, 0) != NODE_MAGIC:
            raise OracleError(f"{kind} tree node magic mismatch")
        protected = node[:4] + b"\0\0\0\0" + node[8:]
        if crc32(protected) != be32(node, 4):
            raise OracleError(f"{kind} tree node CRC mismatch")
        count, level, flags, sibling = be32(node, 16), be16(node, 20), be16(node, 22), be32(node, 24)
        if level >= 32 or flags != 0 or count == 0 or expected_level is not None and level != expected_level:
            raise OracleError(f"{kind} tree node header is invalid")
        leaf = level == 0
        capacity = (len(node) - 28) // (key_size + value_size) if leaf else \
            (len(node) - 32) // (key_size + 4)
        if count > capacity or sibling and (not leaf or sibling >= superblock["block_count"]):
            raise OracleError(f"{kind} tree node capacity is invalid")
        keys = [node[28 + index * key_size:28 + (index + 1) * key_size] for index in range(count)]
        for key in keys:
            validate_key(kind, key)
        if any(key_sort_key(kind, keys[index]) >= key_sort_key(kind, keys[index + 1])
               for index in range(len(keys) - 1)):
            raise OracleError(f"{kind} tree keys are unordered")
        if leaf:
            values_start = 28 + capacity * key_size
            leaves.extend((key, node[values_start + index * value_size:
                                     values_start + (index + 1) * value_size])
                          for index, key in enumerate(keys))
            return
        children_start = 28 + capacity * key_size
        for index in range(count + 1):
            child = be32(node, children_start + index * 4)
            descend(child, level - 1)

    descend(root)
    return leaves


def inode_extents(image, superblock, inode):
    """Return the inode's extent records: one inline record or a tree's."""
    flags = be32(inode, 44)
    root, inline_length, inline_crc = be32(inode, 16), be32(inode, 48), be32(inode, 52)
    if flags & ~(INODE_INLINE_EXTENT | INODE_HAS_COMMENT):
        raise OracleError("unknown inode flags")
    if not flags & INODE_INLINE_EXTENT:
        if inline_length or inline_crc:
            raise OracleError("inline fields without inline extent")
        return walk_tree(image, superblock, root, "extent") if root else []
    if root == 0 or inline_length == 0:
        raise OracleError("invalid inline extent")
    return [(bytes(4), inode[16:20] + inode[48:56])]


def digest_file(image, superblock, inode):
    size = be64(inode, 8)
    extents = inode_extents(image, superblock, inode)
    digest = hashlib.sha256()
    remaining = size
    cursor = 0
    for key, value in extents:
        logical = be32(key, 0) * superblock["block_size"]
        disk, length = be32(value, 0), be32(value, 4)
        if length == 0 or disk + length > superblock["block_count"] or logical < cursor:
            raise OracleError("invalid extent range")
        if superblock["options"] & 1 and length != 1:
            raise OracleError("checksummed extent length is invalid")
        while cursor < min(logical, size):
            count = min(len(ZERO_CHUNK), min(logical, size) - cursor)
            digest.update(ZERO_CHUNK[:count])
            cursor += count
        for index in range(length):
            if cursor >= size:
                break
            data = block_at(image, superblock, disk + index)
            if superblock["options"] & 1:
                if be32(value, 8) == 0 or crc32(data) != be32(value, 8):
                    raise OracleError("data checksum mismatch")
            count = min(len(data), size - cursor)
            digest.update(data[:count])
            cursor += count
    while cursor < remaining:
        count = min(len(ZERO_CHUNK), remaining - cursor)
        digest.update(ZERO_CHUNK[:count])
        cursor += count
    return digest.hexdigest()


def take_parts(records, index, head_key, count):
    """Return the values of the count continuation parts after records[index]."""
    values = []
    for part in range(1, count + 1):
        if index + part >= len(records):
            raise OracleError("directory record part missing")
        key, value = records[index + part]
        if key[:11] != head_key[:11] or key[11] != part:
            raise OracleError("directory record part missing")
        values.append(value)
    following = index + count + 1
    if following < len(records) and records[following][0][:11] == head_key[:11]:
        raise OracleError("directory record has an extra part")
    return values


def parse_dir_entry(records, index, key, value):
    """Return the name, inode and type of the entry at index and its part count."""
    owner, name_hash, ordinal = be32(key, 0), be32(key, 5), be16(key, 9)
    inode, entry_type, name_length, flags = be32(value, 0), value[4], value[5], value[6]
    if not 0 < inode < INODE_LIMIT or entry_type > 3 or name_length == 0 or flags:
        raise OracleError("invalid directory entry")
    inline = min(name_length, DIR_INLINE_NAME)
    name = bytearray(value[7:7 + inline])
    if any(value[7 + inline:]):
        raise OracleError("directory entry padding is not zero")
    count = 0 if name_length <= DIR_INLINE_NAME else \
        -(-(name_length - DIR_INLINE_NAME) // DIR_PART_BYTES)
    for part_value in take_parts(records, index, key, count):
        used = min(DIR_PART_BYTES, name_length - len(name))
        name += part_value[:used]
        if any(part_value[used:]):
            raise OracleError("directory entry padding is not zero")
    name = bytes(name)
    if fnv1a(name) != name_hash:
        raise OracleError("directory entry hash mismatch")
    if name in (b".", b".."):
        raise OracleError("directory entry has a dot name")
    if owner == 0 and (name != b"/" or inode != 1 or entry_type != 1 or ordinal):
        raise OracleError("invalid root record")
    return (name, inode, entry_type), count


def parse_comment(records, index, key, value):
    """Return the text of the comment at index and its part count."""
    length = value[0]
    if not 1 <= length <= 79:
        raise OracleError("invalid comment record")
    inline = min(length, DIR_COMMENT_INLINE)
    text = bytearray(value[1:1 + inline])
    if any(value[1 + inline:]):
        raise OracleError("comment padding is not zero")
    count = 1 if length > DIR_COMMENT_INLINE else 0
    for part_value in take_parts(records, index, key, count):
        used = length - len(text)
        text += part_value[:used]
        if any(part_value[used:]):
            raise OracleError("comment padding is not zero")
    return bytes(text), count


def directory_parents(entries, parents):
    """Check the root record, case aliases and parent links against the
    entries; return each directory's parent as its entry names it."""
    if entries.get(0) != [(b"/", 1, 1)]:
        raise OracleError("root record missing")
    named = {}
    for owner, items in entries.items():
        if len({fold_name(name) for name, _, _ in items}) != len(items):
            raise OracleError("directory holds case aliases")
        for _, inode, entry_type in items:
            if entry_type == 1 and owner != 0:
                if inode in named:
                    raise OracleError("directory has two names")
                named[inode] = owner
    if named != parents:
        raise OracleError("parent links disagree with directory entries")
    return named


def parse_directory(records):
    """Return entries by directory, comments and parent links of a directory tree."""
    entries, parents, comments = {}, {}, {}
    index = 0
    while index < len(records):
        key, value = records[index]
        owner, record, part = be32(key, 0), key[4], key[11]
        if part != 0:
            raise OracleError("directory record part without its head")
        count = 0
        if record == DIR_ENTRY:
            item, count = parse_dir_entry(records, index, key, value)
            entries.setdefault(owner, []).append(item)
        elif owner == 0 or record == DIR_PARENT and owner == 1:
            raise OracleError("invalid parent link or comment owner")
        elif record == DIR_PARENT:
            parent = be32(value, 0)
            if not 0 < parent < INODE_LIMIT or any(value[4:]):
                raise OracleError("invalid parent link")
            parents[owner] = parent
        else:
            comments[owner], count = parse_comment(records, index, key, value)
        index += count + 1
    return entries, comments, directory_parents(entries, parents)


def build_manifest(image, superblock):
    inode_values = {be32(key, 0): value for key, value in
                    walk_tree(image, superblock, superblock["inode_root"], "inode")}
    directory = walk_tree(image, superblock, superblock["directory_root"], "directory")
    entries, comments, named = parse_directory(directory)
    commented = set(comments)
    if 1 not in inode_values:
        raise OracleError("root inode missing")
    flagged = {number for number, inode in inode_values.items()
               if be32(inode, 44) & INODE_HAS_COMMENT}
    if flagged != commented:
        raise OracleError("comment flag disagrees with comment records")
    for inode in inode_values.values():
        if be32(inode, 4) == 1 and (be32(inode, 16) or be32(inode, 44) & INODE_INLINE_EXTENT):
            raise OracleError("directory inode with extents")
    for owner, items in entries.items():
        if owner != 0 and (owner not in inode_values or be32(inode_values[owner], 4) != 1):
            raise OracleError("directory entry owned by a non-directory")
        for _, inode, entry_type in items:
            if inode not in inode_values or be32(inode_values[inode], 4) != entry_type:
                raise OracleError("directory entry type disagrees with its inode")
    manifest = []
    directory_ancestors = set()

    def visit(inode_number, path):
        inode = inode_values.get(inode_number)
        if inode is None or be32(inode, 0) != inode_number:
            raise OracleError("directory entry points to invalid inode")
        item = {"path": path, "inode": inode_number, "type": be32(inode, 4),
                "size": be64(inode, 8), "links": be32(inode, 20)}
        if item["type"] in (0, 2):
            item["sha256"] = digest_file(image, superblock, inode)
        manifest.append(item)
        if item["type"] == 1:
            if inode_number in directory_ancestors:
                raise OracleError("namespace directory cycle")
            directory_ancestors.add(inode_number)
            for name, child, entry_type in sorted(entries.get(inode_number, [])):
                if entry_type not in (0, 1, 2, 3):
                    raise OracleError("invalid directory entry type")
                component = name.decode("latin-1")
                visit(child, path.rstrip("/") + "/" + component)
            directory_ancestors.remove(inode_number)

    visit(1, "/")
    if set(named) - {item["inode"] for item in manifest if item["type"] == 1}:
        raise OracleError("directory unreachable from the root")
    snapshots = []
    for key, value in walk_tree(image, superblock, superblock["snapshot_root"], "snapshot"):
        name = value[20:].split(b"\0", 1)[0]
        if name.startswith(b".deleting_"):
            continue
        snapshots.append({"id": be32(key, 0), "name": name.decode("latin-1"),
                          "transaction_id": be64(value, 8)})
    return sorted(manifest, key=lambda item: item["path"]), snapshots


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        image = args.image.read_bytes()
        superblock = select_superblock(image)
        namespace, snapshots = build_manifest(image, superblock)
        result = {"format_version": 1, "status": "ok", "superblock": superblock,
                  "namespace": namespace, "snapshots": snapshots}
        exit_code = 0
    except (OSError, OracleError) as error:
        result = {"format_version": 1, "status": "error", "code": str(error)}
        exit_code = 3
    output = json.dumps(result, sort_keys=True, separators=(",", ":"))
    print(output)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
