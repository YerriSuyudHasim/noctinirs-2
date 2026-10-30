#include "nn2_test.h"
#include "nocti_v4_sample_clock.h"

static void delayed_task_delivery_cannot_move_the_physical_commit(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan, delayed;
    nocti_v4_sample_k_count_t count;
    TEST_ASSERT_TRUE(nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_TRUE(nocti_v4_sample_slot_plan(&geo, 1000u, 0u, 0u, &plan));
    delayed = plan;
    /* Callback captures these indices; the scheduler may receive the record
     * forty samples later. Delivery time cannot become first-affected time. */
    nocti_v4_sample_note_commit(&plan, 1, 999u);
    nocti_v4_sample_note_commit(&plan, 0, 1017u);
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(1000u, plan.on_first_affected);
    TEST_ASSERT_EQUAL_UINT32(1018u, plan.off_first_affected);
    TEST_ASSERT_EQUAL_UINT32(14u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
    nocti_v4_sample_note_commit(&delayed, 1, 1039u);
    nocti_v4_sample_note_commit(&delayed, 0, 1057u);
    nocti_v4_sample_count_slot(&geo, &delayed, &count);
    TEST_ASSERT_EQUAL_UINT32(0u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(0u, count.off_stable);
}

static void geometry_matches_the_ten_hertz_grid(void)
{
    nocti_v4_sample_geometry_t geo;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_UINT32(5000u, geo.slot_us);
    TEST_ASSERT_EQUAL_UINT32(40u, geo.slot_samples);
    TEST_ASSERT_EQUAL_UINT32(800u, geo.frame_samples);
    TEST_ASSERT_EQUAL_UINT32(18u, geo.off_phase_samples);
    TEST_ASSERT_EQUAL_UINT32(4u, geo.settle_samples);
    TEST_ASSERT_EQUAL_UINT32(1u, geo.guard_samples);
    TEST_ASSERT_EQUAL_UINT32(2u, geo.lead_samples);
    TEST_ASSERT_EQUAL_UINT32(14u, geo.on_stable_samples);
    TEST_ASSERT_EQUAL_UINT32(17u, geo.off_stable_samples);
    TEST_ASSERT_EQUAL_UINT32(8000u, geo.adc_hz);
}

static void every_allowed_rate_holds_k_target(void)
{
    static const uint32_t rates[] = {2u, 4u, 5u, 8u, 10u};
    uint32_t index;
    for (index = 0u; index < 5u; ++index) {
        nocti_v4_sample_geometry_t geo;
        uint32_t slot_us = 1000000u / (20u * rates[index]);
        TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(rates[index],
                                                              slot_us * 44u / 100u, &geo));
        TEST_ASSERT_TRUE(geo.on_stable_samples >= NOCTI_V4_SAMPLE_K_TARGET);
        TEST_ASSERT_TRUE(geo.off_stable_samples >= NOCTI_V4_SAMPLE_K_TARGET);
        TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(rates[index],
                                                              slot_us / 2u, &geo));
        TEST_ASSERT_TRUE(geo.on_stable_samples >= NOCTI_V4_SAMPLE_K_TARGET);
        TEST_ASSERT_TRUE(geo.off_stable_samples >= NOCTI_V4_SAMPLE_K_TARGET);
        TEST_ASSERT_EQUAL_UINT32(slot_us / 125u, geo.slot_samples);
    }
}

static void rejected_rates_do_not_produce_a_plan(void)
{
    nocti_v4_sample_geometry_t geo;
    TEST_ASSERT_EQUAL_INT(0, nocti_v4_sample_geometry_for(1u, 22000u, &geo));
    TEST_ASSERT_EQUAL_INT(0, nocti_v4_sample_geometry_for(3u, 7333u, &geo));
    TEST_ASSERT_EQUAL_INT(0, nocti_v4_sample_geometry_for(10u, 0u, &geo));
    TEST_ASSERT_EQUAL_INT(0, nocti_v4_sample_geometry_for(10u, 5000u, &geo));
    TEST_ASSERT_EQUAL_UINT8(0u, geo.valid);
}

static void edges_sit_on_the_sample_index(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 1000u, 0u, 0u, &plan));
    TEST_ASSERT_EQUAL_UINT32(1000u, plan.on_edge);
    TEST_ASSERT_EQUAL_UINT32(998u, plan.on_submit);
    TEST_ASSERT_EQUAL_UINT32(1018u, plan.off_edge);
    TEST_ASSERT_EQUAL_UINT32(1016u, plan.off_submit);
    TEST_ASSERT_EQUAL_UINT32(1040u, plan.slot_end);
    TEST_ASSERT_EQUAL_UINT32(1039u, plan.guard_start);
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 1000u, 1u, 3u, &plan));
    TEST_ASSERT_EQUAL_UINT32(1000u + 800u + 3u * 40u, plan.on_edge);
}

static void on_time_commit_excludes_settle_and_counts_k(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 0u, 0u, 0u, &plan));
    nocti_v4_sample_note_commit(&plan, 1, plan.on_edge - 1u);
    nocti_v4_sample_note_commit(&plan, 0, plan.off_edge - 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, plan.on_commit_late);
    TEST_ASSERT_EQUAL_UINT8(0u, plan.off_commit_late);
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_SETTLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_edge));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_SETTLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_edge + 3u));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_STABLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_edge + 4u));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_OFF_SETTLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.off_edge));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_OFF_STABLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.off_edge + 4u));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_END_GUARD,
                          nocti_v4_sample_classify(&geo, &plan, plan.guard_start));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_OUTSIDE,
                          nocti_v4_sample_classify(&geo, &plan, plan.slot_end));
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(14u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
    TEST_ASSERT_EQUAL_UINT32(0u, count.commit_late);
    TEST_ASSERT_TRUE(count.on_stable >= NOCTI_V4_SAMPLE_K_TARGET);
    TEST_ASSERT_TRUE(count.off_stable >= NOCTI_V4_SAMPLE_K_TARGET);
}

static void late_commit_is_marked_and_not_stable(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    uint32_t late_index;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 50u, 0u, 1u, &plan));
    nocti_v4_sample_note_commit(&plan, 1, plan.on_edge + 1u);
    nocti_v4_sample_note_commit(&plan, 0, plan.off_edge - 1u);
    TEST_ASSERT_EQUAL_UINT32(2u, plan.on_late_samples);
    late_index = plan.on_edge;
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_COMMIT_LATE,
                          nocti_v4_sample_classify(&geo, &plan, late_index));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_COMMIT_LATE,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_edge + 1u));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_SETTLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_first_affected));
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(2u, count.commit_late);
    TEST_ASSERT_EQUAL_UINT32(12u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
}

static void early_commit_does_not_open_the_window_early(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 0u, 0u, 0u, &plan));
    nocti_v4_sample_note_commit(&plan, 1, plan.on_edge - 5u);
    nocti_v4_sample_note_commit(&plan, 0, plan.off_edge - 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, plan.on_commit_late);
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_SETTLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_edge));
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(14u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
}

static void early_off_commit_removes_on_samples(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 0u, 0u, 0u, &plan));
    nocti_v4_sample_note_commit(&plan, 1, plan.on_edge - 1u);
    nocti_v4_sample_note_commit(&plan, 0, plan.off_edge - 3u);
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_OFF_TRANSITION,
                          nocti_v4_sample_classify(&geo, &plan, plan.off_edge - 1u));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_STABLE,
                          nocti_v4_sample_classify(&geo, &plan, plan.off_first_affected - 1u));
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(12u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
}

static void missing_commit_has_no_stable_samples(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 0u, 0u, 0u, &plan));
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(0u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(0u, count.off_stable);
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_PRECOMMIT,
                          nocti_v4_sample_classify(&geo, &plan, plan.on_edge + 10u));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_OFF_TRANSITION,
                          nocti_v4_sample_classify(&geo, &plan, plan.off_edge + 6u));
}

static void drift_and_jitter_do_not_change_k(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    uint32_t slot;
    uint64_t previous_anchor = 0u;
    /* +30 ppm on a 125 us period is 0.00375 us. Integer timestamps stay on
     * the 125 us grid here; the jitter is an additive ESP-clock error that
     * the classifier is not allowed to see. */
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    for (slot = 0u; slot < NOCTI_V4_SAMPLE_SLOT_COUNT; ++slot) {
        uint32_t sequence;
        uint64_t anchor;
        TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, 10u, 0u, slot, &plan));
        nocti_v4_sample_note_commit(&plan, 1, plan.on_edge - 1u);
        nocti_v4_sample_note_commit(&plan, 0, plan.off_edge - 1u);
        anchor = nocti_v4_sample_host_us(1000000ull, 10u, plan.on_edge);
        if (slot > 0u) {
            TEST_ASSERT_TRUE(anchor > previous_anchor);
        }
        previous_anchor = anchor;
        for (sequence = plan.on_edge; sequence != plan.slot_end; sequence += 1u) {
            int32_t jitter = (int32_t)(sequence % 5u) - 2;
            uint64_t drifted = anchor +
                (uint64_t)(sequence - plan.on_edge) * 125ull + (uint64_t)(jitter + 2);
            (void)drifted;
            (void)nocti_v4_sample_classify(&geo, &plan, sequence);
        }
        nocti_v4_sample_count_slot(&geo, &plan, &count);
        TEST_ASSERT_EQUAL_UINT32(14u, count.on_stable);
        TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
    }
    TEST_ASSERT_TRUE(previous_anchor > nocti_v4_sample_host_us(1000000ull, 10u, 10u));
}

static void frame_anchors_stay_monotonic_across_wrap(void)
{
    nocti_v4_sample_geometry_t geo;
    nocti_v4_sample_slot_plan_t plan;
    nocti_v4_sample_k_count_t count;
    const uint32_t origin = 0xFFFFFFF0u;
    uint64_t first;
    uint64_t second;
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_geometry_for(10u, 2200u, &geo));
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_slot_plan(&geo, origin, 0u, 0u, &plan));
    TEST_ASSERT_EQUAL_UINT32(origin, plan.on_edge);
    TEST_ASSERT_EQUAL_UINT32(origin + 40u, plan.slot_end);
    nocti_v4_sample_note_commit(&plan, 1, plan.on_edge - 1u);
    nocti_v4_sample_note_commit(&plan, 0, plan.off_edge - 1u);
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_ON_SETTLE,
                          nocti_v4_sample_classify(&geo, &plan, origin));
    TEST_ASSERT_EQUAL_INT(NOCTI_V4_SAMPLE_CLASS_END_GUARD,
                          nocti_v4_sample_classify(&geo, &plan, plan.guard_start));
    nocti_v4_sample_count_slot(&geo, &plan, &count);
    TEST_ASSERT_EQUAL_UINT32(14u, count.on_stable);
    TEST_ASSERT_EQUAL_UINT32(17u, count.off_stable);
    first = nocti_v4_sample_host_us(0u, origin, origin);
    second = nocti_v4_sample_host_us(0u, origin, origin + geo.frame_samples);
    TEST_ASSERT_EQUAL_UINT64(0u, first);
    TEST_ASSERT_TRUE(second > first);
    TEST_ASSERT_EQUAL_INT(1, nocti_v4_sample_reached(0u, origin));
    TEST_ASSERT_EQUAL_INT(0, nocti_v4_sample_reached(origin, 0u));
}

static void time64_is_little_endian(void)
{
    uint8_t bytes[8];
    const uint64_t value = 0x0807060504030201ull;
    nocti_v4_sample_store_u64_le(bytes, value);
    TEST_ASSERT_EQUAL_UINT8(0x01u, bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0x08u, bytes[7]);
    TEST_ASSERT_EQUAL_UINT64(value, nocti_v4_sample_load_u64_le(bytes));
    TEST_ASSERT_TRUE(bytes[0] != 0x08u);
}

void test_v4_sample_clock_suite(void)
{
    RUN_TEST(delayed_task_delivery_cannot_move_the_physical_commit);
    RUN_TEST(geometry_matches_the_ten_hertz_grid);
    RUN_TEST(every_allowed_rate_holds_k_target);
    RUN_TEST(rejected_rates_do_not_produce_a_plan);
    RUN_TEST(edges_sit_on_the_sample_index);
    RUN_TEST(on_time_commit_excludes_settle_and_counts_k);
    RUN_TEST(late_commit_is_marked_and_not_stable);
    RUN_TEST(early_commit_does_not_open_the_window_early);
    RUN_TEST(early_off_commit_removes_on_samples);
    RUN_TEST(missing_commit_has_no_stable_samples);
    RUN_TEST(drift_and_jitter_do_not_change_k);
    RUN_TEST(frame_anchors_stay_monotonic_across_wrap);
    RUN_TEST(time64_is_little_endian);
}
