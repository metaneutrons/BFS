#!/usr/bin/env python3
"""Read-only independent inspector for committed BFS v2 images.

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
SUPPORTED_OPTIONS = 0x7
MIN_BLOCK = 1024
MAX_BLOCK = 65536
MAX_WALK_NODES = 1_000_000
ZERO_CHUNK = b"\0" * 65536
LAYOUTS = {
    "directory": (264, 8),
    "inode": (4, 44),
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
    if version != 2:
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
    if kind == "directory":
        name_length = key[8]
        if name_length == 0 or any(key[9 + name_length:]):
            raise OracleError("invalid directory key name encoding")
        if be32(key, 4) != fnv1a(key[9:9 + name_length]):
            raise OracleError("directory key hash mismatch")


def key_sort_key(kind, key):
    if kind != "directory":
        return key
    name_length = key[8]
    return (be32(key, 0), be32(key, 4), fold_name(key[9:9 + name_length]), name_length)


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


def digest_file(image, superblock, inode):
    size = be64(inode, 8)
    root = be32(inode, 16)
    extents = walk_tree(image, superblock, root, "extent") if root else []
    digest = hashlib.sha256()
    remaining = size
    cursor = 0
    for key, value in extents:
        logical = be32(key, 0) * superblock["block_size"]
        disk, length = be32(value, 0), be32(value, 4)
        if length == 0 or disk + length > superblock["block_count"] or logical < cursor:
            raise OracleError("invalid extent range")
        while cursor < min(logical, size):
            count = min(len(ZERO_CHUNK), min(logical, size) - cursor)
            digest.update(ZERO_CHUNK[:count])
            cursor += count
        for index in range(length):
            if cursor >= size:
                break
            data = block_at(image, superblock, disk + index)
            count = min(len(data), size - cursor)
            digest.update(data[:count])
            cursor += count
    while cursor < remaining:
        count = min(len(ZERO_CHUNK), remaining - cursor)
        digest.update(ZERO_CHUNK[:count])
        cursor += count
    return digest.hexdigest()


def build_manifest(image, superblock):
    inode_values = {be32(key, 0): value for key, value in
                    walk_tree(image, superblock, superblock["inode_root"], "inode")}
    directory = walk_tree(image, superblock, superblock["directory_root"], "directory")
    entries = {}
    for key, value in directory:
        parent, name_length = be32(key, 0), key[8]
        name = key[9:9 + name_length]
        if parent & 0x80000000 or name == b"..":
            continue
        entries.setdefault(parent, []).append((name, be32(value, 0), be32(value, 4)))
    if 1 not in inode_values:
        raise OracleError("root inode missing")
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
