#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exact diagnostic counts and separate inclusive times; never sum scopes."""

from pathlib import Path


def load(path):
    pairs = [line.split("\t", 1) for line in path.read_text().splitlines()]
    rows = dict(pairs)
    if len(rows) != len(pairs) or rows.get("PASS") != "1":
        raise ValueError(f"duplicate rows or missing PASS: {path}")
    if rows.get("FS_DEEP_COMPARE") != "12" or rows.get("DRIVE") != "DH1:":
        raise ValueError(f"unexpected schema/drive: {path}")
    return {key: int(value) for key, value in pairs if key != "DRIVE"}


if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    label = "metadata-deep-v12-20261008-m3"
    expected = {f"{label}-1-bfs-first", f"{label}-2-pfs3-first"}
    actual = {path.name for path in root.glob(f"{label}-*") if path.is_dir()}
    if expected != actual:
        raise ValueError(f"unexpected run inventory: {actual ^ expected}")
    print("run\tphase\tphase_us\traw_reads\traw_read_us\tcache_reads\t"
          "cache_hits\tcache_misses\tinode_reads\tindex_hits\tleaf_hits\t"
          "inode_views\tinode_resident\tdir_views\tdir_resident\tcrc_reads\t"
          "crc_samples\tcrc_sample_us\tpacket_us\tother_packet_us")
    for name in sorted(expected):
        rows = load(root / name / "system/Results/bfs.deep-compare.tsv")
        hz = rows["CLOCK_HZ"]
        if hz <= 0:
            raise ValueError("nonpositive clock frequency")
        phases = [key[:-3] for key in rows if key.endswith("_US")]
        for phase in phases:
            def value(suffix):
                return rows[f"{phase}_{suffix}"]

            def microseconds(suffix):
                return f"{value(suffix) * 1000000 / hz:.1f}"

            fields = [value("US"), value("BIO_READS"), microseconds("READ_TICKS"),
                      value("CACHE_READ_CALLS"), value("CACHE_READ_HITS"),
                      value("CACHE_READ_MISSES"), value("INODE_READ_CALLS"),
                      value("BTREE_INDEX_HINT_HITS"), value("BTREE_LEAF_HINT_HITS"),
                      value("INODE_TREE_NODE_VIEWS"), value("INODE_TREE_RESIDENT_VIEWS"),
                      value("DIR_TREE_NODE_VIEWS"), value("DIR_TREE_RESIDENT_VIEWS"),
                      value("NODE_CRC_READ_CALLS"), value("NODE_CRC_READ_SAMPLES"),
                      microseconds("NODE_CRC_READ_SAMPLE_TICKS"),
                      microseconds("PACKET_SAMPLE_TICKS"),
                      microseconds("PACKET_OTHER_SAMPLE_TICKS")]
            print("\t".join(map(str, [name, phase, *fields])))
