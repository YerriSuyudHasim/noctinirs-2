/* Executes the production ISR helper and arm/rearm/latch APIs with SDK mocks.
 * This verifies wake semantics, not FreeRTOS scheduling or physical latency. */
#include "nn2_test.h"
#include "nocti_v4_sample_edge.h"
#include "esp_task_wdt.h"

static unsigned s_task;
static uint32_t s_notifications;

void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{
    TEST_ASSERT_EQUAL_PTR(&s_task, task);
    ++s_notifications;
    *woken = pdTRUE;
}

/* Only satisfy unrelated functions linked from common.c; watchdog behavior
 * is deliberately outside this sample-edge fixture's coverage. */
esp_err_t esp_task_wdt_add(void *task) { (void)task; return ESP_OK; }
esp_err_t esp_task_wdt_reset(void) { return ESP_OK; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "HOST_MOCK"; }

static void arm(uint32_t generation)
{
    s_notifications = 0;
    nocti_v4_sample_edge_arm(&s_task, generation);
}

static void drdy(uint32_t sequence, uint64_t timestamp)
{
    BaseType_t woken = pdFALSE;
    nocti_v4_sample_edge_on_drdy(sequence, timestamp, &woken);
}

static void one_pending_edge_does_not_create_an_eight_khz_wake_storm(void)
{
    uint32_t sequence, generation;
    arm(7u);
    TEST_ASSERT_FALSE(nocti_v4_sample_edge_observed(&sequence, &generation));
    for (uint32_t n = 100u; n < 180u; ++n) drdy(n, (uint64_t)n * 125u);
    TEST_ASSERT_EQUAL_UINT32(1u, s_notifications);
    TEST_ASSERT_TRUE(nocti_v4_sample_edge_observed(&sequence, &generation));
    TEST_ASSERT_EQUAL_UINT32(179u, sequence);
    TEST_ASSERT_EQUAL_UINT32(7u, generation);
}

static void next_edge_rearms_once_even_if_already_due(void)
{
    arm(8u);
    nocti_v4_sample_edge_set_submit(20u);
    drdy(19u, 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, s_notifications);
    for (uint32_t n = 20u; n < 60u; ++n) drdy(n, (uint64_t)n * 125u);
    TEST_ASSERT_EQUAL_UINT32(1u, s_notifications);
    nocti_v4_sample_edge_set_submit(40u);
    drdy(60u, 7500u);
    drdy(61u, 7625u);
    TEST_ASSERT_EQUAL_UINT32(2u, s_notifications);
}

static void anchor_latch_is_independent_of_a_pending_wake(void)
{
    uint32_t sequence;
    uint64_t timestamp;
    arm(9u);
    nocti_v4_sample_edge_set_watch(12u);
    drdy(10u, 1000u);
    drdy(11u, 1125u);
    TEST_ASSERT_FALSE(nocti_v4_sample_edge_copy_latch(&sequence, &timestamp));
    drdy(12u, 0x100000002ull);
    drdy(13u, 0x100000080ull);
    TEST_ASSERT_EQUAL_UINT32(1u, s_notifications);
    TEST_ASSERT_TRUE(nocti_v4_sample_edge_copy_latch(&sequence, &timestamp));
    TEST_ASSERT_EQUAL_UINT32(12u, sequence);
    TEST_ASSERT_EQUAL_UINT64(0x100000002ull, timestamp);
}

static void disarm_and_new_generation_do_not_reuse_an_old_wake(void)
{
    uint32_t sequence, generation;
    arm(10u);
    drdy(20u, 1000u);
    nocti_v4_sample_edge_disarm();
    drdy(21u, 1125u);
    TEST_ASSERT_EQUAL_UINT32(1u, s_notifications);
    nocti_v4_sample_edge_arm(&s_task, 11u);
    TEST_ASSERT_FALSE(nocti_v4_sample_edge_observed(&sequence, &generation));
    drdy(1u, 125u);
    TEST_ASSERT_EQUAL_UINT32(2u, s_notifications);
    TEST_ASSERT_TRUE(nocti_v4_sample_edge_observed(&sequence, &generation));
    TEST_ASSERT_EQUAL_UINT32(11u, generation);
    TEST_ASSERT_EQUAL_UINT32(1u, sequence);
}

static void one_shot_targets_work_across_sample_index_wrap(void)
{
    arm(12u);
    nocti_v4_sample_edge_set_submit(0xfffffffeu);
    drdy(0xfffffffdu, 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, s_notifications);
    drdy(0xfffffffeu, 2u);
    drdy(0xffffffffu, 3u);
    drdy(0u, 4u);
    TEST_ASSERT_EQUAL_UINT32(1u, s_notifications);
    nocti_v4_sample_edge_set_submit(1u);
    drdy(0u, 5u);
    TEST_ASSERT_EQUAL_UINT32(1u, s_notifications);
    drdy(1u, 6u);
    drdy(2u, 7u);
    TEST_ASSERT_EQUAL_UINT32(2u, s_notifications);
}

void test_v4_sample_edge_suite(void)
{
    RUN_TEST(one_pending_edge_does_not_create_an_eight_khz_wake_storm);
    RUN_TEST(next_edge_rearms_once_even_if_already_due);
    RUN_TEST(anchor_latch_is_independent_of_a_pending_wake);
    RUN_TEST(disarm_and_new_generation_do_not_reuse_an_old_wake);
    RUN_TEST(one_shot_targets_work_across_sample_index_wrap);
}
