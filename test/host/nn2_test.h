/* Tiny self-contained assert harness (no Unity; licence decision Q-7 pending). */
#ifndef NN2_TEST_H
#define NN2_TEST_H
#include <stdio.h>

extern int nn2_t_run, nn2_t_fail;

#define NN2_CHECK(cond) do { \
    if (!(cond)) { nn2_t_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } \
} while (0)

#define NN2_RUN(fn) do { int f0 = nn2_t_fail; nn2_t_run++; fn(); \
    printf("%s %s\n", (nn2_t_fail == f0) ? "PASS" : "FAIL", #fn); } while (0)

void test_ring_suite(void);
void test_sequencer_suite(void);
#endif
