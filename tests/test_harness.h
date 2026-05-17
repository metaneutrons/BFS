/*
 * BFS — Minimal test harness (no external dependencies)
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_pass_count;
static int test_fail_count;
static const char *test_current_name;

#define TEST_BEGIN(name) \
    do { test_current_name = (name); } while(0)

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %lld, expected %lld\n", \
                __FILE__, __LINE__, #a, _a, _b); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_MEM_EQ(a, b, n) do { \
    if (memcmp((a), (b), (n)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: memcmp(%s, %s, %d)\n", \
                __FILE__, __LINE__, #a, #b, (int)(n)); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    test_current_name = #fn; \
    printf("  %-50s ", #fn); \
    fn(); \
    if (test_fail_count == _prev_fails) { \
        printf("OK\n"); \
        test_pass_count++; \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

#define TEST_SUITE_BEGIN(name) \
    int main(void) { \
        int _prev_fails; \
        printf("=== %s ===\n", (name)); \
        test_pass_count = 0; \
        test_fail_count = 0;

#define TEST_RUN(fn) \
        _prev_fails = test_fail_count; \
        RUN_TEST(fn);

#define TEST_SUITE_END() \
        printf("\n%d passed, %d failed\n", test_pass_count, test_fail_count); \
        return test_fail_count > 0 ? 1 : 0; \
    }

#endif /* TEST_HARNESS_H */
