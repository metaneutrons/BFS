/* SPDX-License-Identifier: MPL-2.0 */
/* Isolated AmigaOS oracle and timing probe for bfs_crc32_sparse. */

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

uint32_t bfs_crc32(uint32_t, const void *, size_t);
uint32_t bfs_crc32_sparse(uint32_t, const void *, uint32_t);
uint32_t bfs_crc32_abi_clobber_d2(uint32_t, const void *, size_t);
uint32_t bfs_crc32_abi_clobber_a2(uint32_t, const void *, size_t);
extern ULONG bfs_crc32_abi_probe(CrcFunction, uint32_t, const void *, size_t,
                                 uint32_t *);

struct Device *TimerBase;
/* The ABI witness uses size_t. Reject an incompatible target typedef rather
 * than relying only on both argument types having the same bit width. */
typedef char sparse_length_matches_size_t[
    __builtin_types_compatible_p(size_t, uint32_t) ? 1 : -1];
static CrcFunction volatile call_baseline = bfs_crc32;
static CrcFunction volatile call_sparse = bfs_crc32_sparse;
static volatile uint32_t timing_sink;

#define PREFIX 8UL
#define DATA_MAX 4096UL
#define STORAGE (PREFIX + 8UL + DATA_MAX)
#define SAMPLE_COUNT 6UL
#define TIMING_COUNT 5UL
#define REPORT_CAPACITY 8192UL

enum { ZERO, DENSE, BLOCK256, DIRKEY264, MIX128_384 };
static const char *pattern_names[] = {
    "zero", "dense", "block32_224", "dirkey_like_264", "mixed128_384"
};
static const uint32_t seeds[] = {0x00000000u, 0xFFFFFFFFu, 0x12345678u};
static const ULONG short_lengths[] = {
    0, 1, 15, 16, 17, 31, 32, 63, 64, 65, 66, 127, 128, 129
};
static const ULONG perturb_positions[] = {0, 63, 64, 65, 255, 256, 2047, 4095};

typedef struct {
    ULONG cases, baseline_calls, sparse_calls, abi_checks;
    ULONG oracle_errors, abi_errors, control_cases, control_passes;
    ULONG control_errors, timing_cases, timing_samples;
    ULONG timing_calls[2], timing_errors, order_baseline_first;
    ULONG order_sparse_first, errors, clock_hz;
    uint32_t digest;
    const char *first_kind, *first_fixture;
    ULONG first_length, first_offset, first_mask;
    uint32_t first_seed, first_expected, first_actual;
} Counts;

typedef struct {
    const char *name;
    ULONG length, repeats;
    int pattern;
} TimingCase;

static Counts counts = {.digest = 2166136261u};
static UBYTE storage[STORAGE];
static uint64_t baseline_ticks[TIMING_COUNT][SAMPLE_COUNT];
static uint64_t sparse_ticks[TIMING_COUNT][SAMPLE_COUNT];
static const TimingCase timing_cases[TIMING_COUNT] = {
    {"dense4096", 4096, 64, DENSE},
    {"zero4096", 4096, 64, ZERO},
    {"sparse4096-dirkey264", 4096, 64, DIRKEY264},
    {"short64-dense", 64, 512, DENSE},
    {"mixed128-384", 512, 64, MIX128_384}
};

static uint32_t reference_crc32(uint32_t initial, const UBYTE *data,
                                size_t length)
{
    uint32_t crc = ~initial;
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static void digest_byte(UBYTE value)
{
    counts.digest = (counts.digest ^ value) * 16777619u;
}

static void digest_u32(uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; i++) digest_byte((UBYTE)(value >> (i * 8)));
}

static UBYTE dense_byte(ULONG index)
{
    return (UBYTE)(((index * 73UL + (index >> 4) * 19UL + 0x5BUL) & 0xFFUL) | 1UL);
}

static void fill_pattern(UBYTE *data, ULONG length, int pattern)
{
    ULONG i;
    for (i = 0; i < length; i++) {
        switch (pattern) {
        case ZERO: data[i] = 0; break;
        case DENSE: data[i] = dense_byte(i); break;
        case BLOCK256: data[i] = (i % 256UL < 32UL) ? dense_byte(i) : 0; break;
        case DIRKEY264: data[i] = (i % 264UL < 13UL) ? dense_byte(i) : 0; break;
        default: data[i] = (i % 512UL < 128UL) ? dense_byte(i) : 0; break;
        }
    }
}

static void note_error(const char *kind, const char *fixture, ULONG length,
                       ULONG offset, uint32_t seed, uint32_t expected,
                       uint32_t actual, ULONG mask)
{
    counts.errors++;
    if (counts.first_kind) return;
    counts.first_kind = kind; counts.first_fixture = fixture;
    counts.first_length = length; counts.first_offset = offset;
    counts.first_seed = seed; counts.first_expected = expected;
    counts.first_actual = actual; counts.first_mask = mask;
}

static void check_one(CrcFunction function, ULONG which, const char *fixture,
                      uint32_t seed, const UBYTE *data, ULONG length,
                      ULONG offset, uint32_t expected)
{
    uint32_t actual = 0;
    ULONG mask = bfs_crc32_abi_probe(function, seed, data, (size_t)length,
                                     &actual);
    counts.abi_checks++;
    if (which) counts.baseline_calls++; else counts.sparse_calls++;
    if (mask) {
        counts.abi_errors++;
        note_error("abi", fixture, length, offset, seed, 0, 0, mask);
    }
    if (actual != expected) {
        counts.oracle_errors++;
        note_error("oracle", fixture, length, offset, seed, expected, actual, 0);
    }
}

static void check(const char *fixture, const UBYTE *data, ULONG length,
                  ULONG offset, uint32_t seed)
{
    uint32_t expected = reference_crc32(seed, data, (size_t)length);
    CrcFunction baseline = call_baseline, sparse = call_sparse;
    ULONG i;
    counts.cases++;
    digest_u32(length); digest_u32(offset); digest_u32(seed);
    for (i = 0; i < length; i++) digest_byte(data[i]);
    digest_u32(expected);
    check_one(baseline, 1, fixture, seed, data, length, offset, expected);
    check_one(sparse, 0, fixture, seed, data, length, offset, expected);
}

static void check_vectors(void)
{
    static const int small_patterns[] = {ZERO, DENSE, BLOCK256};
    static const int large_patterns[] = {ZERO, DENSE, BLOCK256, DIRKEY264};
    ULONG p, li, offset, si;
    for (p = 0; p < sizeof(small_patterns) / sizeof(small_patterns[0]); p++)
        for (li = 0; li < sizeof(short_lengths) / sizeof(short_lengths[0]); li++)
            for (offset = 0; offset < 8; offset++) {
                UBYTE *data = storage + PREFIX + offset;
                fill_pattern(data, short_lengths[li], small_patterns[p]);
                for (si = 0; si < 3; si++)
                    check(pattern_names[small_patterns[p]], data,
                          short_lengths[li], offset, seeds[si]);
            }

    /* Runs of 63, 64, and 65 zero bytes are clipped by actual input length. */
    for (offset = 0; offset < 8; offset++) {
        ULONG run;
        UBYTE *data = storage + PREFIX + offset;
        for (run = 63; run <= 65; run++) {
            fill_pattern(data, 67, ZERO);
            data[0] = 0xA5; data[run + 1] = 0x5A;
            for (si = 0; si < 3; si++) {
                check("zero_run_boundary", data, run + 1, offset, seeds[si]);
                check("zero_run_boundary", data, run + 2, offset, seeds[si]);
            }
        }
    }

    for (p = 0; p < sizeof(large_patterns) / sizeof(large_patterns[0]); p++)
        for (offset = 0; offset < 8; offset++) {
            UBYTE *data = storage + PREFIX + offset;
            fill_pattern(data, DATA_MAX, large_patterns[p]);
            for (si = 0; si < 3; si++)
                check(pattern_names[large_patterns[p]], data, DATA_MAX,
                      offset, seeds[si]);
        }

    for (offset = 0; offset < 8; offset++) {
        UBYTE *data = storage + PREFIX + offset;
        fill_pattern(data, 512, MIX128_384);
        for (si = 0; si < 3; si++)
            check("mixed128_384_control", data, 512, offset, seeds[si]);
    }

    for (p = 0; p < sizeof(perturb_positions) / sizeof(perturb_positions[0]); p++) {
        UBYTE *data = storage + PREFIX;
        fill_pattern(data, DATA_MAX, ZERO);
        data[perturb_positions[p]] = 0xA5;
        for (si = 0; si < 3; si++)
            check("single_byte_perturbation", data, DATA_MAX, 0, seeds[si]);
    }

    {
        static const UBYTE known[] = {'1','2','3','4','5','6','7','8','9'};
        uint32_t oracle = reference_crc32(0, known, sizeof(known));
        if (oracle != 0xCBF43926u)
            note_error("oracle_selfcheck", "123456789", 9, 0, 0,
                       0xCBF43926u, oracle, 0);
        check("known_vector", known, sizeof(known), 0, 0);
    }
}

static void check_abi_controls(void)
{
    static CrcFunction controls[] = {
        bfs_crc32_abi_clobber_d2, bfs_crc32_abi_clobber_a2
    };
    static const ULONG expected_masks[] = {0x00000004UL, 0x00000100UL};
    uint32_t seed = 0x12345678u, actual;
    ULONG i, mask;
    for (i = 0; i < 2; i++) {
        actual = 0;
        mask = bfs_crc32_abi_probe(controls[i], seed, NULL, 0, &actual);
        counts.control_cases++;
        if (mask == expected_masks[i] && actual == seed) counts.control_passes++;
        else {
            counts.control_errors++;
            note_error("abi_negative_control", i ? "clobber_a2" : "clobber_d2",
                       0, 0, seed, mask != expected_masks[i] ? expected_masks[i] : seed,
                       mask != expected_masks[i] ? mask : actual, mask);
        }
    }
}

static BOOL open_eclock(struct MsgPort **port_out,
                        struct timerequest **request_out)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request;
    struct EClockVal initial = {0};
    *port_out = NULL; *request_out = NULL;
    if (!port) return FALSE;
    request = (struct timerequest *)CreateIORequest(port, (ULONG)sizeof(*request));
    if (!request) { DeleteMsgPort(port); return FALSE; }
    if (OpenDevice(TIMERNAME, UNIT_ECLOCK, (struct IORequest *)request, 0)) {
        DeleteIORequest((struct IORequest *)request); DeleteMsgPort(port);
        return FALSE;
    }
    TimerBase = (struct Device *)request->tr_node.io_Device;
    counts.clock_hz = ReadEClock(&initial);
    if (!counts.clock_hz) {
        CloseDevice((struct IORequest *)request);
        DeleteIORequest((struct IORequest *)request); DeleteMsgPort(port);
        TimerBase = NULL; return FALSE;
    }
    *port_out = port; *request_out = request; return TRUE;
}

static void close_eclock(struct MsgPort *port, struct timerequest *request)
{
    CloseDevice((struct IORequest *)request);
    DeleteIORequest((struct IORequest *)request); DeleteMsgPort(port);
    TimerBase = NULL;
}

static uint64_t eclock_value(const struct EClockVal *value)
{
    return ((uint64_t)value->ev_hi << 32) | value->ev_lo;
}

static uint32_t time_batch(CrcFunction volatile *slot, uint32_t seed,
                           const UBYTE *data, ULONG length, ULONG repeats,
                           ULONG which, uint64_t *ticks_out)
{
    struct EClockVal start = {0}, end = {0};
    uint32_t sum = 0x6D2B79F5u;
    ULONG i;
    ReadEClock(&start);
    for (i = 0; i < repeats; i++) {
        CrcFunction function = *slot;
        uint32_t value = function(seed, data, (size_t)length);
        sum = (sum << 5) ^ (sum >> 27) ^ value ^ (i * 0x9E3779B9u);
    }
    ReadEClock(&end);
    *ticks_out = eclock_value(&end) - eclock_value(&start);
    timing_sink = sum;
    counts.timing_calls[which] += repeats;
    return sum;
}

static void run_timing(void)
{
    ULONG c, sample;
    const uint32_t seed = 0x12345678u;
    for (c = 0; c < TIMING_COUNT; c++) {
        const TimingCase *test = &timing_cases[c];
        const UBYTE *data = storage + PREFIX + 3;
        fill_pattern((UBYTE *)data, test->length, test->pattern);
        counts.timing_cases++;
        for (sample = 0; sample < SAMPLE_COUNT; sample++) {
            BOOL baseline_first = (sample & 1UL) == 0;
            uint32_t baseline_sum, sparse_sum;
            if (baseline_first) {
                baseline_sum = time_batch(&call_baseline, seed,
                    data, test->length, test->repeats, 1,
                    &baseline_ticks[c][sample]);
                sparse_sum = time_batch(&call_sparse, seed,
                    data, test->length, test->repeats, 0,
                    &sparse_ticks[c][sample]);
                counts.order_baseline_first++;
            } else {
                sparse_sum = time_batch(&call_sparse, seed,
                    data, test->length, test->repeats, 0,
                    &sparse_ticks[c][sample]);
                baseline_sum = time_batch(&call_baseline, seed,
                    data, test->length, test->repeats, 1,
                    &baseline_ticks[c][sample]);
                counts.order_sparse_first++;
            }
            if (baseline_sum != sparse_sum) {
                counts.timing_errors++;
                note_error("timing_checksum", test->name, test->length, 3,
                           seed, baseline_sum, sparse_sum, 0);
            }
            counts.timing_samples++;
        }
    }
}

static ULONG append_text(char *b, ULONG n, const char *s)
{
    while (*s) b[n++] = *s++;
    return n;
}

static ULONG append_u64(char *b, ULONG n, uint64_t v)
{
    char digits[21]; ULONG used = 0;
    do { digits[used++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (used) b[n++] = digits[--used];
    return n;
}

static ULONG append_hex32(char *b, ULONG n, uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF"; int shift;
    for (shift = 28; shift >= 0; shift -= 4) b[n++] = hex[(v >> shift) & 15u];
    return n;
}

static ULONG append_timing_row(char *b, ULONG n, ULONG c, ULONG sample)
{
    const TimingCase *test = &timing_cases[c];
    n = append_text(b, n, "TIMING\t"); n = append_text(b, n, test->name);
    n = append_text(b, n, "\t"); n = append_u64(b, n, test->length);
    n = append_text(b, n, "\t"); n = append_u64(b, n, test->repeats);
    n = append_text(b, n, "\t"); n = append_u64(b, n, sample);
    n = append_text(b, n, (sample & 1UL) ? "\tsparse-first\t" : "\tbaseline-first\t");
    n = append_u64(b, n, baseline_ticks[c][sample]); n = append_text(b, n, "\t");
    n = append_u64(b, n, sparse_ticks[c][sample]);
    return append_text(b, n, "\n");
}

static ULONG build_report(char *b, BOOL timer_available)
{
    ULONG n = 0, c, sample;
#define PUT(s) do { n = append_text(b, n, (s)); } while (0)
#define NUM(v) do { n = append_u64(b, n, (uint64_t)(v)); } while (0)
#define COUNT(name, value) do { PUT("COUNT\t"); PUT(name); PUT("\t"); NUM(value); PUT("\n"); } while (0)
    PUT("SPARSE_CRC_PROBE\t1\n");
    PUT("META\toracle\tindependent-bitwise-reflected-IEEE\n");
    PUT("META\tapi\tbfs_crc32_sparse; m68k size_t is 32-bit\n");
    PUT("FIXTURE\tname\tdescription\n");
    PUT("FIXTURE\tsmall\tzero,dense,32-nonzero+224-zero per 256; lengths 0,1,15,16,17,31,32,63,64,65,66,127,128,129; offsets 0..7\n");
    PUT("FIXTURE\tzero_run_boundary\tprefix byte; 63,64,65 supplied zero bytes; following byte included separately\n");
    PUT("FIXTURE\tlarge\t4096-byte zero,dense,block256,dirkey264; seeds 00000000,FFFFFFFF,12345678; offsets 0..7\n");
    PUT("FIXTURE\tdirkey_like_264\tsynthetic packed 264-byte key: parent4,hash4,nameLen1,four-char name; 251 zero tail bytes\n");
    PUT("FIXTURE\tsingle_byte_perturbation\t4096 zero bytes with one A5 at offsets 0,63,64,65,255,256,2047,4095\n");
    PUT("FIXTURE\tmixed128_384\tdensity control; 128 nonzero and 384 zero bytes per 512-byte group; not an inode fixture\n");
    COUNT("vector_cases", counts.cases); COUNT("baseline_calls", counts.baseline_calls);
    COUNT("sparse_calls", counts.sparse_calls); COUNT("abi_positive_checks", counts.abi_checks);
    COUNT("abi_negative_control_cases", counts.control_cases);
    COUNT("abi_negative_control_passes", counts.control_passes);
    COUNT("abi_negative_control_errors", counts.control_errors);
    COUNT("oracle_errors", counts.oracle_errors); COUNT("abi_errors", counts.abi_errors);
    COUNT("timing_errors", counts.timing_errors); COUNT("errors", counts.errors);
    COUNT("timer_available", timer_available ? 1 : 0);
    PUT("CLOCK_HZ\t"); NUM(counts.clock_hz); PUT("\n");
    COUNT("oracle_digest_fnv1a32", counts.digest); COUNT("timing_cases", counts.timing_cases);
    COUNT("timing_samples", counts.timing_samples);
    COUNT("baseline_timing_calls", counts.timing_calls[1]);
    COUNT("sparse_timing_calls", counts.timing_calls[0]);
    COUNT("timing_order_baseline_first", counts.order_baseline_first);
    COUNT("timing_order_sparse_first", counts.order_sparse_first);
    PUT("TIMING\tcase\tlength\trepeats\tsample\torder\tbaseline_ticks\tsparse_ticks\n");
    if (timer_available)
        for (c = 0; c < TIMING_COUNT; c++)
            for (sample = 0; sample < SAMPLE_COUNT; sample++)
                n = append_timing_row(b, n, c, sample);
    if (counts.first_kind) {
        PUT("FIRST_ERROR\t"); PUT(counts.first_kind); PUT("\t");
        PUT(counts.first_fixture); PUT("\t"); NUM(counts.first_length);
        PUT("\t"); NUM(counts.first_offset); PUT("\t");
        n = append_hex32(b, n, counts.first_seed); PUT("\t");
        n = append_hex32(b, n, counts.first_expected); PUT("\t");
        n = append_hex32(b, n, counts.first_actual); PUT("\t");
        n = append_hex32(b, n, counts.first_mask); PUT("\n");
    }
    PUT(counts.errors == 0 && timer_available ? "STATUS\tPASS\n" : "STATUS\tFAIL\n");
#undef COUNT
#undef NUM
#undef PUT
    return n;
}

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    struct MsgPort *port = NULL;
    struct timerequest *request = NULL;
    char report[REPORT_CAPACITY];
    ULONG report_length;
    BPTR result, marker;
    BOOL timer_available, recorded, success;
    static const char complete[] = "SPARSE_CRC_PROBE_COMPLETE\n";
    process->pr_WindowPtr = (APTR)-1;
    check_abi_controls(); check_vectors();
    timer_available = open_eclock(&port, &request);
    if (timer_available) { run_timing(); close_eclock(port, request); }
    else note_error("timer", "EClock", 0, 0, 0, 0, 0, 0);
    report_length = build_report(report, timer_available);
    result = Open((STRPTR)"SYS:Results/crc32-sparse-probe.tsv", MODE_NEWFILE);
    if (!result) { process->pr_WindowPtr = old_window; return 20; }
    recorded = Write(result, (APTR)report, (LONG)report_length) == (LONG)report_length;
    if (!Close(result)) recorded = FALSE;
    success = counts.errors == 0 && timer_available;
    if (recorded) {
        marker = Open((STRPTR)"SYS:Results/crc32-sparse-probe.done", MODE_NEWFILE);
        if (!marker) recorded = FALSE;
        else {
            recorded = Write(marker, (APTR)complete, (LONG)(sizeof(complete) - 1)) ==
                       (LONG)(sizeof(complete) - 1);
            if (!Close(marker)) recorded = FALSE;
        }
    }
    process->pr_WindowPtr = old_window;
    return recorded && success ? 0 : 20;
}
