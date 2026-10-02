/* SPDX-License-Identifier: MPL-2.0 */
/*
 * AmigaOS runtime qualification for the retained and candidate 68020 CRC32
 * kernels. The runner links the candidate as bfs_crc32 and renames the exact
 * retained implementation's entry point to bfs_crc32_baseline.
 */

#include <exec/io.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*CrcFunction)(uint32_t, const void *, size_t);

uint32_t bfs_crc32(uint32_t initial, const void *data, size_t length);
uint32_t bfs_crc32_baseline(uint32_t initial, const void *data, size_t length);
uint32_t bfs_crc32_abi_clobber_d2(uint32_t initial, const void *data,
                                  size_t length);
uint32_t bfs_crc32_abi_clobber_a2(uint32_t initial, const void *data,
                                  size_t length);

/* Return mask bits are documented beside the assembly witness. */
extern ULONG bfs_crc32_abi_probe(CrcFunction function, uint32_t initial,
                                 const void *data, size_t length,
                                 uint32_t *result);

struct Device *TimerBase;

static CrcFunction volatile call_candidate = bfs_crc32;
static CrcFunction volatile call_baseline = bfs_crc32_baseline;
static volatile uint32_t timing_sink;

#define SMALL_MAX_LENGTH 132UL
#define SMALL_PREFIX_BYTES 8UL
#define SMALL_STORAGE_BYTES 160UL
#define LARGE_MAX_LENGTH 65539UL
#define LARGE_PREFIX_BYTES 8UL
#define LARGE_STORAGE_BYTES (LARGE_MAX_LENGTH + LARGE_PREFIX_BYTES + 8UL)
#define TIMING_SAMPLE_COUNT 6UL
#define TIMING_LENGTH_COUNT 8UL
#define REPORT_CAPACITY 8192UL

static const uint32_t seeds[] = {
    0x00000000u, 0xFFFFFFFFu, 0x12345678u, 0xA5C39E12u
};

static const ULONG large_lengths[] = {
    4095UL, 4096UL, 4097UL,
    65533UL, 65534UL, 65535UL, 65536UL, 65537UL, 65538UL, 65539UL
};

static const ULONG timing_lengths[TIMING_LENGTH_COUNT] = {
    0UL, 1UL, 3UL, 4UL, 44UL, 256UL, 4096UL, 65536UL
};

static const ULONG timing_repeats[TIMING_LENGTH_COUNT] = {
    50000UL, 20000UL, 20000UL, 15000UL,
    3000UL, 512UL, 64UL, 4UL
};

enum {
    PATTERN_ZERO = 0,
    PATTERN_FF = 1,
    PATTERN_A5 = 2,
    PATTERN_MIXED = 3,
    PATTERN_COUNT = 4
};

typedef struct {
    ULONG small_vector_cases;
    ULONG large_vector_cases;
    ULONG small_chain_cases;
    ULONG large_chain_cases;
    ULONG null_zero_cases;
    ULONG known_vector_cases;
    ULONG abi_negative_control_cases;
    ULONG abi_negative_control_passes;
    ULONG abi_negative_control_errors;
    ULONG crc_calls[2];
    ULONG abi_failures[2];
    ULONG crc_failures[2];
    ULONG timing_calls[2];
    ULONG timing_samples;
    ULONG timing_order_baseline_first;
    ULONG timing_order_candidate_first;
    ULONG failures;
    ULONG clock_hz;
    uint32_t timing_checksum;
    const char *first_mode;
    const char *first_check;
    ULONG first_length;
    ULONG first_offset;
    uint32_t first_seed;
    uint32_t first_expected;
    uint32_t first_actual;
    ULONG first_abi_mask;
} ProbeCounts;

static ProbeCounts counts;
static UBYTE small_storage[SMALL_STORAGE_BYTES];
static UBYTE large_storage[LARGE_STORAGE_BYTES];

static uint32_t reference_crc32(uint32_t initial, const UBYTE *data,
                                size_t length)
{
    uint32_t crc = ~initial;
    size_t index;

    for (index = 0; index < length; index++) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^
                  ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static UBYTE pattern_byte(ULONG pattern, ULONG index)
{
    switch (pattern) {
    case PATTERN_ZERO:
        return 0x00;
    case PATTERN_FF:
        return 0xFF;
    case PATTERN_A5:
        return 0xA5;
    default:
        return (UBYTE)((index * 73UL + (index >> 4) * 19UL + 0x5BUL) &
                       0xFFUL);
    }
}

static void fill_pattern(UBYTE *buffer, ULONG length, ULONG pattern)
{
    ULONG index;
    for (index = 0; index < length; index++)
        buffer[index] = pattern_byte(pattern, index);
}

static void record_failure(const char *mode, const char *check,
                           ULONG length, ULONG offset, uint32_t seed,
                           uint32_t expected, uint32_t actual,
                           ULONG abi_mask)
{
    counts.failures++;
    if (mode && mode[0] == 'b' && mode[1] == 'a') {
        if (abi_mask) counts.abi_failures[1]++;
        else counts.crc_failures[1]++;
    } else if (mode && mode[0] == 'c') {
        if (abi_mask) counts.abi_failures[0]++;
        else counts.crc_failures[0]++;
    }
    if (counts.failures != 1) return;

    counts.first_mode = mode;
    counts.first_check = check;
    counts.first_length = length;
    counts.first_offset = offset;
    counts.first_seed = seed;
    counts.first_expected = expected;
    counts.first_actual = actual;
    counts.first_abi_mask = abi_mask;
}

static uint32_t call_checked(CrcFunction function, ULONG which,
                             const char *mode, uint32_t seed,
                             const UBYTE *data, ULONG length, ULONG offset,
                             uint32_t expected, BOOL has_known_value,
                             uint32_t known_value)
{
    uint32_t actual = 0;
    ULONG abi_mask;

    counts.crc_calls[which]++;
    abi_mask = bfs_crc32_abi_probe(function, seed, data, (size_t)length,
                                   &actual);
    if (abi_mask != 0) {
        record_failure(mode, "abi", length, offset, seed, 0, 0, abi_mask);
    }
    if (actual != expected) {
        record_failure(mode, "oracle", length, offset, seed, expected,
                       actual, 0);
    }
    if (has_known_value && actual != known_value) {
        record_failure(mode, "known-vector", length, offset, seed,
                       known_value, actual, 0);
    }
    return actual;
}

static void check_both(const char *operation, const UBYTE *data,
                       ULONG length, ULONG offset, uint32_t seed,
                       BOOL has_known_value, uint32_t known_value)
{
    CrcFunction baseline = call_baseline;
    CrcFunction candidate = call_candidate;
    uint32_t expected = reference_crc32(seed, data, (size_t)length);

    (void)operation;
    (void)call_checked(baseline, 1, "baseline", seed, data, length, offset,
                       expected, has_known_value, known_value);
    (void)call_checked(candidate, 0, "candidate", seed, data, length, offset,
                       expected, has_known_value, known_value);
}

static void check_small_vectors(void)
{
    ULONG pattern, offset, length, seed_index;

    for (pattern = 0; pattern < PATTERN_COUNT; pattern++) {
        fill_pattern(small_storage, SMALL_STORAGE_BYTES, pattern);
        for (offset = 0; offset < 8; offset++) {
            const UBYTE *data = &small_storage[SMALL_PREFIX_BYTES + offset];
            for (length = 0; length <= SMALL_MAX_LENGTH; length++) {
                for (seed_index = 0;
                     seed_index < sizeof(seeds) / sizeof(seeds[0]);
                     seed_index++) {
                    check_both("small", data, length, offset,
                               seeds[seed_index], FALSE, 0);
                    counts.small_vector_cases++;
                }
            }
        }
    }
}

static void check_null_zero_vectors(void)
{
    ULONG seed_index;
    for (seed_index = 0;
         seed_index < sizeof(seeds) / sizeof(seeds[0]);
         seed_index++) {
        check_both("null-zero", NULL, 0, 0, seeds[seed_index], FALSE, 0);
        counts.null_zero_cases++;
    }
}

static void check_known_vector(void)
{
    static const UBYTE text[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    check_both("known", text, 9, 0, 0, TRUE, 0xCBF43926u);
    counts.known_vector_cases++;
}

static void check_abi_negative_controls(void)
{
    static CrcFunction controls[] = {
        bfs_crc32_abi_clobber_d2,
        bfs_crc32_abi_clobber_a2
    };
    static const ULONG expected_masks[] = {0x00000004UL, 0x00000100UL};
    static const char *names[] = {"clobber-d2", "clobber-a2"};
    ULONG index;
    uint32_t seed = 0x12345678u;

    for (index = 0; index < 2; index++) {
        uint32_t actual = 0;
        ULONG mask = bfs_crc32_abi_probe(controls[index], seed, NULL, 0,
                                         &actual);
        BOOL passed = (mask == expected_masks[index] && actual == seed);

        counts.abi_negative_control_cases++;
        if (passed) {
            counts.abi_negative_control_passes++;
        } else {
            counts.abi_negative_control_errors++;
            if (mask != expected_masks[index])
                record_failure("abi-witness", names[index], 0, 0, seed,
                               expected_masks[index], mask, mask);
            if (actual != seed)
                record_failure("abi-witness", "negative-control-result",
                               0, 0, seed, seed, actual, mask);
        }
    }
}

static void check_large_vectors(void)
{
    ULONG length_index, offset, seed_index;

    for (length_index = 0;
         length_index < sizeof(large_lengths) / sizeof(large_lengths[0]);
         length_index++) {
        ULONG length = large_lengths[length_index];
        fill_pattern(large_storage, LARGE_STORAGE_BYTES, PATTERN_MIXED);

        /* Every length gets every pointer offset and all four seeds. */
        for (offset = 0; offset < 8; offset++) {
            const UBYTE *data = &large_storage[LARGE_PREFIX_BYTES + offset];
            for (seed_index = 0;
                 seed_index < sizeof(seeds) / sizeof(seeds[0]);
                 seed_index++) {
                check_both("large", data, length, offset, seeds[seed_index],
                           FALSE, 0);
                counts.large_vector_cases++;
            }
        }
    }
}

static void check_small_chaining(void)
{
    ULONG pattern, offset, seed_index, split;

    for (pattern = 0; pattern < PATTERN_COUNT; pattern++) {
        fill_pattern(small_storage, SMALL_STORAGE_BYTES, pattern);
        for (offset = 0; offset < 8; offset++) {
            const UBYTE *data = &small_storage[SMALL_PREFIX_BYTES + offset];
            for (seed_index = 0;
                 seed_index < sizeof(seeds) / sizeof(seeds[0]);
                 seed_index++) {
                uint32_t expected = reference_crc32(
                    seeds[seed_index], data, SMALL_MAX_LENGTH);
                for (split = 0; split <= SMALL_MAX_LENGTH; split++) {
                    CrcFunction baseline = call_baseline;
                    CrcFunction candidate = call_candidate;
                    uint32_t expected_prefix = reference_crc32(
                        seeds[seed_index], data, split);
                    ULONG abi_mask;
                    uint32_t first, final;

                    counts.small_chain_cases++;

                    counts.crc_calls[1] += 2;
                    first = 0;
                    abi_mask = bfs_crc32_abi_probe(
                        baseline, seeds[seed_index], data, (size_t)split,
                        &first);
                    if (abi_mask)
                        record_failure("baseline", "chain-prefix-abi",
                            split, offset, seeds[seed_index], 0, 0, abi_mask);
                    if (first != expected_prefix)
                        record_failure("baseline", "chain-prefix",
                            split, offset, seeds[seed_index],
                            expected_prefix, first, 0);
                    final = 0;
                    abi_mask = bfs_crc32_abi_probe(
                        baseline, first, data + split,
                        (size_t)(SMALL_MAX_LENGTH - split), &final);
                    if (abi_mask)
                        record_failure("baseline", "chain-suffix-abi",
                            SMALL_MAX_LENGTH - split, offset + split,
                            first, 0, 0, abi_mask);
                    if (final != expected)
                        record_failure("baseline", "chain-final",
                            SMALL_MAX_LENGTH, offset, seeds[seed_index],
                            expected, final, 0);

                    counts.crc_calls[0] += 2;
                    first = 0;
                    abi_mask = bfs_crc32_abi_probe(
                        candidate, seeds[seed_index], data, (size_t)split,
                        &first);
                    if (abi_mask)
                        record_failure("candidate", "chain-prefix-abi",
                            split, offset, seeds[seed_index], 0, 0, abi_mask);
                    if (first != expected_prefix)
                        record_failure("candidate", "chain-prefix",
                            split, offset, seeds[seed_index],
                            expected_prefix, first, 0);
                    final = 0;
                    abi_mask = bfs_crc32_abi_probe(
                        candidate, first, data + split,
                        (size_t)(SMALL_MAX_LENGTH - split), &final);
                    if (abi_mask)
                        record_failure("candidate", "chain-suffix-abi",
                            SMALL_MAX_LENGTH - split, offset + split,
                            first, 0, 0, abi_mask);
                    if (final != expected)
                        record_failure("candidate", "chain-final",
                            SMALL_MAX_LENGTH, offset, seeds[seed_index],
                            expected, final, 0);
                }
            }
        }
    }
}

static void check_large_chaining(void)
{
    static const ULONG lengths[] = {4096UL, 65536UL, 65539UL};
    static const ULONG split_offsets[] = {
        0UL, 1UL, 3UL, 43UL, 4095UL, 32767UL, 65535UL,
        65538UL, 65539UL
    };
    ULONG length_index, seed_index, split_index;
    const ULONG data_offset = 3UL;

    fill_pattern(large_storage, LARGE_STORAGE_BYTES, PATTERN_MIXED);
    for (length_index = 0;
         length_index < sizeof(lengths) / sizeof(lengths[0]);
         length_index++) {
        ULONG length = lengths[length_index];
        const UBYTE *data = &large_storage[LARGE_PREFIX_BYTES + data_offset];

        for (seed_index = 0;
             seed_index < sizeof(seeds) / sizeof(seeds[0]);
             seed_index++) {
            uint32_t seed = seeds[seed_index];
            uint32_t expected = reference_crc32(seed, data, length);

            for (split_index = 0;
                 split_index < sizeof(split_offsets) /
                               sizeof(split_offsets[0]);
                 split_index++) {
                ULONG split = split_offsets[split_index];
                CrcFunction functions[2];
                const char *names[2] = {"baseline", "candidate"};
                uint32_t expected_prefix;
                ULONG which;

                if (split > length) continue;
                expected_prefix = reference_crc32(seed, data, split);
                functions[0] = call_baseline;
                functions[1] = call_candidate;
                counts.large_chain_cases++;
                for (which = 0; which < 2; which++) {
                    uint32_t first = 0, final = 0;
                    ULONG mask;

                    counts.crc_calls[which] += 2;
                    mask = bfs_crc32_abi_probe(functions[which],
                        seed, data, (size_t)split, &first);
                    if (mask)
                        record_failure(names[which], "large-chain-prefix-abi",
                            split, data_offset, seed,
                            0, 0, mask);
                    if (first != expected_prefix)
                        record_failure(names[which], "large-chain-prefix",
                            split, data_offset, seed,
                            expected_prefix, first, 0);
                    mask = bfs_crc32_abi_probe(functions[which], first,
                        data + split, (size_t)(length - split), &final);
                    if (mask)
                        record_failure(names[which], "large-chain-suffix-abi",
                            length - split, data_offset + split, first,
                            0, 0, mask);
                    if (final != expected)
                        record_failure(names[which], "large-chain-final",
                            length, data_offset, seed,
                            expected, final, 0);
                }
            }
        }
    }
}

static BOOL open_eclock(struct MsgPort **port_out,
                        struct timerequest **request_out)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request;
    struct EClockVal initial = {0};

    *port_out = NULL;
    *request_out = NULL;
    if (!port) return FALSE;
    request = (struct timerequest *)CreateIORequest(
        port, (ULONG)sizeof(*request));
    if (!request) {
        DeleteMsgPort(port);
        return FALSE;
    }
    if (OpenDevice(TIMERNAME, UNIT_ECLOCK, (struct IORequest *)request, 0)) {
        DeleteIORequest((struct IORequest *)request);
        DeleteMsgPort(port);
        return FALSE;
    }
    TimerBase = (struct Device *)request->tr_node.io_Device;
    counts.clock_hz = ReadEClock(&initial);
    if (counts.clock_hz == 0) {
        CloseDevice((struct IORequest *)request);
        DeleteIORequest((struct IORequest *)request);
        DeleteMsgPort(port);
        TimerBase = NULL;
        return FALSE;
    }
    *port_out = port;
    *request_out = request;
    return TRUE;
}

static void close_eclock(struct MsgPort *port, struct timerequest *request)
{
    if (request) {
        CloseDevice((struct IORequest *)request);
        DeleteIORequest((struct IORequest *)request);
    }
    if (port) DeleteMsgPort(port);
    TimerBase = NULL;
}

static uint32_t time_batch(CrcFunction volatile *function_slot,
                           uint32_t seed, const UBYTE *data, ULONG length,
                           ULONG repeats, ULONG which, uint64_t *ticks_out)
{
    struct EClockVal start = {0}, end = {0};
    uint32_t checksum = 0x6D2B79F5u;
    ULONG index;

    (void)ReadEClock(&start);
    for (index = 0; index < repeats; index++) {
        CrcFunction function = *function_slot;
        uint32_t value = function(seed, data, (size_t)length);
        checksum = (checksum << 5) ^ (checksum >> 27) ^ value ^
                   (index * 0x9E3779B9u);
    }
    (void)ReadEClock(&end);
    *ticks_out = (((uint64_t)end.ev_hi << 32) | end.ev_lo) -
                 (((uint64_t)start.ev_hi << 32) | start.ev_lo);
    timing_sink = checksum;
    counts.timing_calls[which] += repeats;
    counts.timing_checksum ^= checksum;
    return checksum;
}

static void run_timing(uint64_t baseline_ticks[TIMING_LENGTH_COUNT]
                                                [TIMING_SAMPLE_COUNT],
                       uint64_t candidate_ticks[TIMING_LENGTH_COUNT]
                                                 [TIMING_SAMPLE_COUNT],
                       uint32_t *timing_mismatches)
{
    ULONG length_index, sample;
    static const uint32_t timing_seed = 0x12345678u;
    const UBYTE *data = &large_storage[LARGE_PREFIX_BYTES + 3];

    fill_pattern(large_storage, LARGE_STORAGE_BYTES, PATTERN_MIXED);
    for (length_index = 0; length_index < TIMING_LENGTH_COUNT; length_index++) {
        for (sample = 0; sample < TIMING_SAMPLE_COUNT; sample++) {
            uint32_t baseline_sum, candidate_sum;
            BOOL baseline_first =
                ((length_index + sample) & 1UL) == 0;

            if (baseline_first) {
                baseline_sum = time_batch(&call_baseline, timing_seed, data,
                    timing_lengths[length_index], timing_repeats[length_index],
                    1, &baseline_ticks[length_index][sample]);
                candidate_sum = time_batch(&call_candidate, timing_seed, data,
                    timing_lengths[length_index], timing_repeats[length_index],
                    0, &candidate_ticks[length_index][sample]);
                counts.timing_order_baseline_first++;
            } else {
                candidate_sum = time_batch(&call_candidate, timing_seed, data,
                    timing_lengths[length_index], timing_repeats[length_index],
                    0, &candidate_ticks[length_index][sample]);
                baseline_sum = time_batch(&call_baseline, timing_seed, data,
                    timing_lengths[length_index], timing_repeats[length_index],
                    1, &baseline_ticks[length_index][sample]);
                counts.timing_order_candidate_first++;
            }
            if (baseline_sum != candidate_sum) (*timing_mismatches)++;
            counts.timing_samples++;
        }
    }
}

static ULONG append_text(char *buffer, ULONG offset, const char *text)
{
    while (*text) buffer[offset++] = *text++;
    return offset;
}

static ULONG append_u64(char *buffer, ULONG offset, uint64_t value)
{
    char digits[21];
    ULONG count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (count) buffer[offset++] = digits[--count];
    return offset;
}

static ULONG append_hex32(char *buffer, ULONG offset, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        buffer[offset++] = hex[(value >> shift) & 0x0Fu];
    return offset;
}

static ULONG append_named_count(char *buffer, ULONG offset,
                                const char *name, uint64_t value)
{
    offset = append_text(buffer, offset, name);
    offset = append_u64(buffer, offset, value);
    return append_text(buffer, offset, "\n");
}

static ULONG build_report(char *buffer,
                          uint64_t baseline_ticks[TIMING_LENGTH_COUNT]
                                                 [TIMING_SAMPLE_COUNT],
                          uint64_t candidate_ticks[TIMING_LENGTH_COUNT]
                                                  [TIMING_SAMPLE_COUNT],
                          ULONG timing_mismatches, BOOL timer_available)
{
    ULONG offset = 0, length_index, sample;

    offset = append_text(buffer, offset, "mode=AmigaOS CRC32 kernel probe\n");
    offset = append_named_count(buffer, offset, "small_vector_cases=",
                                counts.small_vector_cases);
    offset = append_named_count(buffer, offset, "large_vector_cases=",
                                counts.large_vector_cases);
    offset = append_named_count(buffer, offset, "small_chain_cases=",
                                counts.small_chain_cases);
    offset = append_named_count(buffer, offset, "large_chain_cases=",
                                counts.large_chain_cases);
    offset = append_named_count(buffer, offset, "null_zero_cases=",
                                counts.null_zero_cases);
    offset = append_named_count(buffer, offset, "known_vector_cases=",
                                counts.known_vector_cases);
    offset = append_named_count(buffer, offset,
                                "abi_negative_control_cases=",
                                counts.abi_negative_control_cases);
    offset = append_named_count(buffer, offset,
                                "abi_negative_control_passes=",
                                counts.abi_negative_control_passes);
    offset = append_named_count(buffer, offset,
                                "abi_negative_control_errors=",
                                counts.abi_negative_control_errors);
    offset = append_named_count(buffer, offset, "baseline_crc_calls=",
                                counts.crc_calls[1]);
    offset = append_named_count(buffer, offset, "candidate_crc_calls=",
                                counts.crc_calls[0]);
    offset = append_named_count(buffer, offset, "baseline_abi_failures=",
                                counts.abi_failures[1]);
    offset = append_named_count(buffer, offset, "candidate_abi_failures=",
                                counts.abi_failures[0]);
    offset = append_named_count(buffer, offset, "baseline_crc_failures=",
                                counts.crc_failures[1]);
    offset = append_named_count(buffer, offset, "candidate_crc_failures=",
                                counts.crc_failures[0]);
    offset = append_named_count(buffer, offset, "timer_available=",
                                timer_available ? 1u : 0u);
    offset = append_named_count(buffer, offset, "clock_hz=", counts.clock_hz);
    offset = append_named_count(buffer, offset, "timing_samples=",
                                counts.timing_samples);
    offset = append_named_count(buffer, offset, "timing_mismatches=",
                                timing_mismatches);
    offset = append_named_count(buffer, offset,
                                "timing_order_baseline_first=",
                                counts.timing_order_baseline_first);
    offset = append_named_count(buffer, offset,
                                "timing_order_candidate_first=",
                                counts.timing_order_candidate_first);
    offset = append_named_count(buffer, offset, "baseline_timing_calls=",
                                counts.timing_calls[1]);
    offset = append_named_count(buffer, offset, "candidate_timing_calls=",
                                counts.timing_calls[0]);
    offset = append_text(buffer, offset, "timing_checksum=");
    offset = append_hex32(buffer, offset, counts.timing_checksum);
    offset = append_text(buffer, offset, "\n");

    if (timer_available) {
        offset = append_text(buffer, offset,
            "timing_note=raw EClock ticks only; call order alternates; "
            "no speed claim\n");
        offset = append_text(buffer, offset, "clock_hz=");
        offset = append_u64(buffer, offset, counts.clock_hz);
        offset = append_text(buffer, offset, "\n");
        offset = append_text(buffer, offset,
                             "timing_seed=12345678 data_offset=11\n");
        for (length_index = 0; length_index < TIMING_LENGTH_COUNT;
             length_index++) {
            offset = append_text(buffer, offset, "timing_length=");
            offset = append_u64(buffer, offset, timing_lengths[length_index]);
            offset = append_text(buffer, offset, " repeats=");
            offset = append_u64(buffer, offset, timing_repeats[length_index]);
            offset = append_text(buffer, offset, " samples=");
            for (sample = 0; sample < TIMING_SAMPLE_COUNT; sample++) {
                if (sample) offset = append_text(buffer, offset, ",");
                offset = append_u64(buffer, offset,
                                    baseline_ticks[length_index][sample]);
            }
            offset = append_text(buffer, offset, " candidate_ticks=");
            for (sample = 0; sample < TIMING_SAMPLE_COUNT; sample++) {
                if (sample) offset = append_text(buffer, offset, ",");
                offset = append_u64(buffer, offset,
                                    candidate_ticks[length_index][sample]);
            }
            offset = append_text(buffer, offset, "\n");
        }
    } else {
        offset = append_text(buffer, offset, "timing_error=EClock unavailable\n");
    }

    if (counts.failures || timing_mismatches || !timer_available) {
        offset = append_text(buffer, offset, "failures=");
        offset = append_u64(buffer, offset,
                            counts.failures + timing_mismatches);
        offset = append_text(buffer, offset, "\n");
        if (counts.first_mode) {
            offset = append_text(buffer, offset, "first_failure=");
            offset = append_text(buffer, offset, counts.first_mode);
            offset = append_text(buffer, offset, " check=");
            offset = append_text(buffer, offset, counts.first_check);
            offset = append_text(buffer, offset, " len=");
            offset = append_u64(buffer, offset, counts.first_length);
            offset = append_text(buffer, offset, " offset=");
            offset = append_u64(buffer, offset, counts.first_offset);
            offset = append_text(buffer, offset, " seed=");
            offset = append_hex32(buffer, offset, counts.first_seed);
            offset = append_text(buffer, offset, " expected=");
            offset = append_hex32(buffer, offset, counts.first_expected);
            offset = append_text(buffer, offset, " actual=");
            offset = append_hex32(buffer, offset, counts.first_actual);
            offset = append_text(buffer, offset, " abi_mask=");
            offset = append_hex32(buffer, offset, counts.first_abi_mask);
            offset = append_text(buffer, offset, "\n");
        }
        offset = append_text(buffer, offset, "CRC32-PROBE-FAIL\n");
    } else {
        offset = append_text(buffer, offset, "failures=0\nCRC32-PROBE-PASS\n");
    }
    return offset;
}

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    struct MsgPort *timer_port = NULL;
    struct timerequest *timer_request = NULL;
    uint64_t baseline_ticks[TIMING_LENGTH_COUNT][TIMING_SAMPLE_COUNT] = {{0}};
    uint64_t candidate_ticks[TIMING_LENGTH_COUNT][TIMING_SAMPLE_COUNT] = {{0}};
    ULONG timing_mismatches = 0;
    char report[REPORT_CAPACITY];
    ULONG report_length;
    BPTR result, marker;
    BOOL timer_available, recorded;
    BOOL success;
    static const char pass_marker[] = "CRC32-PROBE-PASS\n";
    static const char fail_marker[] = "CRC32-PROBE-FAIL\n";

    process->pr_WindowPtr = (APTR)-1;

    check_abi_negative_controls();
    check_known_vector();
    check_null_zero_vectors();
    check_small_vectors();
    check_large_vectors();
    check_small_chaining();
    check_large_chaining();

    timer_available = open_eclock(&timer_port, &timer_request);
    if (timer_available) {
        run_timing(baseline_ticks, candidate_ticks, &timing_mismatches);
        close_eclock(timer_port, timer_request);
        timer_port = NULL;
        timer_request = NULL;
    } else {
        record_failure(NULL, "EClock", 0, 0, 0, 0, 0, 0);
    }

    report_length = build_report(report, baseline_ticks, candidate_ticks,
                                 timing_mismatches, timer_available);
    result = Open((STRPTR)"SYS:Results/crc32-probe.txt", MODE_NEWFILE);
    if (!result) {
        static const char message[] = "CRC32-PROBE-FAIL result-open\n";
        (void)Write(Output(), (APTR)message, (LONG)(sizeof(message) - 1));
        process->pr_WindowPtr = old_window;
        return 20;
    }
    recorded = Write(result, (APTR)report, (LONG)report_length) ==
               (LONG)report_length;
    if (!Close(result)) recorded = FALSE;

    success = counts.failures == 0 && timing_mismatches == 0 &&
              timer_available;
    if (recorded) {
        const char *marker_text = success ? pass_marker : fail_marker;
        ULONG marker_length = success
            ? (ULONG)(sizeof(pass_marker) - 1)
            : (ULONG)(sizeof(fail_marker) - 1);
        marker = Open((STRPTR)"SYS:Results/crc32-probe.done", MODE_NEWFILE);
        if (!marker) {
            recorded = FALSE;
        } else {
            recorded = Write(marker, (APTR)marker_text,
                             (LONG)marker_length) == (LONG)marker_length;
            if (!Close(marker)) recorded = FALSE;
        }
    }

    process->pr_WindowPtr = old_window;
    return recorded && success ? 0 : 20;
}
