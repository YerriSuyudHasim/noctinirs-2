/* Batch 5A: production stop/fault diagnostic policy (nocti_v4_stop_diag.h).
 * Host/unit class: this is the real header the firmware compiles, run natively with
 * injected stage results. It does not run scan_stop(), the control worker, FreeRTOS
 * locking or any hardware; those are covered by source-structure and extracted-function
 * tests in tests/architecture/test_batch5a_stop_diag.py and by the Owner bench. */
#include "nn2_test.h"
#include "nocti_v4_stop_diag.h"
#include <limits.h>
#include <string.h>

/* Distinct nonzero codes so a wrong attribution cannot hide behind equal values. */
#define E_SCHED 0x101
#define E_ADS 0x102
#define E_OPTICAL 0x103
#define E_ASYNC 0x104
#define E_FRAME 0x105
#define E_NET 0x106

static nocti_v4_stop_diag_t d;

static void begin(void) { nocti_v4_stop_diag_reset(&d); }

static nocti_v4_stop_report_t attempt(int32_t s, int32_t a, int32_t o, int32_t l, int32_t f, int32_t n)
{
    nocti_v4_stop_report_t r;
    nocti_v4_stop_report_make(&r, s, a, o, l, f, n);
    return r;
}

static void commit(int32_t s, int32_t a, int32_t o, int32_t l, int32_t f, int32_t n)
{
    const nocti_v4_stop_report_t r = attempt(s, a, o, l, f, n);
    nocti_v4_stop_diag_commit(&d, &r);
}

/* Independent model of the baseline return chain in app_main.c scan_stop() at
 * bd880f5 (lines 242-247). It is intentionally not shared with the header. A
 * source-structure test checks that the real chain in app_main.c is unchanged. */
static int32_t baseline_return(int32_t s, int32_t a, int32_t o, int32_t l)
{
    if (s != 0) return s;
    if (a != 0) return a;
    if (o != 0) return o;
    return l;
}

/* Compare fields, not bytes: the three padding bytes are unspecified. */
static void assert_empty_state(const nocti_v4_stop_diag_t *x)
{
    TEST_ASSERT_EQUAL_INT32(0, x->ctrl_error);
    TEST_ASSERT_EQUAL_INT32(0, x->stop_stage_error);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_NONE, x->ctrl_origin);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_NONE, x->failed_stop_stage);
    TEST_ASSERT_EQUAL_UINT8(0u, x->stop_fail_mask);
    TEST_ASSERT_EQUAL_UINT8(0u, x->stop_attempts);
    TEST_ASSERT_EQUAL_UINT8(0u, x->stop_last_ok);
}

static void record_is_sixteen_bytes(void)
{
    TEST_ASSERT_EQUAL_UINT32(16u, (uint32_t)sizeof(nocti_v4_stop_diag_t));
    TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)sizeof(nocti_v4_stop_report_t));
}

static void zero_initialized_static_equals_reset_state(void)
{
    static nocti_v4_stop_diag_t boot; /* static storage is zero at boot */
    nocti_v4_stop_diag_t reset;
    memset(&reset, 0xA5, sizeof reset);
    nocti_v4_stop_diag_reset(&reset);
    assert_empty_state(&boot);
    assert_empty_state(&reset);
}

static void each_return_affecting_stage_fails_alone(void)
{
    const struct { int32_t e[4]; uint8_t stage, bit; int32_t code; } cases[] = {
        {{E_SCHED, 0, 0, 0}, NOCTI_V4_STOP_STAGE_SCHEDULER, NOCTI_V4_STOP_BIT_SCHEDULER, E_SCHED},
        {{0, E_ADS, 0, 0}, NOCTI_V4_STOP_STAGE_ADS, NOCTI_V4_STOP_BIT_ADS, E_ADS},
        {{0, 0, E_OPTICAL, 0}, NOCTI_V4_STOP_STAGE_OPTICAL, NOCTI_V4_STOP_BIT_OPTICAL, E_OPTICAL},
        {{0, 0, 0, E_ASYNC}, NOCTI_V4_STOP_STAGE_ASYNC_LED, NOCTI_V4_STOP_BIT_ASYNC_LED, E_ASYNC},
    };
    for (unsigned i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        begin();
        commit(cases[i].e[0], cases[i].e[1], cases[i].e[2], cases[i].e[3], 0, 0);
        TEST_ASSERT_EQUAL_UINT8(cases[i].stage, d.failed_stop_stage);
        TEST_ASSERT_EQUAL_INT32(cases[i].code, d.stop_stage_error);
        TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_STOP, d.ctrl_origin);
        TEST_ASSERT_EQUAL_INT32(cases[i].code, d.ctrl_error);
        TEST_ASSERT_EQUAL_UINT8(cases[i].bit, d.stop_fail_mask);
        TEST_ASSERT_EQUAL_UINT8(1u, d.stop_attempts);
        TEST_ASSERT_EQUAL_UINT8(0u, d.stop_last_ok);
    }
}

static void drain_only_failures_are_observed_not_causal(void)
{
    const struct { int32_t f, n; uint8_t mask; } cases[] = {
        {E_FRAME, 0, NOCTI_V4_STOP_BIT_FRAME_DRAIN},
        {0, E_NET, NOCTI_V4_STOP_BIT_NETWORK_DRAIN},
        {E_FRAME, E_NET, NOCTI_V4_STOP_BIT_FRAME_DRAIN | NOCTI_V4_STOP_BIT_NETWORK_DRAIN},
    };
    for (unsigned i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        const nocti_v4_stop_report_t r = attempt(0, 0, 0, 0, cases[i].f, cases[i].n);
        TEST_ASSERT_EQUAL_INT32(0, r.error);           /* the STOP does not fail */
        TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_NONE, r.stage);
        begin();
        nocti_v4_stop_diag_commit(&d, &r);
        TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_NONE, d.failed_stop_stage);
        TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_NONE, d.ctrl_origin);
        TEST_ASSERT_EQUAL_INT32(0, d.ctrl_error);
        TEST_ASSERT_EQUAL_INT32(0, d.stop_stage_error);
        TEST_ASSERT_EQUAL_UINT8(cases[i].mask, d.stop_fail_mask);
        TEST_ASSERT_EQUAL_UINT8(1u, d.stop_last_ok);   /* the return value was OK */
        TEST_ASSERT_EQUAL_UINT8(1u, d.stop_attempts);
    }
}

static void drain_combined_with_a_return_affecting_failure_names_only_the_cause(void)
{
    begin();
    commit(0, E_ADS, 0, 0, E_FRAME, E_NET);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_ADS, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.stop_stage_error);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_BIT_ADS | NOCTI_V4_STOP_BIT_FRAME_DRAIN |
                            NOCTI_V4_STOP_BIT_NETWORK_DRAIN, d.stop_fail_mask);
    TEST_ASSERT_EQUAL_UINT8(0u, d.stop_last_ok);
}

static void all_64_failure_combinations_follow_baseline_return_precedence(void)
{
    for (unsigned bits = 0u; bits < 64u; ++bits) {
        const int32_t s = bits & 0x01u ? E_SCHED : 0, a = bits & 0x02u ? E_ADS : 0;
        const int32_t o = bits & 0x04u ? E_OPTICAL : 0, l = bits & 0x08u ? E_ASYNC : 0;
        const int32_t f = bits & 0x10u ? E_FRAME : 0, n = bits & 0x20u ? E_NET : 0;
        const nocti_v4_stop_report_t r = attempt(s, a, o, l, f, n);
        const int32_t expected = baseline_return(s, a, o, l);
        TEST_ASSERT_EQUAL_INT32(expected, r.error);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)bits, r.mask);
        /* The reported stage is exactly the stage whose code the baseline returns. */
        const int32_t code_of_stage[] = {0, s, a, o, l};
        TEST_ASSERT_EQUAL_INT32(expected, code_of_stage[r.stage]);
        TEST_ASSERT_TRUE((r.stage == NOCTI_V4_STOP_STAGE_NONE) == (expected == 0));
        /* A drain bit alone never selects a stage. */
        if ((bits & 0x0Fu) == 0u) TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_NONE, r.stage);
    }
}

static void first_failure_survives_repeated_cleanup(void)
{
    begin();
    commit(0, E_ADS, 0, 0, 0, 0);               /* attempt 1: ADS fails */
    commit(0, 0, 0, 0, 0, 0);                    /* attempt 2: cleanup succeeds */
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_ADS, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.stop_stage_error);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_STOP, d.ctrl_origin);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.ctrl_error);
    TEST_ASSERT_EQUAL_UINT8(1u, d.stop_last_ok);
    TEST_ASSERT_EQUAL_UINT8(2u, d.stop_attempts);
    commit(E_SCHED, 0, E_OPTICAL, 0, E_FRAME, 0); /* attempt 3: a different failure */
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_ADS, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.stop_stage_error);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.ctrl_error);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_BIT_ADS | NOCTI_V4_STOP_BIT_SCHEDULER |
                            NOCTI_V4_STOP_BIT_OPTICAL | NOCTI_V4_STOP_BIT_FRAME_DRAIN,
                            d.stop_fail_mask);
    TEST_ASSERT_EQUAL_UINT8(0u, d.stop_last_ok);
    TEST_ASSERT_EQUAL_UINT8(3u, d.stop_attempts);
    commit(0, 0, 0, 0, 0, 0);                    /* attempt 4: clean again */
    TEST_ASSERT_EQUAL_UINT8(1u, d.stop_last_ok);
    TEST_ASSERT_EQUAL_UINT8(4u, d.stop_attempts);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_ADS, d.failed_stop_stage);
}

static void two_failures_in_one_attempt_latch_the_one_the_baseline_returns(void)
{
    begin();
    commit(0, 0, E_OPTICAL, E_ASYNC, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_OPTICAL, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_INT32(E_OPTICAL, d.stop_stage_error);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_BIT_OPTICAL | NOCTI_V4_STOP_BIT_ASYNC_LED, d.stop_fail_mask);
}

static void start_failure_then_stop_failure_latch_independently(void)
{
    begin();
    nocti_v4_stop_diag_note_ctrl(&d, NOCTI_V4_CTRL_ORIGIN_START, E_SCHED);
    commit(0, 0, E_OPTICAL, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_START, d.ctrl_origin);
    TEST_ASSERT_EQUAL_INT32(E_SCHED, d.ctrl_error);      /* the start error is kept */
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_OPTICAL, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_INT32(E_OPTICAL, d.stop_stage_error);
}

static void start_failure_followed_by_clean_cleanup_reports_no_stop_stage(void)
{
    begin();
    nocti_v4_stop_diag_note_ctrl(&d, NOCTI_V4_CTRL_ORIGIN_START, E_ADS);
    commit(0, 0, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_START, d.ctrl_origin);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_NONE, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_UINT8(1u, d.stop_attempts);
    TEST_ASSERT_EQUAL_UINT8(1u, d.stop_last_ok);
}

static void control_failure_is_first_wins_and_none_is_ignored(void)
{
    begin();
    nocti_v4_stop_diag_note_ctrl(&d, NOCTI_V4_CTRL_ORIGIN_NONE, 77);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_NONE, d.ctrl_origin);
    TEST_ASSERT_EQUAL_INT32(0, d.ctrl_error);
    nocti_v4_stop_diag_note_ctrl(&d, NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED, 259);
    nocti_v4_stop_diag_note_ctrl(&d, NOCTI_V4_CTRL_ORIGIN_BOOT, -1);
    commit(E_SCHED, 0, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED, d.ctrl_origin);
    TEST_ASSERT_EQUAL_INT32(259, d.ctrl_error);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_SCHEDULER, d.failed_stop_stage);
}

static void normal_sessions_report_one_attempt_regardless_of_history(void)
{
    begin();
    for (unsigned session = 0u; session < 5u; ++session) {
        begin();                                   /* accepted START resets */
        TEST_ASSERT_EQUAL_UINT8(0u, d.stop_attempts);
        commit(0, 0, 0, 0, 0, 0);                  /* one scan_stop at STOP */
        TEST_ASSERT_EQUAL_UINT8(1u, d.stop_attempts);
        TEST_ASSERT_EQUAL_UINT8(1u, d.stop_last_ok);
        TEST_ASSERT_EQUAL_UINT8(0u, d.stop_fail_mask);
        TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_CTRL_ORIGIN_NONE, d.ctrl_origin);
    }
}

static void reset_clears_a_populated_record(void)
{
    begin();
    nocti_v4_stop_diag_note_ctrl(&d, NOCTI_V4_CTRL_ORIGIN_START, E_ADS);
    commit(E_SCHED, E_ADS, E_OPTICAL, E_ASYNC, E_FRAME, E_NET);
    begin();
    assert_empty_state(&d);
}

static void attempt_counter_saturates_at_255_without_wrapping(void)
{
    begin();
    commit(0, E_ADS, 0, 0, 0, 0);
    for (unsigned i = 0u; i < 300u; ++i) commit(E_SCHED, 0, 0, 0, E_FRAME, 0);
    TEST_ASSERT_EQUAL_UINT8(255u, d.stop_attempts);
    commit(0, 0, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(255u, d.stop_attempts);       /* still 255, not 0 */
    TEST_ASSERT_EQUAL_UINT8(1u, d.stop_last_ok);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_STAGE_ADS, d.failed_stop_stage);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.stop_stage_error);
    TEST_ASSERT_EQUAL_INT32(E_ADS, d.ctrl_error);
}

static void reserved_mask_bits_are_never_stored(void)
{
    begin();
    nocti_v4_stop_report_t r = attempt(0, 0, 0, 0, 0, 0);
    r.mask = 0xFFu;
    nocti_v4_stop_diag_commit(&d, &r);
    TEST_ASSERT_EQUAL_UINT8(NOCTI_V4_STOP_MASK_ALL, d.stop_fail_mask);
}

static void format_matches_exact_text_for_clean_failed_and_unused_states(void)
{
    char text[256];
    begin();
    TEST_ASSERT_EQUAL_UINT32(strlen(" ctrl_origin=NONE ctrl_error=0 failed_stop_stage=NONE stop_stage_error=0"
        " stop_fail_mask=0x00 stop_attempts=0 stop_last_ok=na stop_other_codes=none"),
        (uint32_t)nocti_v4_stop_diag_format(&d, text, sizeof text));
    TEST_ASSERT_EQUAL_STRING(" ctrl_origin=NONE ctrl_error=0 failed_stop_stage=NONE stop_stage_error=0"
        " stop_fail_mask=0x00 stop_attempts=0 stop_last_ok=na stop_other_codes=none", text);
    commit(0, 0, 0, 0, 0, 0);
    nocti_v4_stop_diag_format(&d, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING(" ctrl_origin=NONE ctrl_error=0 failed_stop_stage=NONE stop_stage_error=0"
        " stop_fail_mask=0x00 stop_attempts=1 stop_last_ok=1 stop_other_codes=none", text);
    begin();
    commit(0, E_ADS, 0, 0, 0, 0);
    nocti_v4_stop_diag_format(&d, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING(" ctrl_origin=STOP ctrl_error=258 failed_stop_stage=ADS stop_stage_error=258"
        " stop_fail_mask=0x02 stop_attempts=1 stop_last_ok=0 stop_other_codes=none", text);
}

static void other_codes_are_unavailable_unless_the_mask_is_only_the_latched_stage(void)
{
    char text[256];
    begin();
    commit(0, 0, 0, 0, E_FRAME, 0);                       /* drain only */
    nocti_v4_stop_diag_format(&d, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "failed_stop_stage=NONE"));
    TEST_ASSERT_NOT_NULL(strstr(text, "stop_fail_mask=0x10"));
    TEST_ASSERT_NOT_NULL(strstr(text, "stop_other_codes=unavailable"));
    begin();
    commit(E_SCHED, 0, 0, E_ASYNC, 0, 0);                 /* two return-affecting */
    nocti_v4_stop_diag_format(&d, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "stop_other_codes=unavailable"));
    begin();
    commit(E_SCHED, 0, 0, 0, 0, 0);                       /* one stage only */
    nocti_v4_stop_diag_format(&d, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "stop_other_codes=none"));
    commit(0, 0, 0, 0, 0, 0);                             /* later clean cleanup */
    nocti_v4_stop_diag_format(&d, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "stop_other_codes=none"));
}

static void format_at_maximum_field_widths_is_188_bytes_and_all_or_nothing(void)
{
    static const char expected[] =
        " ctrl_origin=RUNTIME_SCHED ctrl_error=-2147483648 failed_stop_stage=ASYNC_LED"
        " stop_stage_error=-2147483648 stop_fail_mask=0x3F stop_attempts=255 stop_last_ok=0"
        " stop_other_codes=unavailable";
    char text[256];
    memset(&d, 0, sizeof d);
    d.ctrl_error = INT32_MIN; d.stop_stage_error = INT32_MIN;
    d.ctrl_origin = NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED;
    d.failed_stop_stage = NOCTI_V4_STOP_STAGE_ASYNC_LED;
    d.stop_fail_mask = NOCTI_V4_STOP_MASK_ALL; d.stop_attempts = 255u; d.stop_last_ok = 0u;
    TEST_ASSERT_EQUAL_UINT32(188u, (uint32_t)(sizeof expected - 1u));
    TEST_ASSERT_EQUAL_UINT32(188u, (uint32_t)nocti_v4_stop_diag_format(&d, text, sizeof text));
    TEST_ASSERT_EQUAL_STRING(expected, text);
    memset(text, 'x', sizeof text);
    TEST_ASSERT_EQUAL_UINT32(188u, (uint32_t)nocti_v4_stop_diag_format(&d, text, 189u)); /* exact fit */
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)nocti_v4_stop_diag_format(&d, text, 188u));   /* one short */
    TEST_ASSERT_EQUAL_CHAR('\0', text[0]);                /* nothing partial is left behind */
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)nocti_v4_stop_diag_format(&d, text, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)nocti_v4_stop_diag_format(&d, NULL, 10u));
}

static void unknown_enum_values_are_named_invalid(void)
{
    char text[256];
    memset(&d, 0, sizeof d);
    d.ctrl_origin = 200u; d.failed_stop_stage = 99u; d.stop_attempts = 1u;
    TEST_ASSERT_TRUE(nocti_v4_stop_diag_format(&d, text, sizeof text) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(text, "ctrl_origin=INVALID"));
    TEST_ASSERT_NOT_NULL(strstr(text, "failed_stop_stage=INVALID"));
}

void test_v4_stop_diag_suite(void)
{
    RUN_TEST(record_is_sixteen_bytes);
    RUN_TEST(zero_initialized_static_equals_reset_state);
    RUN_TEST(each_return_affecting_stage_fails_alone);
    RUN_TEST(drain_only_failures_are_observed_not_causal);
    RUN_TEST(drain_combined_with_a_return_affecting_failure_names_only_the_cause);
    RUN_TEST(all_64_failure_combinations_follow_baseline_return_precedence);
    RUN_TEST(first_failure_survives_repeated_cleanup);
    RUN_TEST(two_failures_in_one_attempt_latch_the_one_the_baseline_returns);
    RUN_TEST(start_failure_then_stop_failure_latch_independently);
    RUN_TEST(start_failure_followed_by_clean_cleanup_reports_no_stop_stage);
    RUN_TEST(control_failure_is_first_wins_and_none_is_ignored);
    RUN_TEST(normal_sessions_report_one_attempt_regardless_of_history);
    RUN_TEST(reset_clears_a_populated_record);
    RUN_TEST(attempt_counter_saturates_at_255_without_wrapping);
    RUN_TEST(reserved_mask_bits_are_never_stored);
    RUN_TEST(format_matches_exact_text_for_clean_failed_and_unused_states);
    RUN_TEST(other_codes_are_unavailable_unless_the_mask_is_only_the_latched_stage);
    RUN_TEST(format_at_maximum_field_widths_is_188_bytes_and_all_or_nothing);
    RUN_TEST(unknown_enum_values_are_named_invalid);
}
