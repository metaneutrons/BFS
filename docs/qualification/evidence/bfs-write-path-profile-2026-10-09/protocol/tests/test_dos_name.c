/* SPDX-License-Identifier: MPL-2.0 */
#include "test_harness.h"
#include "bfs_dos_name.h"

static void test_roundtrip(void)
{
    unsigned char encoded[10] = {0};
    const unsigned char text[] = "BFS:";
    TEST_ASSERT(bfs_dos_name_encode(encoded, sizeof(encoded), text, 4));
    TEST_ASSERT_EQ(bfs_dos_name_length(encoded, sizeof(encoded)), 4);
    TEST_ASSERT_MEM_EQ(bfs_dos_name_text(encoded), text, 4);
#ifdef BFS_AROS
    TEST_ASSERT_EQ(encoded[0], 'B');
    TEST_ASSERT_EQ(encoded[4], 0);
#else
    TEST_ASSERT_EQ(encoded[0], 4);
    TEST_ASSERT_EQ(encoded[5], 0);
#endif
}

static void test_bounds(void)
{
    unsigned char encoded[5] = {0};
    const unsigned char text[] = "ABCDE";
    TEST_ASSERT(!bfs_dos_name_encode(encoded, sizeof(encoded), text, 5));
    TEST_ASSERT_EQ(bfs_dos_name_length(NULL, 7), 7);
#ifdef BFS_AROS
    TEST_ASSERT_EQ(bfs_dos_name_length(text, 3), 3);
#endif
}

TEST_SUITE_BEGIN("DOS name ABI")
    TEST_RUN(test_roundtrip);
    TEST_RUN(test_bounds);
TEST_SUITE_END()
