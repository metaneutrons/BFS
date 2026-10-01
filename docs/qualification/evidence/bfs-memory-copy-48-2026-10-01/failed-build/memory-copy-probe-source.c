/* SPDX-License-Identifier: MPL-2.0 */
/*
 * AmigaOS comparison probe for retained and candidate 68020 memcpy kernels.
 * The runner links the candidate as memcpy and renames the retained entry to
 * memcpy_baseline. Expected bytes are generated independently below.
 */

#include <exec/io.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include <stddef.h>
#include <stdint.h>

typedef void *(*CopyFunction)(void *, const void *, size_t);

void *memcpy(void *dst, const void *src, size_t length);
void *memcpy_baseline(void *dst, const void *src, size_t length);
void *bfs_crc32_abi_clobber_d2(void *dst, const void *src, size_t length);
void *bfs_crc32_abi_clobber_a2(void *dst, const void *src, size_t length);

/* The assembly witness has the same five-word stack shape: function, dst,
 * src, length, result. Its second argument is pointer-typed here so this
 * declaration exactly matches CopyFunction without casting function types. */
extern ULONG bfs_crc32_abi_probe(CopyFunction function, void *dst,
                                 const void *src, size_t length,
                                 void **result);

struct Device *TimerBase;

static CopyFunction volatile call_candidate = memcpy;
static CopyFunction volatile call_baseline = memcpy_baseline;
static volatile ULONG timing_sink;

#define SMALL_MAX_LENGTH 132UL
#define GUARD_PREFIX_BYTES 16UL
#define GUARD_SUFFIX_BYTES 16UL
#define MAX_ALIGNMENT_OFFSET 7UL
#define SMALL_STORAGE_BYTES 192UL
#define LARGE_MAX_LENGTH 65537UL
#define LARGE_STORAGE_BYTES \
    (LARGE_MAX_LENGTH + GUARD_PREFIX_BYTES + MAX_ALIGNMENT_OFFSET + \
     GUARD_SUFFIX_BYTES)
#define BULK_MAX_LENGTH (48UL * 65536UL + 1UL)
#define BULK_STORAGE_BYTES \
    (BULK_MAX_LENGTH + GUARD_PREFIX_BYTES + MAX_ALIGNMENT_OFFSET + \
     GUARD_SUFFIX_BYTES)
#define TIMING_MAX_LENGTH (1024UL * 1024UL)
#define TIMING_STORAGE_BYTES \
    (TIMING_MAX_LENGTH + GUARD_PREFIX_BYTES + MAX_ALIGNMENT_OFFSET + \
     GUARD_SUFFIX_BYTES)
#define SMALL_CASES (133UL * 8UL * 8UL)
#define LARGE_CASES (6UL * 8UL * 8UL)
#define BULK_CASES (6UL * 2UL)
#define TIMING_LENGTH_COUNT 8UL
#define TIMING_SAMPLE_COUNT 6UL
#define MAX_TIMING_REPEATS 20000UL
#define REPORT_CAPACITY 8192UL

static const ULONG large_lengths[] = {
    4095UL, 4096UL, 4097UL, 65535UL, 65536UL, 65537UL
};

static const ULONG bulk_lengths[] = {
    44UL * 65536UL - 1UL,
    44UL * 65536UL,
    44UL * 65536UL + 1UL,
    48UL * 65536UL - 1UL,
    48UL * 65536UL,
    48UL * 65536UL + 1UL
};

static const ULONG bulk_src_offsets[] = {0UL, 1UL};
static const ULONG bulk_dst_offsets[] = {0UL, 7UL};

static const ULONG timing_lengths[TIMING_LENGTH_COUNT] = {
    0UL, 44UL, 48UL, 264UL, 512UL, 4096UL, 65536UL, TIMING_MAX_LENGTH
};

static const char *const kernel_names[2] = {"candidate", "baseline"};

enum {
    KERNEL_CANDIDATE = 0,
    KERNEL_BASELINE = 1
};

typedef struct {
    ULONG small_copy_cases;
    ULONG large_copy_cases;
    ULONG bulk_copy_cases;
    ULONG kernel_calls[2];
    ULONG kernel_abi_failures[2];
    ULONG kernel_return_failures[2];
    ULONG kernel_data_failures[2];
    ULONG abi_negative_control_cases;
    ULONG abi_negative_control_passes;
    ULONG abi_negative_control_errors;
    ULONG timing_calls[2];
    ULONG timing_batches[2];
    ULONG timing_samples;
    ULONG timing_order_candidate_first;
    ULONG timing_order_baseline_first;
    ULONG timing_mismatches;
    ULONG timing_repeat_clamps;
    ULONG failures;
    ULONG clock_hz;
    ULONG allocation_failures;
    const char *first_kernel;
    const char *first_check;
    ULONG first_length;
    ULONG first_dst_offset;
    ULONG first_src_offset;
    ULONG first_index;
    ULONG first_expected;
    ULONG first_actual;
    ULONG first_abi_mask;
} ProbeCounts;

static ProbeCounts counts;

static UBYTE small_src_storage[SMALL_STORAGE_BYTES]
    __attribute__((aligned(8)));
static UBYTE small_dst_storage[SMALL_STORAGE_BYTES]
    __attribute__((aligned(8)));

static UBYTE source_byte(ULONG index)
{
    return (UBYTE)((index * 37UL + (index >> 3) * 13UL + 0x5BUL) &
                   0xFFUL);
}

static UBYTE destination_guard_byte(ULONG index)
{
    return (UBYTE)((index * 31UL + (index >> 2) * 17UL + 0x39UL) &
                   0xFFUL);
}

static ULONG case_storage_bytes(ULONG length, ULONG dst_offset,
                                ULONG src_offset)
{
    ULONG final_offset = dst_offset > src_offset ? dst_offset : src_offset;
    return GUARD_PREFIX_BYTES + final_offset + length + GUARD_SUFFIX_BYTES;
}

static void fill_source(UBYTE *storage, ULONG storage_bytes)
{
    ULONG index;
    for (index = 0; index < storage_bytes; index++)
        storage[index] = source_byte(index);
}

static void fill_destination_guard(UBYTE *storage, ULONG storage_bytes)
{
    ULONG index;
    for (index = 0; index < storage_bytes; index++)
        storage[index] = destination_guard_byte(index);
}

static void record_failure(const char *kernel, const char *check,
                           ULONG length, ULONG dst_offset,
                           ULONG src_offset, ULONG index,
                           ULONG expected, ULONG actual, ULONG abi_mask)
{
    counts.failures++;
    if (kernel && kernel[0] == 'c') {
        if (abi_mask) counts.kernel_abi_failures[KERNEL_CANDIDATE]++;
        else if (check && check[0] == 'r')
            counts.kernel_return_failures[KERNEL_CANDIDATE]++;
        else counts.kernel_data_failures[KERNEL_CANDIDATE]++;
    } else if (kernel && kernel[0] == 'b' && kernel[1] == 'a') {
        if (abi_mask) counts.kernel_abi_failures[KERNEL_BASELINE]++;
        else if (check && check[0] == 'r')
            counts.kernel_return_failures[KERNEL_BASELINE]++;
        else counts.kernel_data_failures[KERNEL_BASELINE]++;
    }
    if (counts.failures != 1) return;

    counts.first_kernel = kernel;
    counts.first_check = check;
    counts.first_length = length;
    counts.first_dst_offset = dst_offset;
    counts.first_src_offset = src_offset;
    counts.first_index = index;
    counts.first_expected = expected;
    counts.first_actual = actual;
    counts.first_abi_mask = abi_mask;
}

static void verify_copy_result(const char *kernel, ULONG length,
                               ULONG dst_offset, ULONG src_offset,
                               UBYTE *dst_storage, UBYTE *src_storage,
                               ULONG storage_bytes, void *dst, void *returned,
                               ULONG abi_mask)
{
    ULONG index;
    ULONG dst_start = GUARD_PREFIX_BYTES + dst_offset;
    ULONG src_start = GUARD_PREFIX_BYTES + src_offset;

    if (abi_mask) {
        record_failure(kernel, "abi", length, dst_offset, src_offset, 0,
                       0, 0, abi_mask);
    }
    if (returned != dst) {
        record_failure(kernel, "return", length, dst_offset, src_offset, 0,
                       (ULONG)(uintptr_t)dst,
                       (ULONG)(uintptr_t)returned, 0);
    }

    for (index = 0; index < storage_bytes; index++) {
        UBYTE expected = destination_guard_byte(index);
        if (index >= dst_start && index < dst_start + length) {
            ULONG source_index = src_start + index - dst_start;
            expected = source_byte(source_index);
        }
        if (dst_storage[index] != expected) {
            record_failure(kernel, "destination", length, dst_offset,
                           src_offset, index, expected, dst_storage[index], 0);
            break;
        }
    }

    for (index = 0; index < storage_bytes; index++) {
        UBYTE expected = source_byte(index);
        if (src_storage[index] != expected) {
            record_failure(kernel, "source-unchanged", length,
                           dst_offset, src_offset, index, expected,
                           src_storage[index], 0);
            break;
        }
    }
}

static void run_functional_copy(CopyFunction function, ULONG which,
                                ULONG length, ULONG dst_offset,
                                ULONG src_offset, UBYTE *dst_storage,
                                UBYTE *src_storage, ULONG storage_bytes)
{
    UBYTE *dst;
    const UBYTE *src;
    void *returned = NULL;
    ULONG abi_mask;

    fill_source(src_storage, storage_bytes);
    fill_destination_guard(dst_storage, storage_bytes);
    dst = dst_storage + GUARD_PREFIX_BYTES + dst_offset;
    src = src_storage + GUARD_PREFIX_BYTES + src_offset;

    counts.kernel_calls[which]++;
    abi_mask = bfs_crc32_abi_probe(function, dst, src, (size_t)length,
                                   &returned);
    verify_copy_result(kernel_names[which], length, dst_offset, src_offset,
                       dst_storage, src_storage, storage_bytes, dst, returned,
                       abi_mask);
}

static void run_copy_pair(ULONG length, ULONG dst_offset, ULONG src_offset,
                          UBYTE *dst_storage, UBYTE *src_storage,
                          ULONG storage_bytes)
{
    CopyFunction baseline = call_baseline;
    CopyFunction candidate = call_candidate;

    run_functional_copy(baseline, KERNEL_BASELINE, length, dst_offset,
                        src_offset, dst_storage, src_storage, storage_bytes);
    run_functional_copy(candidate, KERNEL_CANDIDATE, length, dst_offset,
                        src_offset, dst_storage, src_storage, storage_bytes);
}

static void check_abi_negative_controls(void)
{
    static CopyFunction controls[2] = {
        bfs_crc32_abi_clobber_d2,
        bfs_crc32_abi_clobber_a2
    };
    static const ULONG expected_masks[2] = {0x00000004UL, 0x00000100UL};
    UBYTE dummy[8] = {0};
    ULONG index;

    for (index = 0; index < 2; index++) {
        void *dst = &dummy[0];
        void *returned = NULL;
        ULONG mask = bfs_crc32_abi_probe(controls[index], dst, &dummy[1], 0,
                                         &returned);

        counts.abi_negative_control_cases++;
        if (mask == expected_masks[index] && returned == dst) {
            counts.abi_negative_control_passes++;
        } else {
            counts.abi_negative_control_errors++;
            if (mask != expected_masks[index])
                record_failure("abi-witness", "negative-control-mask", 0,
                    0, 0, index, expected_masks[index], mask, mask);
            if (returned != dst)
                record_failure("abi-witness", "negative-control-return", 0,
                    0, 0, index, (ULONG)(uintptr_t)dst,
                    (ULONG)(uintptr_t)returned, mask);
        }
    }
}

static void check_small_copies(void)
{
    ULONG length, dst_offset, src_offset;

    for (length = 0; length <= SMALL_MAX_LENGTH; length++) {
        for (dst_offset = 0; dst_offset <= MAX_ALIGNMENT_OFFSET;
             dst_offset++) {
            for (src_offset = 0; src_offset <= MAX_ALIGNMENT_OFFSET;
                 src_offset++) {
                ULONG storage_bytes = case_storage_bytes(
                    length, dst_offset, src_offset);
                run_copy_pair(length, dst_offset, src_offset,
                    small_dst_storage, small_src_storage,
                    storage_bytes);
                counts.small_copy_cases++;
            }
        }
    }
}

static BOOL check_large_copies(void)
{
    UBYTE *dst_storage = (UBYTE *)AllocMem(LARGE_STORAGE_BYTES, MEMF_PUBLIC);
    UBYTE *src_storage = (UBYTE *)AllocMem(LARGE_STORAGE_BYTES, MEMF_PUBLIC);
    ULONG length_index, dst_offset, src_offset;

    if (!dst_storage || !src_storage) {
        counts.allocation_failures++;
        record_failure("large-setup", "AllocMem", LARGE_MAX_LENGTH,
                       0, 0, 0, LARGE_STORAGE_BYTES,
                       (dst_storage ? LARGE_STORAGE_BYTES : 0) +
                       (src_storage ? LARGE_STORAGE_BYTES : 0), 0);
        if (dst_storage) FreeMem(dst_storage, LARGE_STORAGE_BYTES);
        if (src_storage) FreeMem(src_storage, LARGE_STORAGE_BYTES);
        return FALSE;
    }

    for (length_index = 0;
         length_index < sizeof(large_lengths) / sizeof(large_lengths[0]);
         length_index++) {
        ULONG length = large_lengths[length_index];
        for (dst_offset = 0; dst_offset <= MAX_ALIGNMENT_OFFSET;
             dst_offset++) {
            for (src_offset = 0; src_offset <= MAX_ALIGNMENT_OFFSET;
                 src_offset++) {
                ULONG storage_bytes = case_storage_bytes(
                    length, dst_offset, src_offset);
                run_copy_pair(length, dst_offset, src_offset, dst_storage,
                              src_storage, storage_bytes);
                counts.large_copy_cases++;
            }
        }
    }

    FreeMem(src_storage, LARGE_STORAGE_BYTES);
    FreeMem(dst_storage, LARGE_STORAGE_BYTES);
    return TRUE;
}

static BOOL check_bulk_neighborhood_copies(void)
{
    UBYTE *dst_storage = (UBYTE *)AllocMem(BULK_STORAGE_BYTES, MEMF_PUBLIC);
    UBYTE *src_storage = (UBYTE *)AllocMem(BULK_STORAGE_BYTES, MEMF_PUBLIC);
    ULONG length_index, offset_index;

    if (!dst_storage || !src_storage) {
        counts.allocation_failures++;
        record_failure("bulk-setup", "AllocMem", BULK_MAX_LENGTH,
                       0, 0, 0, BULK_STORAGE_BYTES,
                       (dst_storage ? BULK_STORAGE_BYTES : 0) +
                       (src_storage ? BULK_STORAGE_BYTES : 0), 0);
        if (dst_storage) FreeMem(dst_storage, BULK_STORAGE_BYTES);
        if (src_storage) FreeMem(src_storage, BULK_STORAGE_BYTES);
        return FALSE;
    }

    for (length_index = 0;
         length_index < sizeof(bulk_lengths) / sizeof(bulk_lengths[0]);
         length_index++) {
        ULONG length = bulk_lengths[length_index];
        for (offset_index = 0;
             offset_index < sizeof(bulk_src_offsets) /
                            sizeof(bulk_src_offsets[0]);
             offset_index++) {
            ULONG dst_offset = bulk_dst_offsets[offset_index];
            ULONG src_offset = bulk_src_offsets[offset_index];
            ULONG storage_bytes = case_storage_bytes(
                length, dst_offset, src_offset);
            run_copy_pair(length, dst_offset, src_offset, dst_storage,
                          src_storage, storage_bytes);
            counts.bulk_copy_cases++;
        }
    }

    FreeMem(src_storage, BULK_STORAGE_BYTES);
    FreeMem(dst_storage, BULK_STORAGE_BYTES);
    return TRUE;
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

static uint64_t eclock_value(const struct EClockVal *value)
{
    return ((uint64_t)value->ev_hi << 32) | value->ev_lo;
}

static void verify_timing_batch(const char *kernel, ULONG length,
                                ULONG dst_offset, ULONG src_offset,
                                UBYTE *dst_storage, UBYTE *src_storage,
                                ULONG storage_bytes, void *dst,
                                void *returned)
{
    verify_copy_result(kernel, length, dst_offset, src_offset, dst_storage,
                       src_storage, storage_bytes, dst, returned, 0);
}

static uint32_t run_timing_batch(CopyFunction volatile *function_slot,
                                 ULONG which, UBYTE *dst_storage,
                                 UBYTE *src_storage, ULONG storage_bytes,
                                 ULONG length, ULONG dst_offset,
                                 ULONG src_offset, ULONG repeats,
                                 uint64_t *ticks_out)
{
    struct EClockVal start = {0}, end = {0};
    CopyFunction function;
    UBYTE *dst;
    const UBYTE *src;
    void *last_return = NULL;
    uint32_t checksum = 0x6D2B79F5u;
    ULONG index;

    fill_source(src_storage, storage_bytes);
    fill_destination_guard(dst_storage, storage_bytes);
    dst = dst_storage + GUARD_PREFIX_BYTES + dst_offset;
    src = src_storage + GUARD_PREFIX_BYTES + src_offset;

    (void)ReadEClock(&start);
    for (index = 0; index < repeats; index++) {
        function = *function_slot;
        last_return = function(dst, src, (size_t)length);
        checksum = (checksum << 5) ^ (checksum >> 27) ^
                   (uint32_t)(uintptr_t)last_return ^
                   (index * 0x9E3779B9UL);
    }
    (void)ReadEClock(&end);
    *ticks_out = eclock_value(&end) - eclock_value(&start);

    timing_sink = (ULONG)checksum;
    counts.timing_calls[which] += repeats;
    counts.timing_batches[which]++;
    verify_timing_batch(kernel_names[which], length, dst_offset, src_offset,
                        dst_storage, src_storage, storage_bytes, dst,
                        last_return);
    return checksum;
}

static ULONG calibrated_repeats(uint64_t baseline_ticks,
                                uint64_t candidate_ticks,
                                ULONG clock_hz, BOOL *clamped)
{
    uint64_t per_call = baseline_ticks > candidate_ticks
        ? baseline_ticks : candidate_ticks;
    uint64_t target_ticks = ((uint64_t)clock_hz + 49u) / 50u;
    uint64_t repeats;

    *clamped = FALSE;
    if (target_ticks == 0) target_ticks = 1;
    if (per_call == 0) {
        repeats = MAX_TIMING_REPEATS;
        *clamped = TRUE;
    } else {
        repeats = (target_ticks + per_call - 1u) / per_call;
        if (repeats == 0) repeats = 1;
        if (repeats > MAX_TIMING_REPEATS) {
            repeats = MAX_TIMING_REPEATS;
            *clamped = TRUE;
        }
    }
    return (ULONG)repeats;
}

static BOOL run_timing(uint64_t baseline_ticks[TIMING_LENGTH_COUNT]
                                                [TIMING_SAMPLE_COUNT],
                       uint64_t candidate_ticks[TIMING_LENGTH_COUNT]
                                                 [TIMING_SAMPLE_COUNT],
                       uint64_t baseline_calibration[TIMING_LENGTH_COUNT],
                       uint64_t candidate_calibration[TIMING_LENGTH_COUNT],
                       ULONG repeats_by_length[TIMING_LENGTH_COUNT])
{
    UBYTE *dst_storage = (UBYTE *)AllocMem(TIMING_STORAGE_BYTES, MEMF_PUBLIC);
    UBYTE *src_storage = (UBYTE *)AllocMem(TIMING_STORAGE_BYTES, MEMF_PUBLIC);
    ULONG length_index, sample;
    const ULONG dst_offset = 5UL;
    const ULONG src_offset = 3UL;

    if (!dst_storage || !src_storage) {
        counts.allocation_failures++;
        record_failure("timing-setup", "AllocMem", TIMING_MAX_LENGTH,
                       dst_offset, src_offset, 0, TIMING_STORAGE_BYTES,
                       (dst_storage ? TIMING_STORAGE_BYTES : 0) +
                       (src_storage ? TIMING_STORAGE_BYTES : 0), 0);
        if (dst_storage) FreeMem(dst_storage, TIMING_STORAGE_BYTES);
        if (src_storage) FreeMem(src_storage, TIMING_STORAGE_BYTES);
        return FALSE;
    }

    for (length_index = 0; length_index < TIMING_LENGTH_COUNT;
         length_index++) {
        BOOL clamped;
        uint32_t baseline_check, candidate_check;
        ULONG storage_bytes = case_storage_bytes(
            timing_lengths[length_index], dst_offset, src_offset);

        baseline_check = run_timing_batch(&call_baseline, KERNEL_BASELINE,
            dst_storage, src_storage, storage_bytes,
            timing_lengths[length_index], dst_offset, src_offset, 1UL,
            &baseline_calibration[length_index]);
        candidate_check = run_timing_batch(&call_candidate, KERNEL_CANDIDATE,
            dst_storage, src_storage, storage_bytes,
            timing_lengths[length_index], dst_offset, src_offset, 1UL,
            &candidate_calibration[length_index]);
        if (baseline_check != candidate_check) {
            counts.timing_mismatches++;
            record_failure("timing", "calibration-checksum",
                timing_lengths[length_index], dst_offset, src_offset,
                length_index, baseline_check, candidate_check, 0);
        }
        repeats_by_length[length_index] = calibrated_repeats(
            baseline_calibration[length_index],
            candidate_calibration[length_index], counts.clock_hz, &clamped);
        if (clamped) counts.timing_repeat_clamps++;

        for (sample = 0; sample < TIMING_SAMPLE_COUNT; sample++) {
            uint32_t baseline_sum, candidate_sum;
            BOOL baseline_first = ((length_index + sample) & 1UL) == 0;
            ULONG repeats = repeats_by_length[length_index];

            if (baseline_first) {
                baseline_sum = run_timing_batch(&call_baseline,
                    KERNEL_BASELINE, dst_storage, src_storage,
                    storage_bytes, timing_lengths[length_index],
                    dst_offset, src_offset, repeats,
                    &baseline_ticks[length_index][sample]);
                candidate_sum = run_timing_batch(&call_candidate,
                    KERNEL_CANDIDATE, dst_storage, src_storage,
                    storage_bytes, timing_lengths[length_index],
                    dst_offset, src_offset, repeats,
                    &candidate_ticks[length_index][sample]);
                counts.timing_order_baseline_first++;
            } else {
                candidate_sum = run_timing_batch(&call_candidate,
                    KERNEL_CANDIDATE, dst_storage, src_storage,
                    storage_bytes, timing_lengths[length_index],
                    dst_offset, src_offset, repeats,
                    &candidate_ticks[length_index][sample]);
                baseline_sum = run_timing_batch(&call_baseline,
                    KERNEL_BASELINE, dst_storage, src_storage,
                    storage_bytes, timing_lengths[length_index],
                    dst_offset, src_offset, repeats,
                    &baseline_ticks[length_index][sample]);
                counts.timing_order_candidate_first++;
            }
            if (baseline_sum != candidate_sum) {
                counts.timing_mismatches++;
                record_failure("timing", "sample-checksum",
                    timing_lengths[length_index], dst_offset, src_offset,
                    sample, baseline_sum, candidate_sum, 0);
            }
            counts.timing_samples++;
        }
    }

    FreeMem(src_storage, TIMING_STORAGE_BYTES);
    FreeMem(dst_storage, TIMING_STORAGE_BYTES);
    return TRUE;
}

static ULONG append_text(char *buffer, ULONG offset, const char *text)
{
    while (*text) buffer[offset++] = *text++;
    return offset;
}

static ULONG append_decimal(char *buffer, ULONG offset, uint64_t value)
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

static ULONG append_hex32(char *buffer, ULONG offset, ULONG value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        buffer[offset++] = hex[(value >> shift) & 0x0FUL];
    return offset;
}

static ULONG append_named_count(char *buffer, ULONG offset,
                                const char *name, uint64_t value)
{
    offset = append_text(buffer, offset, name);
    offset = append_decimal(buffer, offset, value);
    return append_text(buffer, offset, "\n");
}

static ULONG append_tick_row(char *buffer, ULONG offset, ULONG length_index,
                             uint64_t baseline_ticks[TIMING_LENGTH_COUNT]
                                                    [TIMING_SAMPLE_COUNT],
                             uint64_t candidate_ticks[TIMING_LENGTH_COUNT]
                                                      [TIMING_SAMPLE_COUNT],
                             uint64_t baseline_calibration[TIMING_LENGTH_COUNT],
                             uint64_t candidate_calibration[TIMING_LENGTH_COUNT],
                             ULONG repeats)
{
    ULONG sample;

    offset = append_text(buffer, offset, "timing_length=");
    offset = append_decimal(buffer, offset, timing_lengths[length_index]);
    offset = append_text(buffer, offset, " repeats=");
    offset = append_decimal(buffer, offset, repeats);
    offset = append_text(buffer, offset, " calibration_ticks=");
    offset = append_decimal(buffer, offset,
                            baseline_calibration[length_index]);
    offset = append_text(buffer, offset, ",");
    offset = append_decimal(buffer, offset,
                            candidate_calibration[length_index]);
    offset = append_text(buffer, offset, " baseline_ticks=");
    for (sample = 0; sample < TIMING_SAMPLE_COUNT; sample++) {
        if (sample) offset = append_text(buffer, offset, ",");
        offset = append_decimal(buffer, offset,
                                baseline_ticks[length_index][sample]);
    }
    offset = append_text(buffer, offset, " candidate_ticks=");
    for (sample = 0; sample < TIMING_SAMPLE_COUNT; sample++) {
        if (sample) offset = append_text(buffer, offset, ",");
        offset = append_decimal(buffer, offset,
                                candidate_ticks[length_index][sample]);
    }
    return append_text(buffer, offset, "\n");
}

static ULONG build_report(char *buffer,
                          uint64_t baseline_ticks[TIMING_LENGTH_COUNT]
                                                 [TIMING_SAMPLE_COUNT],
                          uint64_t candidate_ticks[TIMING_LENGTH_COUNT]
                                                  [TIMING_SAMPLE_COUNT],
                          uint64_t baseline_calibration[TIMING_LENGTH_COUNT],
                          uint64_t candidate_calibration[TIMING_LENGTH_COUNT],
                          ULONG repeats_by_length[TIMING_LENGTH_COUNT],
                          BOOL timer_available,
                          BOOL timing_buffers_available)
{
    ULONG offset = 0, length_index;

    offset = append_text(buffer, offset,
                         "mode=AmigaOS memcpy candidate comparison\n");
    offset = append_named_count(buffer, offset, "small_expected_cases=",
                                SMALL_CASES);
    offset = append_named_count(buffer, offset, "small_completed_cases=",
                                counts.small_copy_cases);
    offset = append_named_count(buffer, offset, "large_expected_cases=",
                                LARGE_CASES);
    offset = append_named_count(buffer, offset, "large_completed_cases=",
                                counts.large_copy_cases);
    offset = append_named_count(buffer, offset, "bulk_expected_cases=",
                                BULK_CASES);
    offset = append_named_count(buffer, offset, "bulk_completed_cases=",
                                counts.bulk_copy_cases);
    offset = append_text(buffer, offset,
        "small_lengths=0..132; all 64 src/dst offsets; deterministic mixed "
        "source pattern\n");
    offset = append_text(buffer, offset,
        "large_lengths=4095,4096,4097,65535,65536,65537; "
        "all 64 src/dst offsets; deterministic mixed source pattern\n");
    offset = append_text(buffer, offset,
        "bulk_lengths=44*65536-1,44*65536,+1,48*65536-1,48*65536,+1; "
        "offset pairs=(0,0),(1,7)\n");
    offset = append_named_count(buffer, offset, "candidate_checked_calls=",
                                counts.kernel_calls[KERNEL_CANDIDATE]);
    offset = append_named_count(buffer, offset, "baseline_checked_calls=",
                                counts.kernel_calls[KERNEL_BASELINE]);
    offset = append_named_count(buffer, offset, "candidate_abi_failures=",
                                counts.kernel_abi_failures[KERNEL_CANDIDATE]);
    offset = append_named_count(buffer, offset, "baseline_abi_failures=",
                                counts.kernel_abi_failures[KERNEL_BASELINE]);
    offset = append_named_count(buffer, offset, "candidate_return_failures=",
                                counts.kernel_return_failures[KERNEL_CANDIDATE]);
    offset = append_named_count(buffer, offset, "baseline_return_failures=",
                                counts.kernel_return_failures[KERNEL_BASELINE]);
    offset = append_named_count(buffer, offset, "candidate_data_failures=",
                                counts.kernel_data_failures[KERNEL_CANDIDATE]);
    offset = append_named_count(buffer, offset, "baseline_data_failures=",
                                counts.kernel_data_failures[KERNEL_BASELINE]);
    offset = append_named_count(buffer, offset,
                                "abi_negative_control_cases=",
                                counts.abi_negative_control_cases);
    offset = append_named_count(buffer, offset,
                                "abi_negative_control_passes=",
                                counts.abi_negative_control_passes);
    offset = append_named_count(buffer, offset,
                                "abi_negative_control_errors=",
                                counts.abi_negative_control_errors);
    offset = append_named_count(buffer, offset, "allocation_failures=",
                                counts.allocation_failures);
    offset = append_named_count(buffer, offset, "timing_available=",
                                timer_available ? 1UL : 0UL);
    offset = append_named_count(buffer, offset, "timing_buffers_available=",
                                timing_buffers_available ? 1UL : 0UL);
    offset = append_named_count(buffer, offset, "clock_hz=", counts.clock_hz);
    offset = append_named_count(buffer, offset, "timing_samples=",
                                counts.timing_samples);
    offset = append_named_count(buffer, offset,
                                "timing_order_baseline_first=",
                                counts.timing_order_baseline_first);
    offset = append_named_count(buffer, offset,
                                "timing_order_candidate_first=",
                                counts.timing_order_candidate_first);
    offset = append_named_count(buffer, offset, "timing_repeat_clamps=",
                                counts.timing_repeat_clamps);
    offset = append_named_count(buffer, offset, "timing_mismatches=",
                                counts.timing_mismatches);
    offset = append_named_count(buffer, offset, "baseline_timing_calls=",
                                counts.timing_calls[KERNEL_BASELINE]);
    offset = append_named_count(buffer, offset, "candidate_timing_calls=",
                                counts.timing_calls[KERNEL_CANDIDATE]);
    offset = append_named_count(buffer, offset, "baseline_timing_batches=",
                                counts.timing_batches[KERNEL_BASELINE]);
    offset = append_named_count(buffer, offset, "candidate_timing_batches=",
                                counts.timing_batches[KERNEL_CANDIDATE]);
    offset = append_text(buffer, offset,
        "functional_buffers=two AllocMem regions; largest pair total bytes=");
    offset = append_decimal(buffer, offset,
                            (uint64_t)BULK_STORAGE_BYTES * 2u);
    offset = append_text(buffer, offset, "\ntiming_buffers=two AllocMem regions; total bytes=");
    offset = append_decimal(buffer, offset,
                            (uint64_t)TIMING_STORAGE_BYTES * 2u);
    offset = append_text(buffer, offset, "\n");

    offset = append_text(buffer, offset,
        "timing_note=raw EClock ticks; six alternating paired samples; "
        "20ms target, repeats capped; src_offset=3 dst_offset=5\n");
    offset = append_text(buffer, offset,
        "abi_note=callee-saved D2-D7/A2-A6 and SP witness; arbitrary SP "
        "corruption may prevent return\n");
    if (timer_available && timing_buffers_available) {
        for (length_index = 0; length_index < TIMING_LENGTH_COUNT;
             length_index++) {
            offset = append_tick_row(buffer, offset, length_index,
                baseline_ticks, candidate_ticks,
                baseline_calibration, candidate_calibration,
                repeats_by_length[length_index]);
        }
    } else {
        offset = append_text(buffer, offset,
            timer_available ? "timing_error=timing buffer allocation failed\n"
                            : "timing_error=EClock unavailable\n");
    }

    if (counts.failures || !timer_available) {
        offset = append_named_count(buffer, offset, "failures=",
                                    counts.failures);
        if (counts.first_kernel) {
            offset = append_text(buffer, offset, "first_failure=");
            offset = append_text(buffer, offset, counts.first_kernel);
            offset = append_text(buffer, offset, " check=");
            offset = append_text(buffer, offset, counts.first_check);
            offset = append_text(buffer, offset, " len=");
            offset = append_decimal(buffer, offset, counts.first_length);
            offset = append_text(buffer, offset, " dst_offset=");
            offset = append_decimal(buffer, offset, counts.first_dst_offset);
            offset = append_text(buffer, offset, " src_offset=");
            offset = append_decimal(buffer, offset, counts.first_src_offset);
            offset = append_text(buffer, offset, " index=");
            offset = append_decimal(buffer, offset, counts.first_index);
            offset = append_text(buffer, offset, " expected=");
            offset = append_hex32(buffer, offset, counts.first_expected);
            offset = append_text(buffer, offset, " actual=");
            offset = append_hex32(buffer, offset, counts.first_actual);
            offset = append_text(buffer, offset, " abi_mask=");
            offset = append_hex32(buffer, offset, counts.first_abi_mask);
            offset = append_text(buffer, offset, "\n");
        }
        offset = append_text(buffer, offset, "MEMORY-COPY-COMPARE-FAIL\n");
    } else {
        offset = append_text(buffer, offset,
                             "failures=0\nMEMORY-COPY-COMPARE-PASS\n");
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
    uint64_t baseline_calibration[TIMING_LENGTH_COUNT] = {0};
    uint64_t candidate_calibration[TIMING_LENGTH_COUNT] = {0};
    ULONG repeats_by_length[TIMING_LENGTH_COUNT] = {0};
    char report[REPORT_CAPACITY];
    ULONG report_length;
    BPTR result, marker;
    BOOL timer_available, timing_buffers_available = FALSE;
    BOOL recorded, success;
    static const char pass_marker[] = "MEMORY-COPY-COMPARE-PASS\n";
    static const char fail_marker[] = "MEMORY-COPY-COMPARE-FAIL\n";

    process->pr_WindowPtr = (APTR)-1;

    check_abi_negative_controls();
    check_small_copies();
    (void)check_large_copies();
    (void)check_bulk_neighborhood_copies();

    timer_available = open_eclock(&timer_port, &timer_request);
    if (timer_available) {
        timing_buffers_available = run_timing(
            baseline_ticks, candidate_ticks, baseline_calibration,
            candidate_calibration, repeats_by_length);
        close_eclock(timer_port, timer_request);
        timer_port = NULL;
        timer_request = NULL;
    } else {
        record_failure("timing", "EClock", 0, 0, 0, 0, 0, 0, 0);
    }

    report_length = build_report(report, baseline_ticks, candidate_ticks,
        baseline_calibration, candidate_calibration, repeats_by_length,
        timer_available, timing_buffers_available);
    result = Open((STRPTR)"SYS:Results/memory-copy-compare.txt", MODE_NEWFILE);
    if (!result) {
        static const char message[] =
            "MEMORY-COPY-COMPARE-FAIL result-open\n";
        (void)Write(Output(), (APTR)message, (LONG)(sizeof(message) - 1));
        process->pr_WindowPtr = old_window;
        return 20;
    }
    recorded = Write(result, (APTR)report, (LONG)report_length) ==
               (LONG)report_length;
    if (!Close(result)) recorded = FALSE;

    success = counts.failures == 0 && timer_available &&
              timing_buffers_available &&
              counts.small_copy_cases == SMALL_CASES &&
              counts.large_copy_cases == LARGE_CASES &&
              counts.bulk_copy_cases == BULK_CASES &&
              counts.kernel_calls[KERNEL_CANDIDATE] ==
                  SMALL_CASES + LARGE_CASES + BULK_CASES &&
              counts.kernel_calls[KERNEL_BASELINE] ==
                  SMALL_CASES + LARGE_CASES + BULK_CASES &&
              counts.abi_negative_control_passes == 2 &&
              counts.timing_samples == TIMING_LENGTH_COUNT *
                                        TIMING_SAMPLE_COUNT;
    if (recorded) {
        const char *marker_text = success ? pass_marker : fail_marker;
        ULONG marker_length = success
            ? (ULONG)(sizeof(pass_marker) - 1)
            : (ULONG)(sizeof(fail_marker) - 1);
        marker = Open((STRPTR)"SYS:Results/memory-copy-compare.done",
                      MODE_NEWFILE);
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
