/* SPDX-License-Identifier: MPL-2.0 */

#include "test_harness.h"
#include "bfs_types.h"

static void test_be16_layout(void)
{
    uint8_t bytes[2];
    const uint8_t expected[] = {0x12, 0x34};
    bfs_store_be16(bytes, 0x1234);
    TEST_ASSERT_MEM_EQ(bytes, expected, sizeof(bytes));
    TEST_ASSERT_EQ(bfs_load_be16(bytes), 0x1234);
}

static void test_be32_layout(void)
{
    uint8_t bytes[4];
    const uint8_t expected[] = {0x12, 0x34, 0x56, 0x78};
    bfs_store_be32(bytes, 0x12345678);
    TEST_ASSERT_MEM_EQ(bytes, expected, sizeof(bytes));
    TEST_ASSERT_EQ(bfs_load_be32(bytes), 0x12345678);
}

static void test_be64_layout(void)
{
    uint8_t bytes[8];
    const uint8_t expected[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    bfs_store_be64(bytes, UINT64_C(0x0123456789ABCDEF));
    TEST_ASSERT_MEM_EQ(bytes, expected, sizeof(bytes));
    TEST_ASSERT(bfs_load_be64(bytes) == UINT64_C(0x0123456789ABCDEF));
}

static void test_unaligned_access(void)
{
    uint8_t storage[9] = {0};
    const uint8_t expected[] = {0xDE, 0xAD, 0xBE, 0xEF};
    bfs_store_be32(storage + 1, 0xDEADBEEF);
    TEST_ASSERT_MEM_EQ(storage + 1, expected, sizeof(expected));
    TEST_ASSERT_EQ(bfs_load_be32(storage + 1), 0xDEADBEEF);
}

TEST_SUITE_BEGIN("Big-Endian Encoding")
    TEST_RUN(test_be16_layout);
    TEST_RUN(test_be32_layout);
    TEST_RUN(test_be64_layout);
    TEST_RUN(test_unaligned_access);
TEST_SUITE_END()
