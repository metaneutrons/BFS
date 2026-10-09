/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Execute the project's 68020 memcpy/memset implementations inside AmigaOS.
 * The emulator runner links src/amiga/memcpy_68k.s directly and uses
 * volatile function pointers so the C compiler cannot replace these calls
 * with builtins.
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <dos/dosextens.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t length);
void *memset(void *dst, int value, size_t length);

typedef void *(*CopyFunction)(void *, const void *, size_t);
typedef void *(*SetEntry)(void *, int, size_t);

static CopyFunction volatile call_memcpy = memcpy;
static SetEntry volatile call_memset = memset;

#define SMALL_MAX_LENGTH 132UL
#define SMALL_STORAGE_BYTES 192UL
#define SMALL_PREFIX_BYTES 16UL
#define LARGE_STORAGE_EXTRA 64UL

static const UBYTE fill_values[] = {0x00, 0x01, 0xA5, 0xFF};
static const ULONG large_lengths[] = {
    4096UL,
    65536UL,
    66880UL,
    44UL * 65535UL,
    44UL * 65535UL + 1UL,
    44UL * 65535UL + 2UL,
    44UL * 65535UL + 3UL,
    44UL * 65535UL + 43UL,
    44UL * 65536UL,
    44UL * 65536UL + 1UL,
    44UL * 65536UL + 2UL,
    44UL * 65536UL + 3UL,
    44UL * 65536UL + 43UL
};

struct ProbeCounts {
    ULONG small_copy_cases;
    ULONG small_set_cases;
    ULONG large_copy_cases;
    ULONG large_set_cases;
    ULONG large_copy_bytes;
    ULONG large_set_bytes;
    ULONG failures;
    const char *first_operation;
    const char *first_check;
    ULONG first_length;
    ULONG first_dst_offset;
    ULONG first_src_offset;
    ULONG first_value;
    ULONG first_index;
    UBYTE first_expected;
    UBYTE first_actual;
};

static struct ProbeCounts counts;

static UBYTE guard_pattern(ULONG index, ULONG salt)
{
    return (UBYTE)((index * 31UL + (index >> 2) * 17UL + salt) & 0xFFUL);
}

static UBYTE source_pattern(ULONG index)
{
    return (UBYTE)((index * 37UL + (index >> 3) * 13UL + 0x5BUL) & 0xFFUL);
}

static void record_failure(const char *operation, const char *check,
                           ULONG length, ULONG dst_offset, ULONG src_offset,
                           ULONG value, ULONG index, UBYTE expected,
                           UBYTE actual)
{
    counts.failures++;
    if (counts.failures != 1) return;

    counts.first_operation = operation;
    counts.first_check = check;
    counts.first_length = length;
    counts.first_dst_offset = dst_offset;
    counts.first_src_offset = src_offset;
    counts.first_value = value;
    counts.first_index = index;
    counts.first_expected = expected;
    counts.first_actual = actual;
}

static void check_small_memcpy(ULONG length, ULONG dst_offset,
                               ULONG src_offset)
{
    UBYTE __attribute__((aligned(8))) dst_storage[SMALL_STORAGE_BYTES];
    UBYTE __attribute__((aligned(8))) src_storage[SMALL_STORAGE_BYTES];
    UBYTE __attribute__((aligned(8))) expected[SMALL_STORAGE_BYTES];
    UBYTE __attribute__((aligned(8))) source_before[SMALL_STORAGE_BYTES];
    ULONG index;
    UBYTE *dst = &dst_storage[SMALL_PREFIX_BYTES + dst_offset];
    UBYTE *src = &src_storage[SMALL_PREFIX_BYTES + src_offset];
    void *returned;
    BOOL failed = FALSE;
    const char *failed_check = NULL;
    ULONG failed_index = 0;
    UBYTE failed_expected = 0;
    UBYTE failed_actual = 0;

    for (index = 0; index < SMALL_STORAGE_BYTES; index++) {
        dst_storage[index] = guard_pattern(index, 0x23UL);
        expected[index] = dst_storage[index];
        src_storage[index] = source_pattern(index + src_offset);
        source_before[index] = src_storage[index];
    }
    for (index = 0; index < length; index++) {
        expected[SMALL_PREFIX_BYTES + dst_offset + index] =
            source_before[SMALL_PREFIX_BYTES + src_offset + index];
    }

    returned = call_memcpy(dst, src, (size_t)length);
    if (returned != dst) {
        failed = TRUE;
        failed_check = "return";
    }
    for (index = 0; index < SMALL_STORAGE_BYTES; index++) {
        if (dst_storage[index] != expected[index]) {
            failed = TRUE;
            if (!failed_check) {
                failed_check = "destination";
                failed_index = index;
                failed_expected = expected[index];
                failed_actual = dst_storage[index];
            }
            break;
        }
    }
    for (index = 0; index < SMALL_STORAGE_BYTES; index++) {
        if (src_storage[index] != source_before[index]) {
            failed = TRUE;
            if (!failed_check) {
                failed_check = "source";
                failed_index = index;
                failed_expected = source_before[index];
                failed_actual = src_storage[index];
            }
            break;
        }
    }
    if (failed) {
        record_failure("memcpy-small", failed_check, length, dst_offset,
                       src_offset, 0, failed_index, failed_expected,
                       failed_actual);
    }
}

static void check_small_memset(ULONG length, ULONG dst_offset, UBYTE value)
{
    UBYTE __attribute__((aligned(8))) dst_storage[SMALL_STORAGE_BYTES];
    UBYTE __attribute__((aligned(8))) expected[SMALL_STORAGE_BYTES];
    ULONG index;
    UBYTE *dst = &dst_storage[SMALL_PREFIX_BYTES + dst_offset];
    void *returned;
    BOOL failed = FALSE;
    const char *failed_check = NULL;
    ULONG failed_index = 0;
    UBYTE failed_expected = 0;
    UBYTE failed_actual = 0;

    for (index = 0; index < SMALL_STORAGE_BYTES; index++) {
        dst_storage[index] = guard_pattern(index, 0x91UL);
        expected[index] = dst_storage[index];
    }
    for (index = 0; index < length; index++) {
        expected[SMALL_PREFIX_BYTES + dst_offset + index] = value;
    }

    returned = call_memset(dst, (int)value, (size_t)length);
    if (returned != dst) {
        failed = TRUE;
        failed_check = "return";
    }
    for (index = 0; index < SMALL_STORAGE_BYTES; index++) {
        if (dst_storage[index] != expected[index]) {
            failed = TRUE;
            if (!failed_check) {
                failed_check = "destination";
                failed_index = index;
                failed_expected = expected[index];
                failed_actual = dst_storage[index];
            }
            break;
        }
    }
    if (failed) {
        record_failure("memset-small", failed_check, length, dst_offset,
                       0, value, failed_index, failed_expected,
                       failed_actual);
    }
}

static void check_large_memcpy(ULONG length, ULONG dst_offset,
                               ULONG src_offset)
{
    ULONG storage_bytes = length + LARGE_STORAGE_EXTRA;
    UBYTE *dst_storage = AllocVec(storage_bytes, MEMF_PUBLIC);
    UBYTE *src_storage = AllocVec(storage_bytes, MEMF_PUBLIC);
    UBYTE *dst;
    UBYTE *src;
    ULONG index;
    BOOL failed = FALSE;
    const char *failed_check = NULL;
    ULONG failed_index = 0;
    UBYTE failed_expected = 0;
    UBYTE failed_actual = 0;

    if (!dst_storage || !src_storage) {
        record_failure("memcpy-large", "AllocVec", length, dst_offset,
                       src_offset, 0, 0, 0, 0);
        if (dst_storage) FreeVec(dst_storage);
        if (src_storage) FreeVec(src_storage);
        return;
    }

    dst = &dst_storage[SMALL_PREFIX_BYTES + dst_offset];
    src = &src_storage[SMALL_PREFIX_BYTES + src_offset];
    for (index = 0; index < storage_bytes; index++) {
        dst_storage[index] = guard_pattern(index, 0x39UL);
        src_storage[index] = source_pattern(index);
    }

    if (call_memcpy(dst, src, (size_t)length) != dst) {
        failed = TRUE;
        failed_check = "return";
    }
    for (index = 0; index < storage_bytes; index++) {
        UBYTE expected = guard_pattern(index, 0x39UL);
        if (index >= SMALL_PREFIX_BYTES + dst_offset &&
            index < SMALL_PREFIX_BYTES + dst_offset + length) {
            ULONG source_index = SMALL_PREFIX_BYTES + src_offset +
                index - (SMALL_PREFIX_BYTES + dst_offset);
            expected = source_pattern(source_index);
        }
        if (dst_storage[index] != expected) {
            failed = TRUE;
            if (!failed_check) {
                failed_check = "destination";
                failed_index = index;
                failed_expected = expected;
                failed_actual = dst_storage[index];
            }
            break;
        }
    }
    for (index = 0; index < storage_bytes; index++) {
        UBYTE expected = source_pattern(index);
        if (src_storage[index] != expected) {
            failed = TRUE;
            if (!failed_check) {
                failed_check = "source";
                failed_index = index;
                failed_expected = expected;
                failed_actual = src_storage[index];
            }
            break;
        }
    }
    if (failed) {
        record_failure("memcpy-large", failed_check, length, dst_offset,
                       src_offset, 0, failed_index, failed_expected,
                       failed_actual);
    }

    FreeVec(src_storage);
    FreeVec(dst_storage);
}

static void check_large_memset(ULONG length, ULONG dst_offset, UBYTE value)
{
    ULONG storage_bytes = length + LARGE_STORAGE_EXTRA;
    UBYTE *dst_storage = AllocVec(storage_bytes, MEMF_PUBLIC);
    UBYTE *dst;
    ULONG index;
    BOOL failed = FALSE;
    const char *failed_check = NULL;
    ULONG failed_index = 0;
    UBYTE failed_expected = 0;
    UBYTE failed_actual = 0;

    if (!dst_storage) {
        record_failure("memset-large", "AllocVec", length, dst_offset,
                       0, value, 0, 0, 0);
        return;
    }

    dst = &dst_storage[SMALL_PREFIX_BYTES + dst_offset];
    for (index = 0; index < storage_bytes; index++) {
        dst_storage[index] = guard_pattern(index, 0xC7UL);
    }

    if (call_memset(dst, (int)value, (size_t)length) != dst) {
        failed = TRUE;
        failed_check = "return";
    }
    for (index = 0; index < storage_bytes; index++) {
        UBYTE expected = guard_pattern(index, 0xC7UL);
        if (index >= SMALL_PREFIX_BYTES + dst_offset &&
            index < SMALL_PREFIX_BYTES + dst_offset + length) {
            expected = value;
        }
        if (dst_storage[index] != expected) {
            failed = TRUE;
            if (!failed_check) {
                failed_check = "destination";
                failed_index = index;
                failed_expected = expected;
                failed_actual = dst_storage[index];
            }
            break;
        }
    }
    if (failed) {
        record_failure("memset-large", failed_check, length, dst_offset,
                       0, value, failed_index, failed_expected,
                       failed_actual);
    }

    FreeVec(dst_storage);
}

static ULONG append_text(char *buffer, ULONG offset, const char *text)
{
    while (*text) buffer[offset++] = *text++;
    return offset;
}

static ULONG append_decimal(char *buffer, ULONG offset, ULONG value)
{
    char digits[12];
    ULONG count = 0;

    do {
        digits[count++] = (char)('0' + value % 10UL);
        value /= 10UL;
    } while (value);
    while (count) buffer[offset++] = digits[--count];
    return offset;
}

static ULONG build_report(char *buffer)
{
    ULONG offset = 0;
    offset = append_text(buffer, offset, "small_memcpy_cases=");
    offset = append_decimal(buffer, offset, counts.small_copy_cases);
    offset = append_text(buffer, offset, "\nsmall_memset_cases=");
    offset = append_decimal(buffer, offset, counts.small_set_cases);
    offset = append_text(buffer, offset, "\nlarge_memcpy_cases=");
    offset = append_decimal(buffer, offset, counts.large_copy_cases);
    offset = append_text(buffer, offset, "\nlarge_memset_cases=");
    offset = append_decimal(buffer, offset, counts.large_set_cases);
    offset = append_text(buffer, offset, "\nlarge_memcpy_bytes=");
    offset = append_decimal(buffer, offset, counts.large_copy_bytes);
    offset = append_text(buffer, offset, "\nlarge_memset_bytes=");
    offset = append_decimal(buffer, offset, counts.large_set_bytes);
    offset = append_text(buffer, offset, "\nfailures=");
    offset = append_decimal(buffer, offset, counts.failures);
    offset = append_text(buffer, offset, "\n");

    if (counts.failures) {
        offset = append_text(buffer, offset, "first_failure=");
        offset = append_text(buffer, offset, counts.first_operation);
        offset = append_text(buffer, offset, " check=");
        offset = append_text(buffer, offset, counts.first_check);
        offset = append_text(buffer, offset, " len=");
        offset = append_decimal(buffer, offset, counts.first_length);
        offset = append_text(buffer, offset, " dst_offset=");
        offset = append_decimal(buffer, offset, counts.first_dst_offset);
        offset = append_text(buffer, offset, " src_offset=");
        offset = append_decimal(buffer, offset, counts.first_src_offset);
        offset = append_text(buffer, offset, " value=");
        offset = append_decimal(buffer, offset, counts.first_value);
        offset = append_text(buffer, offset, " index=");
        offset = append_decimal(buffer, offset, counts.first_index);
        offset = append_text(buffer, offset, " expected=");
        offset = append_decimal(buffer, offset, counts.first_expected);
        offset = append_text(buffer, offset, " actual=");
        offset = append_decimal(buffer, offset, counts.first_actual);
        offset = append_text(buffer, offset, "\nMEMORY-FUNCTIONS-FAIL\n");
    } else {
        offset = append_text(buffer, offset, "MEMORY-FUNCTIONS-PASS\n");
    }
    return offset;
}

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    ULONG length;
    ULONG dst_offset;
    ULONG src_offset;
    ULONG value_index;
    ULONG large_index;
    char report[512];
    static const char complete_text[] = "MEMORY-FUNCTIONS-RECORDED\n";
    ULONG report_length;
    BPTR result;
    BPTR marker;
    BOOL recorded;

    process->pr_WindowPtr = (APTR)-1;

    for (length = 0; length <= SMALL_MAX_LENGTH; length++) {
        for (dst_offset = 0; dst_offset < 8; dst_offset++) {
            for (src_offset = 0; src_offset < 8; src_offset++) {
                check_small_memcpy(length, dst_offset, src_offset);
                counts.small_copy_cases++;
            }
            for (value_index = 0;
                 value_index < sizeof(fill_values) / sizeof(fill_values[0]);
                 value_index++) {
                check_small_memset(length, dst_offset,
                                   fill_values[value_index]);
                counts.small_set_cases++;
            }
        }
    }

    for (large_index = 0;
         large_index < sizeof(large_lengths) / sizeof(large_lengths[0]);
         large_index++) {
        ULONG large_length = large_lengths[large_index];
        UBYTE fill = fill_values[large_index %
            (sizeof(fill_values) / sizeof(fill_values[0]))];
        ULONG copy_src_offset = (large_index * 3UL) % 8UL;
        ULONG copy_dst_offset = (large_index * 5UL + 1UL) % 8UL;
        ULONG set_dst_offset = (large_index * 7UL + 2UL) % 8UL;

        check_large_memcpy(large_length, copy_dst_offset, copy_src_offset);
        counts.large_copy_cases++;
        counts.large_copy_bytes += large_length;
        check_large_memset(large_length, set_dst_offset, fill);
        counts.large_set_cases++;
        counts.large_set_bytes += large_length;
    }

    report_length = build_report(report);
    result = Open((STRPTR)"SYS:Results/memory-functions.txt", MODE_NEWFILE);
    if (!result) {
        Write(Output(), (APTR)"MEMORY-FUNCTIONS-FAIL result-open\n", 34);
        process->pr_WindowPtr = old_window;
        return 20;
    }
    recorded = Write(result, (APTR)report, (LONG)report_length) ==
        (LONG)report_length;
    if (!Close(result)) recorded = FALSE;
    if (recorded) {
        marker = Open((STRPTR)"SYS:Results/memory-functions.done",
                      MODE_NEWFILE);
        if (!marker) {
            recorded = FALSE;
        } else {
            recorded = Write(marker, (APTR)complete_text,
                             (LONG)(sizeof(complete_text) - 1)) ==
                (LONG)(sizeof(complete_text) - 1);
            if (!Close(marker)) recorded = FALSE;
        }
    }
    process->pr_WindowPtr = old_window;
    return recorded && !counts.failures ? 0 : 20;
}
