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

static void test_crc32_zero_append_oracle(void)
{
    static const uint32_t seeds[] = {
        0u, 1u, 0xFFFFFFFFu, 0x12345678u,
    };
    static uint8_t zeros[65536];
    uint32_t random = 0xA5C39E12u;

    for (size_t length = 0; length <= 128; length++) {
        for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
            TEST_ASSERT_EQ(bfs_crc32_zeros(seeds[seed], (uint32_t)length),
                           reference_crc32(seeds[seed], zeros, length));
        }
    }

    for (uint32_t power = 1; power <= 65536; power <<= 1) {
        const uint32_t lengths[] = {
            power - 1,
            power,
            power < 65536 ? power + 1 : power,
        };
        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
                uint32_t length = lengths[i];
                TEST_ASSERT_EQ(bfs_crc32_zeros(seeds[seed], length),
                               reference_crc32(seeds[seed], zeros, length));
            }
        }
    }

    for (unsigned sample = 0; sample < 200; sample++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        uint32_t length = random % 65537u;
        uint32_t seed = seeds[sample % (sizeof(seeds) / sizeof(seeds[0]))];
        TEST_ASSERT_EQ(bfs_crc32_zeros(seed, length),
                       reference_crc32(seed, zeros, length));
    }
}

static void test_crc32_zero_append_chaining(void)
{
    static const uint8_t prefix[] = {
        0x42, 0x46, 0x53, 0x20, 0x70, 0x72, 0x65, 0x66, 0x69, 0x78, 0x21,
    };
    static const uint8_t suffix[] = {
        0x73, 0x75, 0x66, 0x66, 0x69, 0x78, 0x20, 0x42, 0x46, 0x53, 0x0A,
    };
    static const uint32_t zero_lengths[] = {0u, 1u, 31u, 256u, 4097u, 65536u};
    static uint8_t message[sizeof(prefix) + 65536 + sizeof(suffix)];
    const uint32_t seed = 0x12345678u;

    TEST_ASSERT_EQ(bfs_crc32_zeros(seed, 0), seed);
    for (size_t i = 0; i < sizeof(zero_lengths) / sizeof(zero_lengths[0]); i++) {
        uint32_t zero_length = zero_lengths[i];
        memset(message, 0, sizeof(message));
        memcpy(message, prefix, sizeof(prefix));
        memcpy(message + sizeof(prefix) + zero_length, suffix, sizeof(suffix));
        uint32_t actual = bfs_crc32(seed, message, sizeof(prefix));
        actual = bfs_crc32_zeros(actual, zero_length);
        actual = bfs_crc32(actual, message + sizeof(prefix) + zero_length,
                           sizeof(suffix));
        TEST_ASSERT_EQ(actual,
                       reference_crc32(seed, message,
                                       sizeof(prefix) + zero_length +
                                           sizeof(suffix)));
    }
}

static void test_crc32_zero_append_large_lengths(void)
{
    static const uint32_t seeds[] = {
        0u, 1u, 0xFFFFFFFFu, 0x12345678u,
    };
    static const struct {
        uint32_t first;
        uint32_t second;
    } splits[] = {
        {0u, 0xFFFFFFFFu},
        {0xFFFFFFFFu, 0u},
        {0x40000000u, 0x40000000u},
        {0x80000000u, 0x7FFFFFFFu},
        {0x12345678u, 0x65432101u},
    };

    for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
        for (size_t i = 0; i < sizeof(splits) / sizeof(splits[0]); i++) {
            uint64_t total = (uint64_t)splits[i].first + splits[i].second;
            uint32_t chained = bfs_crc32_zeros(seeds[seed], splits[i].first);
            chained = bfs_crc32_zeros(chained, splits[i].second);
            TEST_ASSERT(total <= UINT32_MAX);
            TEST_ASSERT_EQ(bfs_crc32_zeros(seeds[seed], (uint32_t)total), chained);
        }
    }

    /* Two partitions of the same 64-bit virtual zero run (2^32 + 1 bytes). */
    for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
        uint64_t first_total = (uint64_t)0xFFFFFFFFu + 2u;
        uint64_t second_total = (uint64_t)0x80000000u +
                                0x80000000u + 1u;
        uint32_t first = bfs_crc32_zeros(seeds[seed], 0xFFFFFFFFu);
        first = bfs_crc32_zeros(first, 2u);
        uint32_t second = bfs_crc32_zeros(seeds[seed], 0x80000000u);
        second = bfs_crc32_zeros(second, 0x80000000u);
        second = bfs_crc32_zeros(second, 1u);
        TEST_ASSERT_EQ(first_total, second_total);
        TEST_ASSERT_EQ(first, second);
    }
}

static uint32_t crc32_test_matrix_times(const uint32_t matrix[32],
                                        uint32_t vector)
{
    uint32_t result = 0;
    unsigned column = 0;

    while (vector != 0) {
        if (vector & 1u)
            result ^= matrix[column];
        vector >>= 1;
        column++;
    }
    return result;
}

static void crc32_test_matrix_square(const uint32_t matrix[32],
                                    uint32_t squared[32])
{
    for (unsigned column = 0; column < 32; column++)
        squared[column] = crc32_test_matrix_times(matrix, matrix[column]);
}

static void crc32_test_build_zero_powers(uint32_t powers[32][32])
{
    uint32_t zero_bit[32];
    uint32_t scratch[32];

    for (unsigned bit = 0; bit < 32; bit++) {
        uint32_t basis = (uint32_t)1u << bit;
        zero_bit[bit] = (basis >> 1) ^
                        ((basis & 1u) ? 0xEDB88320u : 0u);
    }

    /* Three matrix squarings turn the one-zero-bit map into one zero byte. */
    for (unsigned square = 0; square < 3; square++) {
        crc32_test_matrix_square(zero_bit, scratch);
        memcpy(zero_bit, scratch, sizeof(zero_bit));
    }
    memcpy(powers[0], zero_bit, sizeof(zero_bit));

    for (unsigned power = 1; power < 32; power++)
        crc32_test_matrix_square(powers[power - 1], powers[power]);
}

static uint32_t crc32_test_zero_oracle(uint32_t initial, uint32_t length,
                                       const uint32_t powers[32][32])
{
    uint32_t crc = ~initial;

    for (unsigned power = 0; length != 0; power++, length >>= 1) {
        if (length & 1u)
            crc = crc32_test_matrix_times(powers[power], crc);
    }
    return ~crc;
}

static void test_crc32_zero_append_independent_high_powers(void)
{
    static const uint32_t common_seeds[] = {
        0u, 1u, 0xFFFFFFFFu, 0x12345678u,
    };
    static const uint32_t high_boundaries[] = {
        0x0FFFFFFFu, 0x10000000u, 0x10000001u,
        0x1FFFFFFFu, 0x20000000u, 0x20000001u,
        0x3FFFFFFFu, 0x40000000u, 0x40000001u,
        0x7FFFFFFFu, 0x80000000u, 0x80000001u,
        UINT32_MAX,
    };
    uint32_t powers[32][32];
    uint32_t random = 0xC13FA9A9u;

    crc32_test_build_zero_powers(powers);

    /* Check every byte-length power against all 32 independent basis states. */
    for (unsigned state_bit = 0; state_bit < 32; state_bit++) {
        uint32_t seed = ~((uint32_t)1u << state_bit);
        for (unsigned length_bit = 0; length_bit < 32; length_bit++) {
            uint32_t length = (uint32_t)1u << length_bit;
            TEST_ASSERT_EQ(bfs_crc32_zeros(seed, length),
                           crc32_test_zero_oracle(seed, length, powers));
        }
    }

    for (size_t i = 0;
         i < sizeof(high_boundaries) / sizeof(high_boundaries[0]); i++) {
        uint32_t length = high_boundaries[i];
        for (unsigned state_bit = 0; state_bit < 32; state_bit++) {
            uint32_t seed = ~((uint32_t)1u << state_bit);
            TEST_ASSERT_EQ(bfs_crc32_zeros(seed, length),
                           crc32_test_zero_oracle(seed, length, powers));
        }
        for (size_t seed = 0;
             seed < sizeof(common_seeds) / sizeof(common_seeds[0]); seed++) {
            TEST_ASSERT_EQ(bfs_crc32_zeros(common_seeds[seed], length),
                           crc32_test_zero_oracle(common_seeds[seed], length,
                                                  powers));
        }
    }

    /* Check random large lengths and seeds against the runtime-built oracle. */
    for (unsigned sample = 0; sample < 128; sample++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        uint32_t length = random | 0x10000000u;
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        uint32_t seed = random;
        TEST_ASSERT_EQ(bfs_crc32_zeros(seed, length),
                       crc32_test_zero_oracle(seed, length, powers));
    }

    /* Exercise every adjacent byte-power doubling, including p28 -> p29. */
    for (unsigned state_bit = 0; state_bit < 32; state_bit++) {
        uint32_t seed = ~((uint32_t)1u << state_bit);
        for (unsigned power = 0; power < 31; power++) {
            uint32_t length = (uint32_t)1u << power;
            uint32_t once = bfs_crc32_zeros(seed, length);
            uint32_t twice = bfs_crc32_zeros(once, length);
            uint32_t doubled = bfs_crc32_zeros(seed, length << 1);
            TEST_ASSERT_EQ(twice, doubled);
        }
    }
}

static void test_crc32_zero_append_all_nibble_states(void)
{
    uint32_t powers[32][32];
    crc32_test_build_zero_powers(powers);

    /* Each lookup table entry is observed independently through the public
     * API: complement the initial CRC to set one nibble of the linear state.
     * The oracle derives its matrices from the polynomial at runtime. */
    for (unsigned power = 0; power < 32; power++) {
        uint32_t length = (uint32_t)1u << power;
        for (unsigned nibble = 0; nibble < 8; nibble++) {
            for (uint32_t value = 0; value < 16; value++) {
                uint32_t initial = ~(value << (4u * nibble));
                TEST_ASSERT_EQ(bfs_crc32_zeros(initial, length),
                               crc32_test_zero_oracle(initial, length, powers));
            }
        }
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
    TEST_RUN(test_crc32_zero_append_oracle);
    TEST_RUN(test_crc32_zero_append_chaining);
    TEST_RUN(test_crc32_zero_append_large_lengths);
    TEST_RUN(test_crc32_zero_append_independent_high_powers);
    TEST_RUN(test_crc32_zero_append_all_nibble_states);
TEST_SUITE_END()
