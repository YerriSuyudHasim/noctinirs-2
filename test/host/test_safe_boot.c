#include "nn2_test.h"
#include "nn2_app_safe_boot.h"
#include "nn2_reset_led_pin.h"
#include <string.h>

enum {
    OP_RESET_LOW = 1,
    OP_RESET_RELEASE,
    OP_DELAY,
    OP_WRITE_IREFALL,
    OP_WRITE_PWMALL,
    OP_WRITE_LEDOUT,
    OP_READ_IREF,
    OP_READ_LEDOUT
};

typedef struct {
    int op;
    uint8_t index;
    uint8_t value;
    uint32_t delay_ms;
} op_t;

typedef struct {
    op_t ops[80];
    size_t n;
    int errors;
    nn2_boot_outcome_t error_outcome;
    nn2_status_t error_status;
    nn2_status_t fail_low;
    nn2_status_t fail_release;
    nn2_status_t fail_delay;
    nn2_status_t fail_bus;
    int fail_bus_on;
    int bus_seen;
    int low_seen;
    int fail_low_on;
    uint8_t iref[NN2_PORT_EMITTER_SAFE_IREF_COUNT];
    uint8_t ledout[NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT];
    const nn2_app_safe_boot_result_t *watching;
    int saw_first;
    nn2_reset_reason_t reason_at_first;
} fake_t;

static fake_t g;

static void push(int op, uint8_t index, uint8_t value, uint32_t delay_ms)
{
    TEST_ASSERT_TRUE(g.n < sizeof g.ops / sizeof g.ops[0]);
    if (g.n == 0u && g.watching) {
        g.reason_at_first = g.watching->reset_reason;
        g.saw_first = 1;
    }
    g.ops[g.n].op = op;
    g.ops[g.n].index = index;
    g.ops[g.n].value = value;
    g.ops[g.n].delay_ms = delay_ms;
    g.n++;
}

static nn2_status_t m_reset(void *ctx, bool asserted)
{
    fake_t *f = ctx;
    if (asserted) {
        int seen = f->low_seen++;
        push(OP_RESET_LOW, 0u, 0u, 0u);
        if (f->fail_low != NN2_OK && (f->fail_low_on < 0 || seen == f->fail_low_on)) {
            return f->fail_low;
        }
        return NN2_OK;
    }
    push(OP_RESET_RELEASE, 0u, 0u, 0u);
    return f->fail_release;
}

static nn2_status_t m_delay(void *ctx, uint32_t ms)
{
    fake_t *f = ctx;
    push(OP_DELAY, 0u, 0u, ms);
    return f->fail_delay;
}

static nn2_status_t next_bus(fake_t *f)
{
    int seen = f->bus_seen++;
    if (f->fail_bus != NN2_OK && seen == f->fail_bus_on) {
        return f->fail_bus;
    }
    return NN2_OK;
}

static nn2_status_t m_irefall(void *ctx, uint8_t value)
{
    fake_t *f = ctx;
    push(OP_WRITE_IREFALL, 0u, value, 0u);
    return next_bus(f);
}

static nn2_status_t m_pwmall(void *ctx, uint8_t value)
{
    fake_t *f = ctx;
    push(OP_WRITE_PWMALL, 0u, value, 0u);
    return next_bus(f);
}

static nn2_status_t m_ledout_w(void *ctx, uint8_t index, uint8_t value)
{
    fake_t *f = ctx;
    push(OP_WRITE_LEDOUT, index, value, 0u);
    return next_bus(f);
}

static nn2_status_t m_iref_r(void *ctx, uint8_t index, uint8_t *value)
{
    fake_t *f = ctx;
    nn2_status_t st;
    push(OP_READ_IREF, index, 0u, 0u);
    st = next_bus(f);
    if (st != NN2_OK) {
        return st;
    }
    TEST_ASSERT_TRUE(index < NN2_PORT_EMITTER_SAFE_IREF_COUNT);
    *value = f->iref[index];
    return NN2_OK;
}

static nn2_status_t m_ledout_r(void *ctx, uint8_t index, uint8_t *value)
{
    fake_t *f = ctx;
    nn2_status_t st;
    push(OP_READ_LEDOUT, index, 0u, 0u);
    st = next_bus(f);
    if (st != NN2_OK) {
        return st;
    }
    TEST_ASSERT_TRUE(index < NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT);
    *value = f->ledout[index];
    return NN2_OK;
}

static void m_error(void *ctx, nn2_boot_outcome_t outcome, nn2_status_t error)
{
    fake_t *f = ctx;
    f->errors++;
    f->error_outcome = outcome;
    f->error_status = error;
}

static void reset_fake(void)
{
    memset(&g, 0, sizeof g);
    g.fail_bus_on = -1;
    g.fail_low_on = -1;
}

static nn2_port_emitter_safe_t port(void)
{
    nn2_port_emitter_safe_t p;
    p.set_reset_asserted = m_reset;
    p.write_irefall = m_irefall;
    p.write_pwmall = m_pwmall;
    p.write_ledout = m_ledout_w;
    p.read_iref = m_iref_r;
    p.read_ledout = m_ledout_r;
    p.delay_ms = m_delay;
    p.ctx = &g;
    return p;
}

static nn2_status_t run(nn2_reset_reason_t reason, uint32_t spiram, nn2_app_safe_boot_result_t *out)
{
    nn2_port_emitter_safe_t p = port();
    nn2_app_safe_boot_input_t in;
    in.reset_reason = reason;
    in.spiram_bytes = spiram;
    g.watching = out;
    return nn2_app_safe_boot_run(&in, &p, m_error, &g, out);
}

static int count_op(int op)
{
    size_t i;
    int n = 0;
    for (i = 0u; i < g.n; i++) {
        if (g.ops[i].op == op) {
            n++;
        }
    }
    return n;
}

static void assert_success_trace(nn2_reset_reason_t reason)
{
    size_t i;
    size_t cursor = 0u;
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[cursor++].op);
    TEST_ASSERT_EQUAL_INT(OP_RESET_RELEASE, g.ops[cursor++].op);
    TEST_ASSERT_EQUAL_INT(OP_DELAY, g.ops[cursor].op);
    TEST_ASSERT_EQUAL_UINT32(NN2_APP_SAFE_BOOT_RELEASE_MS, g.ops[cursor].delay_ms);
    cursor++;
    TEST_ASSERT_EQUAL_INT(OP_WRITE_IREFALL, g.ops[cursor].op);
    TEST_ASSERT_EQUAL_UINT8(NN2_PORT_EMITTER_SAFE_REG_OFF, g.ops[cursor].value);
    cursor++;
    TEST_ASSERT_EQUAL_INT(OP_WRITE_PWMALL, g.ops[cursor].op);
    TEST_ASSERT_EQUAL_UINT8(NN2_PORT_EMITTER_SAFE_REG_OFF, g.ops[cursor].value);
    cursor++;
    for (i = 0u; i < NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(OP_WRITE_LEDOUT, g.ops[cursor].op);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, g.ops[cursor].index);
        TEST_ASSERT_EQUAL_UINT8(NN2_PORT_EMITTER_SAFE_REG_OFF, g.ops[cursor].value);
        cursor++;
    }
    for (i = 0u; i < NN2_PORT_EMITTER_SAFE_IREF_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(OP_READ_IREF, g.ops[cursor].op);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, g.ops[cursor].index);
        cursor++;
    }
    for (i = 0u; i < NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(OP_READ_LEDOUT, g.ops[cursor].op);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, g.ops[cursor].index);
        cursor++;
    }
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[cursor].op);
    cursor++;
    TEST_ASSERT_EQUAL_size_t(cursor, g.n);
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_PWMALL));
    TEST_ASSERT_EQUAL_INT(0, g.errors);
    (void)reason;
}

static void safe_boot_rejects_missing_hooks(void)
{
    nn2_port_emitter_safe_t p;
    nn2_app_safe_boot_input_t in;
    nn2_app_safe_boot_result_t out;
    reset_fake();
    p = port();
    in.reset_reason = NN2_RESET_POWERON;
    in.spiram_bytes = 0u;
    p.write_pwmall = NULL;
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_app_safe_boot_run(&in, &p, m_error, &g, &out));
    p = port();
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_app_safe_boot_run(NULL, &p, m_error, &g, &out));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_app_safe_boot_run(&in, &p, NULL, &g, &out));
    in.reset_reason = NN2_RESET_COUNT_;
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_app_safe_boot_run(&in, &p, m_error, &g, &out));
    TEST_ASSERT_EQUAL_INT(0, g.n);
    TEST_ASSERT_EQUAL_INT(0, g.errors);
}

static void safe_boot_reason_is_stored_before_hardware(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    TEST_ASSERT_EQUAL(NN2_OK, run(NN2_RESET_BROWNOUT, 0u, &out));
    TEST_ASSERT_EQUAL_INT(1, g.saw_first);
    TEST_ASSERT_EQUAL_INT(NN2_RESET_BROWNOUT, g.reason_at_first);
    TEST_ASSERT_EQUAL_INT(NN2_RESET_BROWNOUT, out.reset_reason);
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[0].op);
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", nn2_app_reset_reason_str(out.reset_reason));
}

static void safe_boot_success_order_single_attempt(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    TEST_ASSERT_EQUAL(NN2_OK, run(NN2_RESET_POWERON, 0u, &out));
    assert_success_trace(NN2_RESET_POWERON);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_OK, out.outcome);
    TEST_ASSERT_EQUAL_INT(NN2_OK, out.error);
    TEST_ASSERT_EQUAL_UINT32(NN2_APP_SAFE_BOOT_ATTEMPT_LIMIT, out.attempts);
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_RESET_RELEASE));
    TEST_ASSERT_EQUAL_INT(2, count_op(OP_RESET_LOW));
}

static void safe_boot_every_reset_reason_same_sequence(void)
{
    nn2_reset_reason_t reason;
    for (reason = NN2_RESET_UNKNOWN; reason < NN2_RESET_COUNT_; reason++) {
        nn2_app_safe_boot_result_t out;
        reset_fake();
        TEST_ASSERT_EQUAL(NN2_OK, run(reason, 0u, &out));
        assert_success_trace(reason);
        TEST_ASSERT_EQUAL_INT(reason, out.reset_reason);
        TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
        TEST_ASSERT_FALSE(out.emitter_enabled);
    }
}

static void safe_boot_readback_mismatch_is_faulted_boot(void)
{
    nn2_app_safe_boot_result_t out;
    size_t i;
    int iref_reads = 0;
    reset_fake();
    g.iref[10] = 0x01u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO, run(NN2_RESET_SW, 0u, &out));
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
    TEST_ASSERT_EQUAL_STRING("FAULTED (BOOT)", nn2_app_safe_boot_outcome_str(out.outcome));
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
    TEST_ASSERT_EQUAL_INT(1, g.errors);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, g.error_outcome);
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_PWMALL));
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[g.n - 1u].op);
    for (i = 0u; i < g.n; i++) {
        if (g.ops[i].op == OP_READ_IREF) {
            iref_reads++;
        }
    }
    TEST_ASSERT_EQUAL_INT(11, iref_reads);
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_READ_LEDOUT));
}

static void safe_boot_bus_failure_does_not_retry(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.fail_bus = NN2_ERR_TIMEOUT;
    g.fail_bus_on = 1;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT, run(NN2_RESET_TASK_WDT, 0u, &out));
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
    TEST_ASSERT_EQUAL_INT(NN2_ERR_TIMEOUT, out.error);
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_PWMALL));
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_WRITE_LEDOUT));
    TEST_ASSERT_EQUAL_INT(1, g.errors);
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[g.n - 1u].op);
}

static void safe_boot_read_timeout_does_not_retry(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.fail_bus = NN2_ERR_TIMEOUT;
    g.fail_bus_on = 8;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT, run(NN2_RESET_EXT, 0u, &out));
    TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_EQUAL_INT(6, count_op(OP_WRITE_LEDOUT));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_READ_IREF));
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
}

static void safe_boot_spiram_holds_reset(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE, run(NN2_RESET_POWERON, 1u, &out));
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED_SPIRAM, out.outcome);
    TEST_ASSERT_EQUAL_STRING("FAULTED (SPIRAM)", nn2_app_safe_boot_outcome_str(out.outcome));
    TEST_ASSERT_EQUAL_size_t(1u, g.n);
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[0].op);
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_RESET_RELEASE));
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_EQUAL_UINT32(0u, out.attempts);
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_INT(1, g.errors);
    TEST_ASSERT_EQUAL_INT(NN2_RESET_POWERON, out.reset_reason);
}

static void safe_boot_initial_low_failure_does_not_release(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.fail_low = NN2_ERR_IO;
    TEST_ASSERT_EQUAL(NN2_ERR_IO, run(NN2_RESET_PANIC, 0u, &out));
    TEST_ASSERT_EQUAL_size_t(1u, g.n);
    TEST_ASSERT_EQUAL_INT(OP_RESET_LOW, g.ops[0].op);
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_RESET_RELEASE));
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_FALSE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_UINT32(0u, out.attempts);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
    TEST_ASSERT_EQUAL_INT(1, g.errors);
}

static void safe_boot_release_failure_reasserts(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.fail_release = NN2_ERR_IO;
    TEST_ASSERT_EQUAL(NN2_ERR_IO, run(NN2_RESET_WDT, 0u, &out));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_RESET_RELEASE));
    TEST_ASSERT_EQUAL_INT(2, count_op(OP_RESET_LOW));
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_DELAY));
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_EQUAL_UINT32(0u, out.attempts);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
}

static void safe_boot_delay_failure_skips_writes(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.fail_delay = NN2_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT, run(NN2_RESET_DEEPSLEEP, 0u, &out));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_DELAY));
    TEST_ASSERT_EQUAL_INT(0, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
    TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
}

static void safe_boot_final_hold_failure_stays_faulted(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.fail_low = NN2_ERR_IO;
    g.fail_low_on = 1;
    TEST_ASSERT_EQUAL(NN2_ERR_IO, run(NN2_RESET_USB, 0u, &out));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_RESET_RELEASE));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_EQUAL_INT(24, count_op(OP_READ_IREF));
    TEST_ASSERT_EQUAL_INT(6, count_op(OP_READ_LEDOUT));
    TEST_ASSERT_EQUAL_INT(2, count_op(OP_RESET_LOW));
    TEST_ASSERT_FALSE(out.reset_held_low);
    TEST_ASSERT_FALSE(out.emitter_enabled);
    TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
    TEST_ASSERT_EQUAL_INT(1, g.errors);
}

static void safe_boot_allowed_ops_only(void)
{
    nn2_app_safe_boot_result_t out;
    size_t i;
    reset_fake();
    TEST_ASSERT_EQUAL(NN2_OK, run(NN2_RESET_JTAG, 0u, &out));
    for (i = 0u; i < g.n; i++) {
        TEST_ASSERT_TRUE(g.ops[i].op >= OP_RESET_LOW && g.ops[i].op <= OP_READ_LEDOUT);
    }
    TEST_ASSERT_FALSE(out.emitter_enabled);
}

static void safe_boot_reason_strings_cover_the_map(void)
{
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", nn2_app_reset_reason_str(NN2_RESET_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("POWERON", nn2_app_reset_reason_str(NN2_RESET_POWERON));
    TEST_ASSERT_EQUAL_STRING("EXT", nn2_app_reset_reason_str(NN2_RESET_EXT));
    TEST_ASSERT_EQUAL_STRING("SW", nn2_app_reset_reason_str(NN2_RESET_SW));
    TEST_ASSERT_EQUAL_STRING("PANIC", nn2_app_reset_reason_str(NN2_RESET_PANIC));
    TEST_ASSERT_EQUAL_STRING("INT_WDT", nn2_app_reset_reason_str(NN2_RESET_INT_WDT));
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", nn2_app_reset_reason_str(NN2_RESET_TASK_WDT));
    TEST_ASSERT_EQUAL_STRING("WDT", nn2_app_reset_reason_str(NN2_RESET_WDT));
    TEST_ASSERT_EQUAL_STRING("DEEPSLEEP", nn2_app_reset_reason_str(NN2_RESET_DEEPSLEEP));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", nn2_app_reset_reason_str(NN2_RESET_BROWNOUT));
    TEST_ASSERT_EQUAL_STRING("SDIO", nn2_app_reset_reason_str(NN2_RESET_SDIO));
    TEST_ASSERT_EQUAL_STRING("USB", nn2_app_reset_reason_str(NN2_RESET_USB));
    TEST_ASSERT_EQUAL_STRING("JTAG", nn2_app_reset_reason_str(NN2_RESET_JTAG));
    TEST_ASSERT_EQUAL_STRING("EFUSE", nn2_app_reset_reason_str(NN2_RESET_EFUSE));
    TEST_ASSERT_EQUAL_STRING("PWR_GLITCH", nn2_app_reset_reason_str(NN2_RESET_PWR_GLITCH));
    TEST_ASSERT_EQUAL_STRING("CPU_LOCKUP", nn2_app_reset_reason_str(NN2_RESET_CPU_LOCKUP));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", nn2_app_reset_reason_str(NN2_RESET_COUNT_));
    TEST_ASSERT_EQUAL_STRING("OK", nn2_app_safe_boot_outcome_str(NN2_BOOT_OK));
    TEST_ASSERT_EQUAL_STRING("FAULTED (BOOT)", nn2_app_safe_boot_outcome_str(NN2_BOOT_FAULTED));
    TEST_ASSERT_EQUAL_STRING("FAULTED (SPIRAM)", nn2_app_safe_boot_outcome_str(NN2_BOOT_FAULTED_SPIRAM));
}

static void safe_boot_issue_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x40u, NN2_PORT_EMITTER_SAFE_REG_IREFALL);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, NN2_PORT_EMITTER_SAFE_REG_PWMALL);
    TEST_ASSERT_EQUAL_HEX8(0x00u, NN2_PORT_EMITTER_SAFE_REG_OFF);
    TEST_ASSERT_EQUAL_UINT(2u, NN2_APP_SAFE_BOOT_RELEASE_MS);
    TEST_ASSERT_TRUE(NN2_APP_SAFE_BOOT_RELEASE_MS * 10u >= NN2_APP_SAFE_BOOT_RELEASE_MIN_TENTHS_MS);
    TEST_ASSERT_EQUAL_UINT(1u, NN2_APP_SAFE_BOOT_ATTEMPT_LIMIT);
    TEST_ASSERT_EQUAL_INT(18, NN2_RESET_LED_GPIO);
    TEST_ASSERT_EQUAL_UINT(6u, NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT);
    TEST_ASSERT_EQUAL_UINT(24u, NN2_PORT_EMITTER_SAFE_IREF_COUNT);
}

static void safe_boot_ledout_mismatch_after_iref(void)
{
    nn2_app_safe_boot_result_t out;
    reset_fake();
    g.ledout[5] = 0x02u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO, run(NN2_RESET_CPU_LOCKUP, 0u, &out));
    TEST_ASSERT_EQUAL_INT(24, count_op(OP_READ_IREF));
    TEST_ASSERT_EQUAL_INT(6, count_op(OP_READ_LEDOUT));
    TEST_ASSERT_EQUAL_INT(1, count_op(OP_WRITE_IREFALL));
    TEST_ASSERT_TRUE(out.reset_held_low);
    TEST_ASSERT_EQUAL_INT(NN2_BOOT_FAULTED, out.outcome);
    TEST_ASSERT_EQUAL_UINT32(1u, out.attempts);
}

void test_safe_boot_suite(void)
{
    RUN_TEST(safe_boot_rejects_missing_hooks);
    RUN_TEST(safe_boot_reason_is_stored_before_hardware);
    RUN_TEST(safe_boot_success_order_single_attempt);
    RUN_TEST(safe_boot_every_reset_reason_same_sequence);
    RUN_TEST(safe_boot_readback_mismatch_is_faulted_boot);
    RUN_TEST(safe_boot_bus_failure_does_not_retry);
    RUN_TEST(safe_boot_read_timeout_does_not_retry);
    RUN_TEST(safe_boot_spiram_holds_reset);
    RUN_TEST(safe_boot_initial_low_failure_does_not_release);
    RUN_TEST(safe_boot_release_failure_reasserts);
    RUN_TEST(safe_boot_delay_failure_skips_writes);
    RUN_TEST(safe_boot_final_hold_failure_stays_faulted);
    RUN_TEST(safe_boot_allowed_ops_only);
    RUN_TEST(safe_boot_reason_strings_cover_the_map);
    RUN_TEST(safe_boot_issue_constants);
    RUN_TEST(safe_boot_ledout_mismatch_after_iref);
}
