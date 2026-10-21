#include "nn2_test.h"
void setUp(void) {}
void tearDown(void) {}
int main(void)
{
    UNITY_BEGIN();
    test_ring_suite();
    test_sequencer_suite();
    test_contract_suite();
    int result = UNITY_END();
    return (Unity.NumberOfTests > 0u && Unity.TestIgnores == 0u && result == 0) ? 0 : 1;
}
