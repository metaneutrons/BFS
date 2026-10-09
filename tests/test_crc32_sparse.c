/* SPDX-License-Identifier: MPL-2.0 */
/* Differential tests for exact CRC32 calculation over sparse byte ranges. */

#include "test_harness.h"
#include "bfs_crc32.h"

/* Bitwise CRC oracle kept independent of the table and sparse implementations. */
static uint32_t reference_crc32(uint32_t initial, const uint8_t *data,
                                uint32_t length)
{
    uint32_t crc = ~initial;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static bool assert_sparse_matches_reference(uint32_t initial,
                                            const uint8_t *data,
                                            uint32_t length)
{
    uint32_t expected = reference_crc32(initial, data, length);
    uint32_t actual = bfs_crc32_sparse(initial, data, length);
    if (actual == expected) return true;
    fprintf(stderr,
            "  sparse CRC mismatch: seed=%08x length=%u got=%08x expected=%08x\n",
            initial, length, actual, expected);
    return false;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void test_sparse_crc_empty_null_and_short_inputs(void)
{
    static const uint32_t seeds[] = {0u, 1u, 0xFFFFFFFFu, 0xA5C39E12u};
    uint8_t bytes[96];
    for (uint32_t i = 0; i < sizeof(bytes); i++)
        bytes[i] = (uint8_t)(i * 37u + 11u);

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        TEST_ASSERT_EQ(bfs_crc32_sparse(seeds[i], NULL, 0), seeds[i]);
        TEST_ASSERT(assert_sparse_matches_reference(seeds[i], NULL, 0));
        for (uint32_t offset = 0; offset < 16; offset++) {
            for (uint32_t length = 0; length < 64; length++) {
                TEST_ASSERT(assert_sparse_matches_reference(
                    seeds[i], bytes + offset, length));
            }
        }
    }
}

static void test_sparse_crc_chunk_and_run_boundaries(void)
{
    static const uint32_t seeds[] = {
        0u, 0x12345678u, 0xFFFFFFFFu, 0xA5C39E12u,
    };
    static const uint32_t lengths[] = {
        0u, 1u, 15u, 16u, 17u, 31u, 32u, 47u, 48u, 63u, 64u, 65u,
        79u, 80u, 81u, 95u, 96u, 127u, 128u, 129u, 191u, 192u, 193u,
        255u, 256u, 257u, 511u, 512u, 513u, 1023u, 1024u, 1025u,
    };
    uint8_t bytes[1060];

    for (uint32_t i = 0; i < sizeof(bytes); i++)
        bytes[i] = (uint8_t)(1u + ((i * 73u + (i >> 3)) % 255u));

    for (uint32_t offset = 0; offset < 16; offset++) {
        for (size_t length_index = 0;
             length_index < sizeof(lengths) / sizeof(lengths[0]);
             length_index++) {
            uint32_t length = lengths[length_index];
            for (size_t seed_index = 0;
                 seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
                TEST_ASSERT(assert_sparse_matches_reference(
                    seeds[seed_index], bytes + offset, length));
            }
        }
    }
}

static void test_sparse_crc_all_zero_ranges(void)
{
    static const uint32_t seeds[] = {
        0u, 1u, 0xFFFFFFFFu, 0x12345678u, 0xA5C39E12u,
    };
    static const uint32_t lengths[] = {
        0u, 1u, 15u, 16u, 17u, 63u, 64u, 65u, 127u, 128u, 129u,
        255u, 256u, 257u, 1023u, 1024u, 4095u, 4096u, 8191u, 8192u,
    };
    static const uint8_t zeros[8192] = {0};

    for (size_t seed_index = 0;
         seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
        for (size_t length_index = 0;
             length_index < sizeof(lengths) / sizeof(lengths[0]);
             length_index++) {
            TEST_ASSERT(assert_sparse_matches_reference(
                seeds[seed_index], zeros, lengths[length_index]));
        }
        for (uint32_t offset = 0; offset < 16; offset++) {
            TEST_ASSERT(assert_sparse_matches_reference(
                seeds[seed_index], zeros + offset, 8192u - offset));
        }
    }
}

static void test_sparse_crc_nonzero_at_every_lane_and_boundary(void)
{
    static const uint32_t seeds[] = {0u, 0x13579BDFu, 0xFFFFFFFFu};
    static const uint32_t lengths[] = {
        63u, 64u, 65u, 79u, 80u, 81u, 127u, 128u, 129u,
        255u, 256u, 257u, 511u, 512u,
    };
    uint8_t bytes[512];

    for (size_t length_index = 0;
         length_index < sizeof(lengths) / sizeof(lengths[0]); length_index++) {
        uint32_t length = lengths[length_index];
        for (uint32_t position = 0; position < length; position++) {
            memset(bytes, 0, sizeof(bytes));
            bytes[position] = (uint8_t)(1u + (position % 255u));
            for (size_t seed_index = 0;
                 seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
                TEST_ASSERT(assert_sparse_matches_reference(
                    seeds[seed_index], bytes, length));
            }
        }
    }
}

static void test_sparse_crc_mixed_runs_including_short_fragments(void)
{
    static const uint32_t seeds[] = {0u, 0x2468ACE0u, 0xFFFFFFFFu};
    static const uint32_t zero_lengths[] = {
        1u, 2u, 15u, 16u, 17u, 31u, 32u, 63u, 64u, 65u, 127u, 128u, 129u,
    };
    uint8_t bytes[2048];

    for (uint32_t lane = 0; lane < 16; lane++) {
        for (uint32_t i = 0; i < sizeof(bytes); i++)
            bytes[i] = (uint8_t)(1u + ((i * 29u + lane * 11u) % 255u));

        uint32_t cursor = 19u + lane;
        for (size_t run = 0;
             run < sizeof(zero_lengths) / sizeof(zero_lengths[0]); run++) {
            uint32_t run_length = zero_lengths[run];
            if (cursor + run_length >= sizeof(bytes)) break;
            memset(bytes + cursor, 0, run_length);
            cursor += run_length + 3u + (uint32_t)(run % 13u);
        }

        for (size_t seed_index = 0;
             seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
            TEST_ASSERT(assert_sparse_matches_reference(seeds[seed_index],
                                                        bytes, sizeof(bytes)));
        }
    }
}

static void test_sparse_crc_chaining_across_zero_runs(void)
{
    static const uint32_t seeds[] = {0u, 1u, 0xA5C39E12u};
    uint8_t bytes[768];
    for (uint32_t i = 0; i < sizeof(bytes); i++)
        bytes[i] = (uint8_t)(1u + ((i * 47u + 9u) % 255u));
    memset(bytes + 91, 0, 257);
    memset(bytes + 493, 0, 127);

    static const uint32_t split_sizes[] = {
        1u, 15u, 16u, 17u, 63u, 64u, 65u, 128u, 257u,
    };
    for (size_t seed_index = 0;
         seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
        uint32_t whole = bfs_crc32_sparse(seeds[seed_index], bytes,
                                          sizeof(bytes));
        TEST_ASSERT_EQ(whole, reference_crc32(seeds[seed_index], bytes,
                                               sizeof(bytes)));
        for (size_t split_index = 0;
             split_index < sizeof(split_sizes) / sizeof(split_sizes[0]);
             split_index++) {
            uint32_t split = split_sizes[split_index];
            uint32_t chained = seeds[seed_index];
            for (uint32_t offset = 0; offset < sizeof(bytes);) {
                uint32_t remaining = (uint32_t)sizeof(bytes) - offset;
                uint32_t part = remaining < split ? remaining : split;
                chained = bfs_crc32_sparse(chained, bytes + offset, part);
                if (split_index == 0 && offset == 0)
                    chained = bfs_crc32_sparse(chained, NULL, 0);
                offset += part;
            }
            TEST_ASSERT_EQ(chained, whole);
        }
    }
}

static void test_sparse_crc_deterministic_random_inputs(void)
{
    static const uint32_t seeds[] = {
        0u, 0xFFFFFFFFu, 0x12345678u, 0x87654321u,
    };
    uint8_t storage[4112];
    uint32_t random = 0xC13FA9A9u;

    for (unsigned sample = 0; sample < 160; sample++) {
        uint32_t offset = next_random(&random) % 16u;
        uint32_t length = next_random(&random) % 4097u;
        for (uint32_t i = 0; i < sizeof(storage); i++) {
            uint32_t value = next_random(&random);
            storage[i] = (value & 7u) == 0 ? 0 : (uint8_t)(value >> 24);
        }

        /* Include deterministic long runs and runs ending at awkward tails. */
        if (length >= 64u) {
            uint32_t run_start = offset + next_random(&random) % (length - 63u);
            uint32_t run_length = 64u + next_random(&random) % 193u;
            uint32_t end = offset + length;
            if (run_length > end - run_start) run_length = end - run_start;
            memset(storage + run_start, 0, run_length);
        }

        for (size_t seed_index = 0;
             seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
            TEST_ASSERT(assert_sparse_matches_reference(
                seeds[seed_index], storage + offset, length));
        }
    }
}

TEST_SUITE_BEGIN("Sparse CRC32")
    TEST_RUN(test_sparse_crc_empty_null_and_short_inputs);
    TEST_RUN(test_sparse_crc_chunk_and_run_boundaries);
    TEST_RUN(test_sparse_crc_all_zero_ranges);
    TEST_RUN(test_sparse_crc_nonzero_at_every_lane_and_boundary);
    TEST_RUN(test_sparse_crc_mixed_runs_including_short_fragments);
    TEST_RUN(test_sparse_crc_chaining_across_zero_runs);
    TEST_RUN(test_sparse_crc_deterministic_random_inputs);
TEST_SUITE_END()
