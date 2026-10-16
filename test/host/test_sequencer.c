#include "nn2_test.h"
#include "nn2_app_sequencer.h"
#include <string.h>

/* Mock ports: record call order, inject failures. */
typedef struct {
    char trace[128];
    size_t n;
    int led_on, led_off, adc_reads, logs, delays;
    nn2_status_t fail_on, fail_adc, fail_off;
    uint64_t now;
    uint32_t adc_value;
    int led_lit;
} mock_t;

static void tr(mock_t *m, char c) { if (m->n + 1 < sizeof(m->trace)) { m->trace[m->n++] = c; m->trace[m->n] = 0; } }

static nn2_status_t m_on(void *c, uint8_t ch) { mock_t *m = c; (void)ch; tr(m, 'N'); m->led_on++;
    if (m->fail_on != NN2_OK) { return m->fail_on; }
    m->led_lit = 1;
    return NN2_OK;
}
static nn2_status_t m_off(void *c, uint8_t ch) { mock_t *m = c; (void)ch; tr(m, 'F'); m->led_off++;
    if (m->fail_off != NN2_OK) { return m->fail_off; } /* failed off: LED stays as it was */
    m->led_lit = 0; return NN2_OK; }
static nn2_status_t m_adc(void *c, uint8_t ch, uint32_t *raw) { mock_t *m = c; (void)ch; tr(m, 'A'); m->adc_reads++;
    if (m->fail_adc != NN2_OK) { return m->fail_adc; }
    *raw = m->adc_value++;
    return NN2_OK;
}
static uint64_t m_now(void *c) { mock_t *m = c; m->now += 10u; return m->now; }
static void m_delay(void *c, uint32_t us) { mock_t *m = c; tr(m, 'D'); m->delays++; m->now += us; }
static void m_log(void *c, nn2_log_level_t l, const char *msg) { mock_t *m = c; (void)l; (void)msg; m->logs++; }

static nn2_core_ring_t g_ring;

static void setup(mock_t *m, nn2_app_sequencer_t *s)
{
    memset(m, 0, sizeof(*m));
    m->adc_value = 100u;
    (void)nn2_core_ring_init(&g_ring);
    nn2_app_seq_config_t cfg = {
        .led = { m_on, m_off, m },
        .adc = { m_adc, m, NN2_SAMPLE_SRC_FAKE },
        .clock = { m_now, m_delay, m },
        .log = { m_log, m },
        .ring = &g_ring,
        .channel = 0,
        .settle_us = 500u,
    };
    (void)nn2_app_sequencer_init(s, &cfg);
}

static void seq_init_rejects_missing_ports(void)
{
    nn2_app_sequencer_t s;
    nn2_app_seq_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    NN2_CHECK(nn2_app_sequencer_init(&s, &cfg) == NN2_ERR_INVALID_ARG);
    NN2_CHECK(nn2_app_sequencer_init(NULL, &cfg) == NN2_ERR_INVALID_ARG);
}

static void seq_step_order(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_IDLE);
    NN2_CHECK(nn2_app_sequencer_step(&s) == NN2_OK);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_LED_ON);
    NN2_CHECK(nn2_app_sequencer_step(&s) == NN2_OK);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_SAMPLE);
    NN2_CHECK(nn2_app_sequencer_step(&s) == NN2_OK);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_LED_OFF);
    NN2_CHECK(nn2_app_sequencer_step(&s) == NN2_OK);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_IDLE);
    NN2_CHECK(strcmp(m.trace, "NDAF") == 0);
}

static void seq_cycles_produce_samples(void)
{
    mock_t m; nn2_app_sequencer_t s; nn2_core_sample_t out; setup(&m, &s);
    for (int i = 0; i < 3; i++) NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_OK);
    NN2_CHECK(s.cycles_ok == 3u);
    NN2_CHECK(nn2_core_ring_count(&g_ring) == 3u);
    NN2_CHECK(m.led_lit == 0);
    for (uint32_t i = 0; i < 3u; i++) {
        NN2_CHECK(nn2_core_ring_pop(&g_ring, &out) == NN2_OK);
        NN2_CHECK(out.seq == i);
        NN2_CHECK(out.raw == 100u + i);
        NN2_CHECK(out.t_sample_us >= out.t_led_on_us + 500u);
    }
}

static void seq_adc_fail_turns_led_off(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    m.fail_adc = NN2_ERR_IO;
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_IO);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_FAULT);
    NN2_CHECK(m.led_lit == 0);
    NN2_CHECK(m.led_off == 1);
    NN2_CHECK(s.faults == 1u);
    NN2_CHECK(m.logs >= 1);
    NN2_CHECK(nn2_core_ring_count(&g_ring) == 0u);
    NN2_CHECK(nn2_app_sequencer_step(&s) == NN2_ERR_INVALID_STATE);
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_INVALID_STATE);
}

static void seq_led_on_fail_and_reset(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    m.fail_on = NN2_ERR_IO;
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_IO);
    NN2_CHECK(m.led_off == 1);
    NN2_CHECK(m.adc_reads == 0);
    NN2_CHECK(m.led_lit == 0);
    m.fail_on = NN2_OK;
    NN2_CHECK(nn2_app_sequencer_reset(&s) == NN2_OK);
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_OK);
    NN2_CHECK(nn2_core_ring_count(&g_ring) == 1u);
}

static void seq_led_off_fail_is_fault(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    m.fail_off = NN2_ERR_IO;
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_IO);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_FAULT);
    NN2_CHECK(nn2_core_ring_count(&g_ring) == 0u);
    /* step-off failed + fail-safe retry failed: exactly 2 off() calls, LED still lit */
    NN2_CHECK(m.led_off == 2);
    NN2_CHECK(m.led_lit == 1);
    NN2_CHECK(s.off_failures == 1u);
}

static void seq_reset_after_failed_off_stays_fault(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    m.fail_off = NN2_ERR_IO;
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_IO);
    NN2_CHECK(nn2_app_sequencer_reset(&s) == NN2_ERR_IO);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_FAULT);
    NN2_CHECK(s.last_error == NN2_ERR_IO);
    NN2_CHECK(m.led_off == 3);
    NN2_CHECK(m.led_lit == 1);
    NN2_CHECK(m.led_on == 1); /* no further on() while in FAULT */
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_INVALID_STATE);
    NN2_CHECK(m.led_on == 1);
}

static void seq_reset_success_clears_last_error(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    m.fail_adc = NN2_ERR_IO;
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_ERR_IO);
    NN2_CHECK(s.last_error == NN2_ERR_IO);
    NN2_CHECK(m.led_off == 1);
    m.fail_adc = NN2_OK;
    NN2_CHECK(nn2_app_sequencer_reset(&s) == NN2_OK);
    NN2_CHECK(m.led_off == 2);
    NN2_CHECK(nn2_app_sequencer_state(&s) == NN2_APP_SEQ_IDLE);
    NN2_CHECK(s.last_error == NN2_OK);
    NN2_CHECK(m.led_lit == 0);
}

static void seq_sample_carries_fake_provenance(void)
{
    mock_t m; nn2_app_sequencer_t s; nn2_core_sample_t out; setup(&m, &s);
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_OK);
    NN2_CHECK(nn2_core_ring_pop(&g_ring, &out) == NN2_OK);
    NN2_CHECK(out.src == NN2_SAMPLE_SRC_FAKE);
    NN2_CHECK(nn2_core_sample_src_str(out.src)[0] == 'F');
}

static void seq_zero_init_provenance_is_unknown(void)
{
    nn2_core_sample_t z; nn2_port_adc_t a;
    memset(&z, 0, sizeof(z)); memset(&a, 0, sizeof(a));
    NN2_CHECK(z.src == NN2_SAMPLE_SRC_UNKNOWN);
    NN2_CHECK(a.source == NN2_SAMPLE_SRC_UNKNOWN);
    NN2_CHECK(NN2_SAMPLE_SRC_UNKNOWN == 0);
    mock_t m; nn2_app_sequencer_t s; nn2_core_sample_t out; setup(&m, &s);
    s.cfg.adc.source = NN2_SAMPLE_SRC_UNKNOWN; /* port that declares nothing */
    NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_OK);
    NN2_CHECK(nn2_core_ring_pop(&g_ring, &out) == NN2_OK);
    NN2_CHECK(out.src == NN2_SAMPLE_SRC_UNKNOWN);
}

static void seq_ring_full_drops_not_fault(void)
{
    mock_t m; nn2_app_sequencer_t s; setup(&m, &s);
    for (uint32_t i = 0; i < NN2_CORE_RING_CAPACITY + 2u; i++)
        NN2_CHECK(nn2_app_sequencer_run_cycle(&s) == NN2_OK);
    NN2_CHECK(g_ring.dropped == 2u);
    NN2_CHECK(s.faults == 0u);
}

void test_sequencer_suite(void)
{
    NN2_RUN(seq_init_rejects_missing_ports);
    NN2_RUN(seq_step_order);
    NN2_RUN(seq_cycles_produce_samples);
    NN2_RUN(seq_adc_fail_turns_led_off);
    NN2_RUN(seq_led_on_fail_and_reset);
    NN2_RUN(seq_led_off_fail_is_fault);
    NN2_RUN(seq_ring_full_drops_not_fault);
    NN2_RUN(seq_reset_after_failed_off_stays_fault);
    NN2_RUN(seq_reset_success_clears_last_error);
    NN2_RUN(seq_sample_carries_fake_provenance);
    NN2_RUN(seq_zero_init_provenance_is_unknown);
}
