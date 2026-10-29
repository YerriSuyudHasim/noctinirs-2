/* Adapted int24/CRC intent from PR36 f66fbcf; protocol and owner logic replaced.
 * TI SBAS890D: see docs/ISSUE35_DIAGNOSTIC_CONTRACT.md for pages/hash. */
#include "nn2_ads131_logic.h"
#include <string.h>
uint16_t nn2_ads131_crc(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (unsigned j = 0; j < 8u; ++j)
            crc = (uint16_t)((crc & 0x8000u) ? ((uint32_t)crc << 1) ^ 0x1021u : (uint32_t)crc << 1);
    }
    return crc;
}
int32_t nn2_ads131_sext24(uint32_t value)
{
    value &= 0xffffffu;
    return (value & 0x800000u) ? (int32_t)(value & 0x7fffffu) - 8388608 : (int32_t)value;
}
static uint16_t word(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static nn2_status_t integrity(const uint8_t *raw)
{
    return raw[2] == 0u && raw[17] == 0u && nn2_ads131_crc(raw, 15u) == word(raw + 15)
           ? NN2_OK : NN2_ERR_IO;
}
static nn2_status_t decode_checked(const uint8_t raw[NN2_ADS131_BYTES], uint64_t time,
                                   nn2_core_adc_record_t *out, bool require_all_fresh)
{
    nn2_core_adc_record_t result = {0};
    result.status = word(raw);
    /* V4 wire-status acceptance. The public parser retains strict freshness;
     * production records carry continuity/freshness uncertainty in quality. */
    if ((result.status & 0xff00u) != 0x0100u ||
        (require_all_fresh && result.status != 0x010fu)) return NN2_ERR_IO;
    for (size_t ch = 0; ch < 4u; ++ch) {
        const uint8_t *p = raw + 3u + ch * 3u;
        result.channel[ch] = nn2_ads131_sext24(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
    }
    result.crc_received = word(raw + 15);
    result.read_complete_us = time;
    result.source = NN2_SAMPLE_SRC_ADC;
    *out = result;
    return NN2_OK;
}
nn2_status_t nn2_ads131_parse(const uint8_t raw[NN2_ADS131_BYTES], uint64_t time,
                            nn2_core_adc_record_t *out)
{
    if (!raw || !out) return NN2_ERR_INVALID_ARG;
    if (integrity(raw) != NN2_OK) return NN2_ERR_IO;
    return decode_checked(raw, time, out, true);
}
nn2_status_t nn2_ads131_validate(const nn2_ads131_config_t *cfg)
{
    return cfg && cfg->clkin_hz == 8192000u && cfg->osr == 512u &&
           cfg->rate_hz == 8000u && cfg->spi_hz > 0u && cfg->spi_hz <= 8000000u
           ? NN2_OK : NN2_ERR_INVALID_ARG;
}
void nn2_ads131_logic_bind(nn2_ads131_logic_t *d, nn2_ads131_io_t io)
{
    memset(d, 0, sizeof(*d)); d->io = io;
}
nn2_status_t nn2_ads131_fail(nn2_ads131_logic_t *d, nn2_core_adc_fault_t reason)
{
    bool runtime_capture = d->observation.state == NN2_ADC_RUNNING && !d->runtime_failure.valid;
    if (runtime_capture) {
        d->runtime_failure.valid = true; d->runtime_failure.fault = reason;
        if (d->runtime_failure.detail == NN2_ADS131_RUNTIME_UNSPECIFIED)
            d->runtime_failure.at_us = d->io.now_us(d->io.ctx);
        d->runtime_failure.last_service_us = d->last_service_us;
    }
    bool capture = d->startup_stage != NN2_ADS131_STAGE_NONE && !d->startup_failure.valid;
    if (capture) {
        d->startup_failure.valid = true;
        d->startup_failure.fault = reason;
        if (d->startup_failure.result == NN2_OK) d->startup_failure.result = NN2_ERR_IO;
    }
    d->observation.state = NN2_ADC_FAULT;
    d->observation.fault = reason;
    d->quality |= NN2_ADC_QUALITY_GAP;
    nn2_status_t cleanup = d->io.reset(d->io.ctx, false);
    if (capture) d->startup_failure.cleanup_result = cleanup;
    if (runtime_capture) d->runtime_failure.cleanup_result = cleanup;
    if (cleanup != NN2_OK) d->observation.fault = NN2_ADC_FAULT_RESET;
    d->startup_stage = NN2_ADS131_STAGE_NONE;
    return NN2_ERR_IO;
}
static void startup_step(nn2_ads131_logic_t *d, nn2_ads131_stage_t stage, unsigned address)
{
    d->startup_stage = stage;
    if (!d->startup_failure.valid) {
        memset(&d->startup_failure, 0, sizeof(d->startup_failure));
        d->startup_failure.stage = stage;
        d->startup_failure.address = (uint8_t)address;
    }
}
static nn2_status_t exchange(nn2_ads131_logic_t *d, uint16_t cmd, uint16_t data, uint8_t *rx)
{
    uint8_t tx[NN2_ADS131_BYTES] = {0};
    tx[0] = (uint8_t)(cmd >> 8); tx[1] = (uint8_t)cmd;
    tx[3] = (uint8_t)(data >> 8); tx[4] = (uint8_t)data;
    bool capture = d->startup_stage != NN2_ADS131_STAGE_NONE && !d->startup_failure.valid;
    nn2_ads131_startup_failure_t *f = &d->startup_failure;
    if (capture) {
        f->transaction = ++d->startup_transaction;
        f->command = cmd; f->previous_command = d->previous_command;
        f->tx_valid = true; memcpy(f->tx, tx, sizeof(f->tx));
    }
    nn2_status_t result = d->io.transfer(d->io.ctx, tx, rx);
    d->previous_command = cmd;
    if (capture) {
        f->result = result; f->rx_valid = result == NN2_OK;
        if (f->rx_valid) {
            memcpy(f->rx, rx, sizeof(f->rx));
            f->crc_calculated = nn2_ads131_crc(rx, 15u); f->crc_received = word(rx + 15);
            f->crc_match = f->crc_calculated == f->crc_received;
            f->response_padding = rx[2]; f->crc_padding = rx[17];
        }
    }
    if (result != NN2_OK) {
        d->observation.spi_errors++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_SPI);
    }
    if (integrity(rx) != NN2_OK) {
        d->observation.crc_bad++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_CRC);
    }
    return NN2_OK;
}
static nn2_status_t read_reg(nn2_ads131_logic_t *d, unsigned address, uint16_t *out)
{
    uint8_t rx[NN2_ADS131_BYTES];
    startup_step(d, NN2_ADS131_STAGE_RREG, address);
    nn2_status_t s = exchange(d, (uint16_t)(0xa000u | (address << 7)), 0u, rx);
    if (s == NN2_OK) {
        startup_step(d, NN2_ADS131_STAGE_RREG_RESPONSE, address);
        s = exchange(d, 0u, 0u, rx);
    }
    if (s == NN2_OK) *out = word(rx);
    return s;
}
static nn2_status_t program(nn2_ads131_logic_t *d, unsigned address, uint16_t value, uint16_t mask)
{
    uint8_t rx[NN2_ADS131_BYTES]; uint16_t actual;
    startup_step(d, NN2_ADS131_STAGE_WREG, address);
    nn2_status_t s = exchange(d, (uint16_t)(0x6000u | (address << 7)), value, rx);
    if (s == NN2_OK) {
        startup_step(d, NN2_ADS131_STAGE_WREG_RESPONSE, address);
        s = exchange(d, 0u, 0u, rx);
    }
    if (s == NN2_OK && word(rx) != (uint16_t)(0x4000u | (address << 7)))
        return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
    if (s == NN2_OK) s = read_reg(d, address, &actual);
    if (s == NN2_OK && (actual & mask) != (value & mask))
        return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
    return s;
}
nn2_status_t nn2_ads131_initialize(nn2_ads131_logic_t *d, const nn2_ads131_config_t *cfg)
{
    uint16_t id; nn2_status_t s;
    if (!d || nn2_ads131_validate(cfg) != NN2_OK) return NN2_ERR_INVALID_ARG;
    if (d->observation.state == NN2_ADC_RUNNING) return NN2_ERR_INVALID_STATE;
    if (!d->io.transfer || !d->io.reset || !d->io.sync || !d->io.delay_us || !d->io.wait_ready || !d->io.now_us)
        return NN2_ERR_INVALID_ARG;
    d->observation.lost += d->count; d->head = 0u; d->count = 0u;
    d->observation.occupancy = 0u;
    d->startup_transaction = 0u; d->previous_command = 0u;
    startup_step(d, NN2_ADS131_STAGE_RESET_LOW, 0xffu);
    if (d->io.reset(d->io.ctx, false) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->io.delay_us(d->io.ctx, 300u);
    startup_step(d, NN2_ADS131_STAGE_RESET_HIGH, 0xffu);
    if (d->io.reset(d->io.ctx, true) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->io.delay_us(d->io.ctx, 20u);
    uint8_t rx[NN2_ADS131_BYTES];
    startup_step(d, NN2_ADS131_STAGE_RESET_ACK, 0xffu);
    s = exchange(d, 0u, 0u, rx);
    if (s == NN2_OK && word(rx) != 0xff24u) return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
    if (s == NN2_OK) {
        startup_step(d, NN2_ADS131_STAGE_UNLOCK, 0xffu);
        s = exchange(d, 0x0655u, 0u, rx);
    }
    if (s == NN2_OK) s = read_reg(d, 0u, &id);
    if (s == NN2_OK && (id & 0xff00u) != 0x2400u) return nn2_ads131_fail(d, NN2_ADC_FAULT_ID);
    if (s == NN2_OK) s = program(d, 2u, 0x0111u, 0x3f1fu);
    if (s == NN2_OK) s = program(d, 3u, 0x0f0au, 0x0f3fu);
    if (s == NN2_OK) s = program(d, 4u, 0u, 0x7777u);
    if (s == NN2_OK) s = program(d, 6u, 0x0600u, 0xffffu);
    /* Explicit V4 channel defaults: zero phase, DC block enabled, normal mux.
     * Do not write factory offset/gain calibration registers. */
    for (unsigned ch = 0u; s == NN2_OK && ch < 4u; ++ch)
        s = program(d, 9u + ch * 5u, 0u, 0xffffu);
    /* V4 verifies reset-loaded calibration without overwriting factory data. */
    for (unsigned ch = 0u; s == NN2_OK && ch < 4u; ++ch) {
        const uint16_t expected[4] = {0u, 0u, 0x8000u, 0u};
        for (unsigned field = 0u; s == NN2_OK && field < 4u; ++field) {
            uint16_t actual;
            s = read_reg(d, 10u + ch * 5u + field, &actual);
            if (s == NN2_OK && (actual & ((field & 1u) ? 0xff00u : 0xffffu)) != expected[field])
                return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
        }
    }
    if (s == NN2_OK) {
        startup_step(d, NN2_ADS131_STAGE_READY, 0xffu);
        nn2_status_t ready = d->io.wait_ready(d->io.ctx, 20u);
        if (ready != NN2_OK) {
            if (!d->startup_failure.valid) d->startup_failure.result = ready;
            d->observation.drdy_timeouts++;
            return nn2_ads131_fail(d, NN2_ADC_FAULT_DRDY);
        }
    }
    if (s == NN2_OK) {
        d->observation.state = NN2_ADC_READY; d->observation.fault = NN2_ADC_FAULT_NONE;
        d->observation.session++; d->quality = 0u;
        d->startup_stage = NN2_ADS131_STAGE_NONE;
    }
    return s;
}
nn2_status_t nn2_ads131_start(nn2_ads131_logic_t *d)
{
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->observation.state != NN2_ADC_READY) return NN2_ERR_INVALID_STATE;
    /* Baseline before SYNC opens ISR capture; an early first edge must not be
     * mistaken for the pre-session baseline when the task resumes. */
    if (d->io.drdy_latch) (void)d->io.drdy_latch(d->io.ctx, &d->edge_serial);
    startup_step(d, NN2_ADS131_STAGE_SYNC, 0xffu);
    if (d->io.sync(d->io.ctx) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->startup_stage = NN2_ADS131_STAGE_NONE;
    d->previous_edge_us = 0u;
    memset(&d->rate, 0, sizeof(d->rate));
    memset(&d->first_unknown, 0, sizeof(d->first_unknown));
    d->latency_seen = d->latency_count = 0u;
    if (!d->runtime_failure.valid) d->trace_head = 0u;
    d->last_service_us = d->io.now_us(d->io.ctx); d->settling = 3u; d->expected_resync = 1u;
    d->observation.state = NN2_ADC_RUNNING; return NN2_OK;
}
nn2_status_t nn2_ads131_stop(nn2_ads131_logic_t *d)
{
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->io.reset(d->io.ctx, false) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->observation.state = NN2_ADC_STOPPED; return NN2_OK;
}
/* Preserve the decision timestamp, before cleanup or fault logging adds latency. */
static nn2_status_t runtime_gap(nn2_ads131_logic_t *d,
                                nn2_ads131_runtime_detail_t detail, uint64_t now)
{
    if (!d->runtime_failure.valid) {
        d->runtime_failure.detail = (uint8_t)detail;
        d->runtime_failure.at_us = now;
    }
    return nn2_ads131_fail(d, NN2_ADC_FAULT_GAP);
}
void nn2_ads131_rate_begin(nn2_ads131_logic_t *d)
{
    memset(&d->rate, 0, sizeof(d->rate));
    d->rate.active = true;
    d->rate.min_period_us = UINT32_MAX;
}
static nn2_status_t rate_sample(nn2_ads131_logic_t *d, uint64_t edge)
{
    nn2_ads131_rate_t *r = &d->rate;
    if (!r->samples) r->first_us = edge;
    else {
        if (edge <= r->last_us) return runtime_gap(d, NN2_ADS131_RUNTIME_RATE, d->io.now_us(d->io.ctx));
        uint64_t period = edge - r->last_us;
        if (period < r->min_period_us) r->min_period_us = (uint32_t)period;
        if (period > r->max_period_us) r->max_period_us = (uint32_t)period;
        /* V4 defines 188 us as an abnormal 8-kSPS period threshold. Keep a
         * separate diagnostic count; mean-rate acceptance alone cannot prove
         * every physical conversion generated a captured GPIO interrupt. */
        if (period > 188u) ++r->period_gaps;
    }
    r->last_us = edge;
    if (++r->samples < NN2_ADS131_RATE_SAMPLES) return NN2_OK;
    uint64_t elapsed = edge - r->first_us;
    r->hz = elapsed ? (uint32_t)(((NN2_ADS131_RATE_SAMPLES - 1ULL) * 1000000u + elapsed / 2u) / elapsed) : 0u;
    r->active = false;
    /* V4 acceptance: mean rate +/-5%, valid CRC/status/sequence/backlog.
     * Period outliers remain visible; mean rate does not prove no physical loss. */
    r->valid = r->hz >= 7600u && r->hz <= 8400u;
    if (!r->valid) {
        d->observation.loss_unknown++;
        return runtime_gap(d, NN2_ADS131_RUNTIME_RATE, d->io.now_us(d->io.ctx));
    }
    return NN2_OK;
}
/* Informational thresholds only: no changes to acceptance or fault policy. */
static void capture_latency(nn2_ads131_logic_t *d, const nn2_ads131_trace_t *t)
{
    if (!t) return;
    unsigned reasons = (t->period_us > 188u ? NN2_ADS131_LATENCY_PERIOD : 0u) |
        ((t->status & 0x000fu) != 0x000fu ? NN2_ADS131_LATENCY_FRESHNESS : 0u) |
        (t->spi_begin_us >= t->edge_us && t->spi_begin_us - t->edge_us > 100u
            ? NN2_ADS131_LATENCY_START_LATE : 0u) |
        (t->spi_begin_us >= t->event_pop_us && t->spi_begin_us - t->event_pop_us > 30u
            ? NN2_ADS131_LATENCY_SERVICE_WORK : 0u) |
        (t->spi_end_us >= t->spi_begin_us && t->spi_end_us - t->spi_begin_us > 80u
            ? NN2_ADS131_LATENCY_TRANSPORT : 0u);
    if (!reasons) return;
    ++d->latency_seen;
    if (d->latency_count == NN2_ADS131_LATENCY_CAPACITY) return;
    nn2_ads131_latency_event_t *event = &d->latency[d->latency_count++];
    event->reasons = (uint8_t)reasons;
    event->previous_valid = d->trace_head > 1u;
    if (event->previous_valid)
        event->previous = d->trace[(d->trace_head - 2u) % NN2_ADS131_TRACE_CAPACITY];
    event->current = *t;
}
nn2_status_t nn2_ads131_service(nn2_ads131_logic_t *d, uint32_t notifications)
{
    uint8_t rx[NN2_ADS131_BYTES]; nn2_core_adc_record_t frame;
    uint64_t now, edge = 0u;
    uint32_t serial = 0u;
    uint32_t dropped = 0u, isr_publish_us = 0u;
    uint64_t service_entry_us;
    nn2_ads131_trace_t *trace = NULL;
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->observation.state != NN2_ADC_RUNNING) return NN2_ERR_INVALID_STATE;
    service_entry_us = d->io.now_us(d->io.ctx);
    if (d->io.take_events) {
        nn2_ads131_event_t event;
        notifications = d->io.take_events(d->io.ctx, &event, &dropped);
        edge = event.time_us; serial = event.serial; isr_publish_us = event.isr_publish_us;
    }
    /* An ISR can append an event during take_events(). Sample time after
     * taking the snapshot, otherwise a legitimate new edge looks future-dated. */
    now = d->io.now_us(d->io.ctx);
    if (!d->runtime_failure.valid) {
        d->runtime_failure.detail = NN2_ADS131_RUNTIME_UNSPECIFIED;
        d->runtime_failure.spi_elapsed_us = 0u;
        d->runtime_failure.pending = notifications; d->runtime_failure.dropped = dropped;
        d->runtime_failure.expected_serial = d->edge_serial + 1u;
        d->runtime_failure.event_serial = serial; d->runtime_failure.edge_us = edge;
        d->runtime_failure.observed_serial = serial;
        if (d->io.take_events && d->io.drdy_latch)
            (void)d->io.drdy_latch(d->io.ctx, &d->runtime_failure.observed_serial);
    }
    if (notifications == 0u) {
        uint64_t reference = d->last_service_us;
        uint64_t timeout_us = 30000u;
        if (d->io.take_events && d->io.drdy_latch) {
            uint32_t latest_serial;
            uint64_t latest = d->io.drdy_latch(d->io.ctx, &latest_serial);
            (void)latest_serial;
            if (latest) reference = latest;
            timeout_us = 5000u; /* V4 DRDY stall watchdog, separate from latency. */
        }
        if (now < reference || (d->io.take_events ? now - reference <= timeout_us
                                                   : now - reference < timeout_us)) return NN2_OK;
        d->observation.drdy_timeouts++; d->observation.loss_unknown++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_DRDY);
    }
    if (!d->runtime_failure.valid) {
        trace = &d->trace[d->trace_head++ % NN2_ADS131_TRACE_CAPACITY];
        memset(trace, 0, sizeof(*trace));
        trace->serial = serial; trace->edge_us = edge; trace->pending = notifications;
        trace->owner_resume_us = d->owner_resume_us;
        trace->service_entry_us = service_entry_us; trace->event_pop_us = now;
        trace->isr_publish_us = isr_publish_us;
        if (edge && d->previous_edge_us && edge > d->previous_edge_us) {
            uint64_t period = edge - d->previous_edge_us;
            trace->period_us = period > UINT32_MAX ? UINT32_MAX : (uint32_t)period;
        }
    }
    const bool recorded = d->io.take_events != NULL;
    const uint32_t max_pending = recorded ? 2u : 1u;
    if (dropped || notifications > max_pending ||
        (!recorded && d->settling == 0u && now - d->last_service_us > 250u)) {
        d->observation.sequence += notifications;
        d->observation.adc_missed += notifications - 1u;
        d->observation.lost += notifications; d->observation.loss_unknown++;
        return runtime_gap(d, dropped || notifications > max_pending ? NN2_ADS131_RUNTIME_BACKLOG
                           : NN2_ADS131_RUNTIME_SERVICE_GAP, now);
    }
    /* One event maps to one read, including a bounded two-frame FIFO backlog.
     * TI SBAS890D 8.5.1.9.1; historical V4 process_event uses the same bound. */
    d->observation.sequence++;
    const uint64_t max_age_us = recorded ? 250u : 125u;
    if (d->io.drdy_latch) {
        uint32_t latest_serial;
        uint64_t latest_edge = d->io.drdy_latch(d->io.ctx, &latest_serial);
        now = d->io.now_us(d->io.ctx);
        if (!d->io.take_events) { edge = latest_edge; serial = latest_serial; }
        if (!d->runtime_failure.valid) {
            d->runtime_failure.event_serial = serial; d->runtime_failure.observed_serial = latest_serial;
            d->runtime_failure.edge_us = edge;
        }
        if (!edge || (!recorded && latest_serial != serial) ||
            serial - d->edge_serial != 1u || now < edge || (!recorded && now - edge >= max_age_us)) {
            d->observation.lost++; d->observation.loss_unknown++;
            nn2_ads131_runtime_detail_t detail = !edge ? NN2_ADS131_RUNTIME_EDGE_MISSING
                : ((!recorded && latest_serial != serial) || serial - d->edge_serial != 1u)
                    ? NN2_ADS131_RUNTIME_SERIAL_BEFORE
                : now < edge ? NN2_ADS131_RUNTIME_EDGE_FUTURE
                : NN2_ADS131_RUNTIME_AGE_BEFORE;
            return runtime_gap(d, detail, now);
        }
    }
    /* Capture transport completion and serial BEFORE CRC/decoding. A new DRDY
     * during CPU validation is queued work, not proof that this read was lost. */
    const uint8_t tx[NN2_ADS131_BYTES] = {0};
    uint64_t spi_begin = d->io.now_us(d->io.ctx);
    nn2_status_t transferred = d->io.transfer(d->io.ctx, tx, rx);
    uint64_t spi_end = d->io.now_us(d->io.ctx);
    if (trace) {
        trace->spi_begin_us = spi_begin; trace->spi_end_us = spi_end;
        trace->phase = 1u;
        if (transferred == NN2_OK) trace->status = word(rx);
    }
    uint32_t after_serial = serial;
    if (d->io.drdy_latch) (void)d->io.drdy_latch(d->io.ctx, &after_serial);
    now = d->io.now_us(d->io.ctx);
    d->previous_command = 0u;
    if (!d->runtime_failure.valid) {
        uint64_t elapsed = spi_end - spi_begin;
        d->runtime_failure.spi_elapsed_us = elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed;
        d->runtime_failure.observed_serial = after_serial;
    }
    if (transferred != NN2_OK) {
        d->observation.spi_errors++; d->observation.lost++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_SPI);
    }
    uint64_t crc_begin = d->io.now_us(d->io.ctx);
    nn2_status_t checked = integrity(rx);
    uint64_t crc_end = d->io.now_us(d->io.ctx);
    if (trace) {
        uint64_t elapsed = crc_end - crc_begin;
        trace->crc_us = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
        trace->phase = 2u;
    }
    if (checked != NN2_OK) {
        d->observation.crc_bad++; d->observation.lost++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_CRC);
    }
    if (d->io.drdy_latch) {
        /* V4 checks queued backlog/sequence before each read, not a blanket
         * end-of-transfer serial/age rule. Legacy count-only tests stay strict. */
        if (!recorded && (after_serial != serial || now - edge >= max_age_us)) {
            d->observation.lost++; d->observation.loss_unknown++;
            return runtime_gap(d, after_serial - serial > (recorded ? 1u : 0u)
                ? NN2_ADS131_RUNTIME_SERIAL_AFTER : NN2_ADS131_RUNTIME_AGE_AFTER, now);
        }
        d->edge_serial = serial;
    }
    d->last_service_us = spi_end;
    d->previous_edge_us = edge;
    if (d->settling) {
        uint16_t status = word(rx);
        /* V4 permits one expected post-SYNC resynchronization indication,
         * only while discarding; CRC and hard status faults remain fatal. */
        if ((status & 0xbf00u) != 0x0100u ||
            ((status & 0x4000u) && !d->expected_resync)) {
            d->observation.lost++;
            return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
        }
        if (status & 0x4000u) --d->expected_resync;
        if (--d->settling == 0u) d->expected_resync = 0u;
        return NN2_OK;
    }
    /* Integrity was checked once above; public parse still validates its input. */
    if (decode_checked(rx, spi_end, &frame, !recorded) != NN2_OK) {
        d->observation.lost++; return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
    }
    if (d->rate.active) return rate_sample(d, edge);
    if (recorded) capture_latency(d, trace);
    if (recorded && ((trace && trace->period_us > 188u) || (frame.status & 0x000fu) != 0x000fu)) {
        d->observation.loss_unknown++;
        d->quality |= NN2_ADC_QUALITY_GAP;
        if (!d->first_unknown.valid) {
            nn2_ads131_unknown_t *u = &d->first_unknown;
            u->reasons = (trace && trace->period_us > 188u ? NN2_ADS131_UNKNOWN_PERIOD : 0u) |
                ((frame.status & 0x000fu) != 0x000fu ? NN2_ADS131_UNKNOWN_FRESHNESS : 0u);
            u->session = d->observation.session;
            u->observed_serial = after_serial;
            u->checked_us = crc_end;
            if (trace) {
                u->current = *trace;
                if (d->trace_head > 1u) {
                    u->previous = d->trace[(d->trace_head - 2u) % NN2_ADS131_TRACE_CAPACITY];
                    u->previous_valid = true;
                }
            } else {
                /* First runtime fault history may already be frozen on a retry. */
                u->current = (nn2_ads131_trace_t){ .edge_us=edge, .spi_begin_us=spi_begin,
                    .spi_end_us=spi_end, .serial=serial, .pending=notifications,
                    .status=frame.status, .phase=2u,
                    .crc_us=(uint32_t)(crc_end-crc_begin) };
            }
            u->valid = true;
        }
    }
    frame.sequence = d->observation.sequence; frame.session = d->observation.session;
    frame.drdy_us = edge;
    frame.quality = d->quality | (d->io.drdy_latch ? 0u : NN2_ADC_QUALITY_NO_EDGE);
    if (d->io.publish) {
        uint32_t depth = 0u;
        if (d->io.publish(d->io.ctx, &frame, &depth) != NN2_OK) {
            d->observation.overflow++; d->observation.lost++;
            return runtime_gap(d, NN2_ADS131_RUNTIME_RAW_OVERFLOW, d->io.now_us(d->io.ctx));
        }
        d->observation.produced++; d->observation.occupancy = depth;
        if (depth > d->observation.max_occupancy) d->observation.max_occupancy = depth;
        return NN2_OK;
    }
    if (d->count == NN2_ADS131_CAPACITY) {
        d->observation.overflow++; d->observation.lost++;
        return runtime_gap(d, NN2_ADS131_RUNTIME_RAW_OVERFLOW, d->io.now_us(d->io.ctx));
    }
    d->items[(d->head + d->count) % NN2_ADS131_CAPACITY] = frame; d->count++;
    d->observation.produced++; d->observation.occupancy = d->count;
    if (d->count > d->observation.max_occupancy) d->observation.max_occupancy = d->count;
    return NN2_OK;
}
nn2_status_t nn2_ads131_pop(nn2_ads131_logic_t *d, nn2_core_adc_record_t *out)
{
    if (!d || !out) return NN2_ERR_INVALID_ARG;
    if (!d->count) return d->observation.state == NN2_ADC_FAULT ? NN2_ERR_IO : NN2_ERR_EMPTY;
    *out = d->items[d->head]; d->head = (d->head + 1u) % NN2_ADS131_CAPACITY;
    d->count--; d->observation.occupancy = d->count; d->observation.consumed++;
    return NN2_OK;
}
