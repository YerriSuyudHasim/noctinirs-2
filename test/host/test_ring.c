#include "nn2_test.h"
#include "nn2_core_ring.h"

static void ring_init_empty(void)
{
    nn2_core_ring_t r;
    nn2_core_sample_t s;
    TEST_ASSERT_TRUE(nn2_core_ring_init(&r) == NN2_OK);
    TEST_ASSERT_TRUE(nn2_core_ring_count(&r) == 0u);
    TEST_ASSERT_TRUE(nn2_core_ring_pop(&r, &s) == NN2_ERR_EMPTY);
}

static void ring_null_args(void)
{
    nn2_core_ring_t r;
    nn2_core_sample_t s = {0};
    TEST_ASSERT_TRUE(nn2_core_ring_init(NULL) == NN2_ERR_INVALID_ARG);
    (void)nn2_core_ring_init(&r);
    TEST_ASSERT_TRUE(nn2_core_ring_push(NULL, &s) == NN2_ERR_INVALID_ARG);
    TEST_ASSERT_TRUE(nn2_core_ring_push(&r, NULL) == NN2_ERR_INVALID_ARG);
    TEST_ASSERT_TRUE(nn2_core_ring_pop(&r, NULL) == NN2_ERR_INVALID_ARG);
}

static void ring_fifo_and_full(void)
{
    nn2_core_ring_t r;
    nn2_core_sample_t s = {0};
    (void)nn2_core_ring_init(&r);
    for (uint32_t i = 0; i < NN2_CORE_RING_CAPACITY; i++) {
        s.seq = i;
        TEST_ASSERT_TRUE(nn2_core_ring_push(&r, &s) == NN2_OK);
    }
    TEST_ASSERT_TRUE(nn2_core_ring_is_full(&r));
    s.seq = 999u;
    TEST_ASSERT_TRUE(nn2_core_ring_push(&r, &s) == NN2_ERR_FULL);
    TEST_ASSERT_TRUE(r.dropped == 1u);
    for (uint32_t i = 0; i < NN2_CORE_RING_CAPACITY; i++) {
        TEST_ASSERT_TRUE(nn2_core_ring_pop(&r, &s) == NN2_OK);
        TEST_ASSERT_TRUE(s.seq == i);
    }
    TEST_ASSERT_TRUE(nn2_core_ring_count(&r) == 0u);
}

static void ring_wraparound(void)
{
    nn2_core_ring_t r;
    nn2_core_sample_t s = {0};
    (void)nn2_core_ring_init(&r);
    for (uint32_t i = 0; i < 3u * NN2_CORE_RING_CAPACITY + 5u; i++) {
        s.seq = i;
        TEST_ASSERT_TRUE(nn2_core_ring_push(&r, &s) == NN2_OK);
        TEST_ASSERT_TRUE(nn2_core_ring_pop(&r, &s) == NN2_OK);
        TEST_ASSERT_TRUE(s.seq == i);
    }
    TEST_ASSERT_TRUE(r.dropped == 0u);
}

static void status_strings(void)
{
    TEST_ASSERT_TRUE(nn2_core_status_str(NN2_OK)[0] == 'O');
    TEST_ASSERT_TRUE(nn2_core_status_str((nn2_status_t)1234) != NULL);
}

void test_ring_suite(void)
{
    RUN_TEST(ring_init_empty);
    RUN_TEST(ring_null_args);
    RUN_TEST(ring_fifo_and_full);
    RUN_TEST(ring_wraparound);
    RUN_TEST(status_strings);
}
