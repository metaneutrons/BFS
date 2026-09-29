/*
 * BFS — CRC32 tests with known test vectors
 */

#include "test_harness.h"
#include "bfs_crc32.h"

/* Independent bitwise reference for both the host C and 68k assembly paths. */
static uint32_t reference_crc32(uint32_t initial, const uint8_t *data,
                                size_t length)
{
    uint32_t crc = ~initial;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

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

static void test_crc32_lengths_alignments_and_seeds(void)
{
    uint8_t buf[80];
    const uint32_t seeds[] = {0, 0x12345678u, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)(i * 37u + 11u);

    for (size_t offset = 0; offset < 4; offset++) {
        for (size_t length = 0; length <= 64; length++) {
            for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
                uint32_t expected = reference_crc32(seeds[seed], buf + offset,
                                                    length);
                TEST_ASSERT_EQ(bfs_crc32(seeds[seed], buf + offset, length),
                               expected);
            }
        }
    }
    TEST_ASSERT(bfs_crc32(0, buf, 64) != bfs_crc32(0, buf + 1, 64));
}

static void test_crc32_full_node(void)
{
    static uint8_t buf[4096 + 3];
    for (size_t i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)(i * 73u + (i >> 4));
    for (size_t offset = 0; offset < 4; offset++) {
        uint32_t expected = reference_crc32(0xA5C39E12u, buf + offset, 4096);
        TEST_ASSERT_EQ(bfs_crc32(0xA5C39E12u, buf + offset, 4096), expected);
    }
}

TEST_SUITE_BEGIN("CRC32")
    TEST_RUN(test_crc32_empty);
    TEST_RUN(test_crc32_check_value);
    TEST_RUN(test_crc32_single_byte);
    TEST_RUN(test_crc32_chaining);
    TEST_RUN(test_crc32_all_ff);
    TEST_RUN(test_crc32_lengths_alignments_and_seeds);
    TEST_RUN(test_crc32_full_node);
TEST_SUITE_END()
