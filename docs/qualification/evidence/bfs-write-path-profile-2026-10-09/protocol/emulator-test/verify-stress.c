/*
 * verify-stress — Host tool to verify BFS stress test results
 *
 * Reads test-report.txt, counts PASS/FAIL lines, exits 0 if all PASS.
 * Usage: verify-stress <test-report.txt>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *f;
    char line[512];
    int pass = 0, fail = 0;

    if (argc < 2) {
        fprintf(stderr, "Usage: verify-stress <test-report.txt>\n");
        return 1;
    }

    f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open %s\n", argv[1]);
        return 1;
    }

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TEST ", 5) == 0) {
            if (strstr(line, " PASS"))
                pass++;
            else if (strstr(line, " FAIL"))
                fail++;
        }
    }
    fclose(f);

    printf("Results: %d PASS, %d FAIL, %d total\n", pass, fail, pass + fail);

    if (fail > 0) {
        printf("FAILED: %d tests did not pass\n", fail);
        return 1;
    }
    if (pass == 0) {
        printf("FAILED: No test results found\n");
        return 1;
    }

    printf("OK: All %d tests passed\n", pass);
    return 0;
}
