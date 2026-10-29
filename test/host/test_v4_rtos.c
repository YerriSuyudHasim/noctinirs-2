#include "nn2_test.h"
#include "nocti_v4_backlog.h"

static void backlog_boot82_counts_three_and_reads_newest(void)
{
    /* Four DRDY events were queued and the newest is still this conversion.
     * The three older conversions were overwritten. That is boot82's depth,
     * without the old hard fault at a remainder of 2. */
    const nocti_v4_backlog_event_t events[] = {
        {1u, 375u}, {2u, 250u}, {3u, 125u}, {4u, 20u},
    };
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 4u);
    TEST_ASSERT_EQUAL_UINT32(1u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(3u, result.missed);
    TEST_ASSERT_EQUAL_UINT32(3u, result.missed_backlog);
    TEST_ASSERT_EQUAL_UINT32(3u, result.catchup);
    TEST_ASSERT_EQUAL_UINT32(0u, result.sequence_gap);
    TEST_ASSERT_EQUAL_UINT32(4u, result.last_sequence);
    TEST_ASSERT_EQUAL_UINT8(0u, result.faulted);
}

static void backlog_one_queued_successor_is_not_a_fault(void)
{
    const nocti_v4_backlog_event_t events[] = {{8u, 40u}, {9u, 10u}};
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 2u);
    TEST_ASSERT_EQUAL_UINT32(1u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(1u, result.catchup);
    TEST_ASSERT_EQUAL_UINT8(0u, result.faulted);
}

static void backlog_depth_16_faults_and_counts_every_pending_event(void)
{
    nocti_v4_backlog_event_t events[17];
    uint32_t index;
    for (index = 0u; index < 17u; ++index) {
        events[index].sequence = index + 1u;
        events[index].age_us = (16u - index) * 125u;
    }
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 17u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.faulted);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(17u, result.missed_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.catchup);
}

static void backlog_stale_newest_faults_without_a_fake_read(void)
{
    const nocti_v4_backlog_event_t events[] = {{3u, 1000u}};
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(1u, result.missed);
}

static void sequence_hole_is_counted_and_stops_the_service(void)
{
    const nocti_v4_backlog_event_t events[] = {{1u, 10u}, {4u, 10u}};
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 2u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_sequence);
    TEST_ASSERT_EQUAL_UINT32(2u, result.sequence_gap);
    TEST_ASSERT_EQUAL_UINT32(3u, result.missed);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(1u, result.catchup);
}

static void spsc_preserves_order_and_counts_overflow(void)
{
    nocti_v4_spsc_t ring = {0};
    uint32_t value = 0u;
    uint32_t index;
    for (index = 1u; index <= NOCTI_V4_SPSC_CAPACITY; ++index) {
        TEST_ASSERT_TRUE(nocti_v4_spsc_push(&ring, index));
    }
    TEST_ASSERT_FALSE(nocti_v4_spsc_push(&ring, 99u));
    TEST_ASSERT_EQUAL_UINT32(1u, ring.overflow);
    TEST_ASSERT_EQUAL_UINT32(NOCTI_V4_SPSC_CAPACITY, ring.depth_max);
    for (index = 1u; index <= NOCTI_V4_SPSC_CAPACITY; ++index) {
        TEST_ASSERT_TRUE(nocti_v4_spsc_pop(&ring, &value));
        TEST_ASSERT_EQUAL_UINT32(index, value);
    }
    TEST_ASSERT_FALSE(nocti_v4_spsc_pop(&ring, &value));
    TEST_ASSERT_TRUE(nocti_v4_spsc_push(&ring, 100u));
    TEST_ASSERT_TRUE(nocti_v4_spsc_pop(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(100u, value);
}

static void empty_service_reads_nothing(void)
{
    const nocti_v4_backlog_result_t result = nocti_v4_backlog_service(NULL, 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(0u, result.missed);
    TEST_ASSERT_EQUAL_UINT8(0u, result.faulted);
}

void test_v4_rtos_suite(void)
{
    RUN_TEST(backlog_boot82_counts_three_and_reads_newest);
    RUN_TEST(backlog_one_queued_successor_is_not_a_fault);
    RUN_TEST(backlog_depth_16_faults_and_counts_every_pending_event);
    RUN_TEST(backlog_stale_newest_faults_without_a_fake_read);
    RUN_TEST(sequence_hole_is_counted_and_stops_the_service);
    RUN_TEST(spsc_preserves_order_and_counts_overflow);
    RUN_TEST(empty_service_reads_nothing);
}
