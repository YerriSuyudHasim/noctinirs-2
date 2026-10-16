#include "nn2_test.h"

int nn2_t_run = 0, nn2_t_fail = 0;

int main(void)
{
    test_ring_suite();
    test_sequencer_suite();
    printf("TESTS RUN: %d, FAILED: %d\n", nn2_t_run, nn2_t_fail);
    return (nn2_t_run > 0 && nn2_t_fail == 0) ? 0 : 1;
}
