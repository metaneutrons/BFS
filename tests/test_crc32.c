/*
 * BFS — CRC32 tests with known test vectors
 */

#include "test_harness.h"
#include "bfs_crc32.h"

static void test_crc32_empty(void)
{
    TEST_ASSERT_EQ(bfs_crc32(0, "", 0), 0x00000000);
}

static void test_crc32_check_value(void)
{
    /* The "check value" for CRC-32/ISO-HDLC: CRC32("123456789") = 0xCBF43926 */
    const char *data = "123456789";
    TEST_ASSERT_EQ(bfs_crc32(0, data, 9), 0xCBF43926);
}

static void test_crc32_single_byte(void)
{
    uint8_t b = 0x00;
    TEST_ASSERT_EQ(bfs_crc32(0, &b, 1), 0xD202EF8D);
}

static void test_crc32_chaining(void)
{
    /* CRC of "123456789" computed in two parts should equal single-pass */
    uint32_t crc = bfs_crc32(0, "12345", 5);
    crc = bfs_crc32(crc, "6789", 4);
    TEST_ASSERT_EQ(crc, 0xCBF43926);
}

static void test_crc32_all_ff(void)
{
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQ(bfs_crc32(0, buf, 4), 0xFFFFFFFF);
}

TEST_SUITE_BEGIN("CRC32")
    TEST_RUN(test_crc32_empty);
    TEST_RUN(test_crc32_check_value);
    TEST_RUN(test_crc32_single_byte);
    TEST_RUN(test_crc32_chaining);
    TEST_RUN(test_crc32_all_ff);
TEST_SUITE_END()
