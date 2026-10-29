#include "nn2_test.h"
#include "nocti_v4_backlog.h"

static nocti_v4_backlog_event_t known(uint32_t sequence, uint32_t age_us)
{
    nocti_v4_backlog_event_t event;
    event.sequence = sequence;
    event.age_us = age_us;
    event.age_known = 1u;
    return event;
}

static void backlog_boot82_counts_three_and_reads_newest(void)
{
    /* Four DRDY events were queued and the newest is still this conversion.
     * The three older conversions were overwritten. That is boot82's depth,
     * without the old hard fault at a remainder of 2. */
    const nocti_v4_backlog_event_t events[] = {
        known(1u, 375u), known(2u, 250u), known(3u, 125u), known(4u, 20u),
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
    const nocti_v4_backlog_event_t events[] = {known(8u, 40u), known(9u, 10u)};
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 2u);
    TEST_ASSERT_EQUAL_UINT32(1u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(1u, result.catchup);
    TEST_ASSERT_EQUAL_UINT8(0u, result.faulted);
}

static void backlog_depth_matches_the_one_millisecond_window(void)
{
    /* Eight successors are 1 ms at 8 kSPS. Ages stay young so only depth fires. */
    nocti_v4_backlog_event_t events[9];
    uint32_t index;
    TEST_ASSERT_EQUAL_UINT32(8u, NOCTI_V4_BACKLOG_FAULT_DEPTH);
    for (index = 0u; index < 9u; ++index) {
        events[index] = known(index + 1u, 10u);
    }
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 9u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.faulted);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(0u, result.catchup);
    TEST_ASSERT_EQUAL_UINT32(9u, result.missed_backlog);
}

static void backlog_seven_successors_are_counted_not_faulted(void)
{
    nocti_v4_backlog_event_t events[8];
    uint32_t index;
    for (index = 0u; index < 8u; ++index) {
        events[index] = known(index + 1u, 10u);
    }
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 8u);
    TEST_ASSERT_EQUAL_UINT8(0u, result.faulted);
    TEST_ASSERT_EQUAL_UINT32(1u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(7u, result.catchup);
    TEST_ASSERT_EQUAL_UINT32(7u, result.missed);
}

static void backlog_stale_newest_faults_without_a_fake_read(void)
{
    const nocti_v4_backlog_event_t events[] = {known(3u, 1000u)};
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(1u, result.missed);
}

static void backlog_unknown_age_is_a_fault_even_when_the_queue_looks_empty(void)
{
    nocti_v4_backlog_event_t event;
    event.sequence = 5u;
    event.age_us = 0u;
    event.age_known = 0u;
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(&event, 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(0u, result.catchup);
    TEST_ASSERT_EQUAL_UINT32(1u, result.missed);
}

static void backlog_unknown_age_does_not_drop_as_if_it_were_fresh(void)
{
    nocti_v4_backlog_event_t events[2];
    events[0].sequence = 1u;
    events[0].age_us = 0u;
    events[0].age_known = 0u;
    events[1] = known(2u, 10u);
    const nocti_v4_backlog_result_t result =
        nocti_v4_backlog_service(events, 2u);
    TEST_ASSERT_EQUAL_UINT8(1u, result.fault_backlog);
    TEST_ASSERT_EQUAL_UINT32(0u, result.reads);
    TEST_ASSERT_EQUAL_UINT32(0u, result.catchup);
    TEST_ASSERT_EQUAL_UINT32(2u, result.missed);
}

static void sequence_hole_is_counted_and_stops_the_service(void)
{
    const nocti_v4_backlog_event_t events[] = {known(1u, 10u), known(4u, 10u)};
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
    /* Capacity 8 models the index rule. The firmware DRDY ring is 128. */
    nocti_v4_spsc_t ring = {0};
    uint32_t value = 0u;
    uint32_t index;
    TEST_ASSERT_EQUAL_UINT32(8u, NOCTI_V4_SPSC_CAPACITY);
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
    RUN_TEST(backlog_depth_matches_the_one_millisecond_window);
    RUN_TEST(backlog_seven_successors_are_counted_not_faulted);
    RUN_TEST(backlog_stale_newest_faults_without_a_fake_read);
    RUN_TEST(backlog_unknown_age_is_a_fault_even_when_the_queue_looks_empty);
    RUN_TEST(backlog_unknown_age_does_not_drop_as_if_it_were_fresh);
    RUN_TEST(sequence_hole_is_counted_and_stops_the_service);
    RUN_TEST(spsc_preserves_order_and_counts_overflow);
    RUN_TEST(empty_service_reads_nothing);
}
