/*
 * Independent v2 byte fixtures. Do not include BFS layout headers or call
 * production codecs here: this test is meant to catch shared-layout mistakes.
 */

#include "test_harness.h"

#include <stdbool.h>
#include <stdint.h>

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static void write_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static bool copy_fixture(uint8_t *destination, size_t destination_len,
                         const uint8_t *source, size_t source_len)
{
    if (destination_len != source_len) return false;
    for (size_t i = 0; i < source_len; i++) destination[i] = source[i];
    return true;
}

static uint32_t crc32_with_zeroed_range(const uint8_t *data, size_t len,
                                        size_t zero_offset, size_t zero_len)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = i >= zero_offset && i < zero_offset + zero_len ? 0 : data[i];
        crc ^= byte;
        for (unsigned int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xEDB88320) : 0);
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint8_t fixture_fold(uint8_t byte)
{
    if (byte >= 'a' && byte <= 'z') return (uint8_t)(byte - ('a' - 'A'));
    if (byte >= 0xE0 && byte <= 0xFE && byte != 0xF7) return (uint8_t)(byte - 0x20);
    return byte;
}

static uint32_t fixture_fnv1a(const uint8_t *name, size_t len)
{
    uint32_t hash = UINT32_C(0x811C9DC5);
    for (size_t i = 0; i < len; i++) {
        hash ^= fixture_fold(name[i]);
        hash *= UINT32_C(0x01000193);
    }
    return hash;
}

static bool fixture_node_valid(const uint8_t *node, size_t block_size,
                               uint32_t key_size, uint32_t value_size,
                               uint32_t block_count)
{
    if (block_size < 32 || read_be32(node) != UINT32_C(0x42544E44) ||
        read_be32(node + 4) != crc32_with_zeroed_range(node, block_size, 4, 4))
        return false;

    uint32_t count = read_be32(node + 16);
    uint16_t level = read_be16(node + 20);
    bool leaf = level == 0;
    uint32_t capacity = leaf ? (uint32_t)((block_size - 28) / (key_size + value_size))
                             : (uint32_t)((block_size - 32) / (key_size + 4));
    if (level >= 32 || count == 0 || count > capacity || read_be16(node + 22) != 0)
        return false;

    uint32_t sibling = read_be32(node + 24);
    if (sibling != 0 && (!leaf || sibling >= block_count)) return false;
    if (!leaf) {
        size_t child_base = 28u + (size_t)capacity * key_size;
        for (uint32_t i = 0; i <= count; i++) {
            uint32_t child = read_be32(node + child_base + (size_t)i * 4);
            if (child == 0 || child >= block_count) return false;
        }
    }
    return true;
}

static const uint8_t fixture_superblock[512] = {
    [0] = 0x42, [1] = 0x46, [2] = 0x53,
    [7] = 0x02,
    [10] = 0x10,
    [15] = 0x40,
    [23] = 0x01,
    [51] = 0x3E,
    [63] = 0x02,
    [64] = 'T', [65] = 'e', [66] = 's', [67] = 't',
    [68] = 'V', [69] = 'o', [70] = 'l',
    [101] = 0x02,
    [236] = 0x10, [237] = 0xEB, [238] = 0x3C, [239] = 0xFC,
};

static const uint8_t fixture_inode[44] = {
    [3] = 0x02,
    [11] = 0x01,
    [12] = 0x23, [13] = 0x45, [14] = 0x67, [15] = 0x89,
    [19] = 0x10,
    [23] = 0x02,
    [27] = 0x0F,
    [28] = 0x01, [29] = 0x23, [30] = 0x04, [31] = 0x56,
    [33] = 0x0A, [35] = 0x14, [37] = 0x1E,
    [39] = 0x28, [41] = 0x32, [43] = 0x3C,
};

static const uint8_t fixture_dir_key[264] = {
    [3] = 0x01,
    [4] = 0x32, [5] = 0x54, [6] = 0x3B, [7] = 0x0B,
    [8] = 0x05,
    [9] = 'H', [10] = 'e', [11] = 'l', [12] = 'l', [13] = 'o',
};

static const uint8_t fixture_dir_value[8] = {
    [3] = 0x02,
};

static const uint8_t fixture_extent[16] = {
    [3] = 0x03,
    [7] = 0x20,
    [11] = 0x02,
    [12] = 0xCA, [13] = 0xFE, [14] = 0xBA, [15] = 0xBE,
};

static const uint8_t fixture_free_space[8] = {
    [3] = 0x20,
    [7] = 0x02,
};

static const uint8_t fixture_refcount[8] = {
    [3] = 0x20,
    [7] = 0x03,
};

static const uint8_t fixture_snapshot[56] = {
    [3] = 0x07,
    [7] = 0x10,
    [11] = 0x20,
    [15] = 0x01,
    [19] = 0x02,
    [24] = 's', [25] = 'n', [26] = 'a', [27] = 'p',
};

static const uint8_t fixture_leaf[1024] = {
    [0] = 0x42, [1] = 0x54, [2] = 0x4E, [3] = 0x44,
    [4] = 0x74, [5] = 0x8B, [6] = 0xE9, [7] = 0x7E,
    [15] = 0x02,
    [19] = 0x01,
    [31] = 0x20,
    [527] = 0x10,
};

static const uint8_t fixture_internal[1024] = {
    [0] = 0x42, [1] = 0x54, [2] = 0x4E, [3] = 0x44,
    [4] = 0xC1, [5] = 0x8D, [6] = 0x60, [7] = 0x88,
    [15] = 0x02,
    [19] = 0x01,
    [21] = 0x01,
    [31] = 0x20,
    [527] = 0x10,
    [531] = 0x30,
};

static void test_superblock_fixture(void)
{
    TEST_ASSERT_EQ(read_be32(fixture_superblock), UINT32_C(0x42465300));
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 4), 2);
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 8), 4096);
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 12), 64);
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 48), 62);
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 60), 2);
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 100), 131072);
    TEST_ASSERT_EQ(crc32_with_zeroed_range(fixture_superblock, 236, 236, 0),
                   UINT32_C(0x10EB3CFC));
    TEST_ASSERT_EQ(read_be32(fixture_superblock + 236), UINT32_C(0x10EB3CFC));
    for (size_t i = 240; i < sizeof(fixture_superblock); i++)
        TEST_ASSERT_EQ(fixture_superblock[i], 0);
}

static void test_record_fixtures(void)
{
    TEST_ASSERT_EQ(read_be32(fixture_inode), 2);
    TEST_ASSERT_EQ(read_be32(fixture_inode + 8), 1);
    TEST_ASSERT_EQ(read_be32(fixture_inode + 12), UINT32_C(0x23456789));
    TEST_ASSERT_EQ(read_be16(fixture_inode + 28), UINT16_C(0x0123));
    TEST_ASSERT_EQ(read_be16(fixture_inode + 42), UINT16_C(0x003C));

    TEST_ASSERT_EQ(read_be32(fixture_dir_key), 1);
    TEST_ASSERT_EQ(fixture_dir_key[8], 5);
    TEST_ASSERT_EQ(fixture_fnv1a(fixture_dir_key + 9, fixture_dir_key[8]),
                   UINT32_C(0x32543B0B));
    TEST_ASSERT_EQ(read_be32(fixture_dir_key + 4), UINT32_C(0x32543B0B));
    TEST_ASSERT_EQ(read_be32(fixture_dir_value), 2);

    TEST_ASSERT_EQ(read_be32(fixture_extent), 3);
    TEST_ASSERT_EQ(read_be32(fixture_extent + 4), 32);
    TEST_ASSERT_EQ(read_be32(fixture_extent + 8), 2);
    TEST_ASSERT_EQ(read_be32(fixture_extent + 12), UINT32_C(0xCAFEBABE));
    TEST_ASSERT_EQ(read_be32(fixture_free_space), 32);
    TEST_ASSERT_EQ(read_be32(fixture_free_space + 4), 2);
    TEST_ASSERT_EQ(read_be32(fixture_refcount), 32);
    TEST_ASSERT_EQ(read_be32(fixture_refcount + 4), 3);

    TEST_ASSERT_EQ(read_be32(fixture_snapshot), 7);
    TEST_ASSERT_EQ(read_be32(fixture_snapshot + 4), 16);
    TEST_ASSERT_EQ(read_be32(fixture_snapshot + 8), 32);
    TEST_ASSERT_EQ(read_be32(fixture_snapshot + 12), 1);
    TEST_ASSERT_EQ(read_be32(fixture_snapshot + 16), 2);
    TEST_ASSERT_EQ(read_be32(fixture_snapshot + 20), 0);
    TEST_ASSERT_MEM_EQ(fixture_snapshot + 24, "snap", 4);
}

static void test_node_fixtures(void)
{
    TEST_ASSERT(fixture_node_valid(fixture_leaf, sizeof(fixture_leaf), 4, 4, 64));
    TEST_ASSERT(fixture_node_valid(fixture_internal, sizeof(fixture_internal), 4, 4, 64));
    TEST_ASSERT_EQ(crc32_with_zeroed_range(fixture_leaf, sizeof(fixture_leaf), 4, 4),
                   UINT32_C(0x748BE97E));
    TEST_ASSERT_EQ(crc32_with_zeroed_range(fixture_internal, sizeof(fixture_internal), 4, 4),
                   UINT32_C(0xC18D6088));
}

static void test_malformed_fixtures(void)
{
    uint8_t superblock[sizeof(fixture_superblock)];
    TEST_ASSERT(copy_fixture(superblock, sizeof(superblock), fixture_superblock,
                             sizeof(fixture_superblock)));
    superblock[100] ^= 1;
    TEST_ASSERT(crc32_with_zeroed_range(superblock, 236, 236, 0) !=
                read_be32(superblock + 236));

    uint8_t node[sizeof(fixture_leaf)];
    TEST_ASSERT(copy_fixture(node, sizeof(node), fixture_leaf, sizeof(fixture_leaf)));
    node[23] = 1;
    write_be32(node + 4, crc32_with_zeroed_range(node, sizeof(node), 4, 4));
    TEST_ASSERT(!fixture_node_valid(node, sizeof(node), 4, 4, 64));

    TEST_ASSERT(copy_fixture(node, sizeof(node), fixture_leaf, sizeof(fixture_leaf)));
    write_be32(node + 16, 0);
    write_be32(node + 4, crc32_with_zeroed_range(node, sizeof(node), 4, 4));
    TEST_ASSERT(!fixture_node_valid(node, sizeof(node), 4, 4, 64));

    TEST_ASSERT(copy_fixture(node, sizeof(node), fixture_leaf, sizeof(fixture_leaf)));
    write_be32(node + 24, 64);
    write_be32(node + 4, crc32_with_zeroed_range(node, sizeof(node), 4, 4));
    TEST_ASSERT(!fixture_node_valid(node, sizeof(node), 4, 4, 64));

    TEST_ASSERT(copy_fixture(node, sizeof(node), fixture_internal, sizeof(fixture_internal)));
    write_be32(node + 524, 0);
    write_be32(node + 4, crc32_with_zeroed_range(node, sizeof(node), 4, 4));
    TEST_ASSERT(!fixture_node_valid(node, sizeof(node), 4, 4, 64));
}

TEST_SUITE_BEGIN("Independent Format Fixtures")
    TEST_RUN(test_superblock_fixture);
    TEST_RUN(test_record_fixtures);
    TEST_RUN(test_node_fixtures);
    TEST_RUN(test_malformed_fixtures);
TEST_SUITE_END()
