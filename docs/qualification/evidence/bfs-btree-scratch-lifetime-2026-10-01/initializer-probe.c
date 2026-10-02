/* Private work-removal probe: consume the actual core helpers, not copies. */
#ifndef BFS_SCRATCH_CORE_SOURCE
#define BFS_SCRATCH_CORE_SOURCE "../src/core/btree.c"
#endif
#include BFS_SCRATCH_CORE_SOURCE
#include <stdio.h>

static int probe_journal(void)
{
    btree_mutation_t journal;
    unsigned char expected[sizeof(journal.new_blocks)];
    unsigned char expected_txns[sizeof(journal.retired_txns)];
    memset(&journal, 0xa5, sizeof(journal));
    memset(expected, 0xa5, sizeof(expected));
    memset(expected_txns, 0xa5, sizeof(expected_txns));
    mutation_init(&journal);
    if (journal.new_count != 0 || journal.retired_count != 0 ||
        memcmp(journal.new_blocks, expected, sizeof(journal.new_blocks)) != 0 ||
        memcmp(journal.retired_blocks, expected, sizeof(journal.retired_blocks)) != 0 ||
        memcmp(journal.retired_txns, expected_txns, sizeof(journal.retired_txns)) != 0) {
        fprintf(stderr, "FAIL: mutation initializer changed unused journal arrays or omitted a count\n");
        return 1;
    }
    printf("PASS: %zu-byte host journal initializes both counts, preserves all unused array bytes\n", sizeof(journal));
    return 0;
}

static int probe_bounds(void)
{
    node_bounds_t bounds;
    unsigned char expected[BFS_MAX_KEY_SIZE];
    memset(&bounds, 0x5a, sizeof(bounds));
    memset(expected, 0x5a, sizeof(expected));
    /* Assign valid boolean representations before testing the real reset. */
    bounds.have_lower = true;
    bounds.have_upper = true;
    node_bounds_reset(&bounds);
    if (bounds.have_lower || bounds.have_upper ||
        memcmp(bounds.lower, expected, sizeof(bounds.lower)) != 0 ||
        memcmp(bounds.upper, expected, sizeof(bounds.upper)) != 0) {
        fprintf(stderr, "FAIL: bounds reset changed unused key arrays or omitted a flag\n");
        return 1;
    }
    printf("PASS: %zu-byte host bounds reset clears both flags, preserves both unused key arrays\n", sizeof(bounds));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "journal") == 0) return probe_journal();
    if (argc == 2 && strcmp(argv[1], "bounds") == 0) return probe_bounds();
    if (argc != 1) return 2;
    return probe_journal() || probe_bounds();
}
