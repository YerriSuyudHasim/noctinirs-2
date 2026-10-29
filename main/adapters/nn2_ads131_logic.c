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
nn2_status_t nn2_ads131_parse(const uint8_t raw[NN2_ADS131_BYTES], uint64_t time,
                            nn2_core_adc_record_t *out)
{
    nn2_core_adc_record_t result = {0};
    if (!raw || !out) return NN2_ERR_INVALID_ARG;
    if (integrity(raw) != NN2_OK) return NN2_ERR_IO;
    result.status = word(raw);
    /* 24-bit, all channels fresh, no lock/reset/CRC/register error/reserved bits. */
    if (result.status != 0x010fu) return NN2_ERR_IO;
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
    d->io.delay_us(d->io.ctx, 251u);
    startup_step(d, NN2_ADS131_STAGE_RESET_HIGH, 0xffu);
    if (d->io.reset(d->io.ctx, true) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->io.delay_us(d->io.ctx, 5u);
    s = read_reg(d, 0u, &id);
    if (s == NN2_OK && (id & 0xff00u) != 0x2400u) return nn2_ads131_fail(d, NN2_ADC_FAULT_ID);
    if (s == NN2_OK) s = program(d, 2u, 0x0110u, 0x3f1fu);
    if (s == NN2_OK) s = program(d, 3u, 0x0f0au, 0x0f3fu);
    if (s == NN2_OK) s = program(d, 4u, 0u, 0x7777u);
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
    d->last_service_us = d->io.now_us(d->io.ctx); d->settling = 2u;
    d->observation.state = NN2_ADC_RUNNING; return NN2_OK;
}
nn2_status_t nn2_ads131_stop(nn2_ads131_logic_t *d)
{
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->io.reset(d->io.ctx, false) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->observation.state = NN2_ADC_STOPPED; return NN2_OK;
}
nn2_status_t nn2_ads131_service(nn2_ads131_logic_t *d, uint32_t notifications)
{
    uint8_t rx[NN2_ADS131_BYTES]; nn2_core_adc_record_t frame;
    uint64_t now, edge = 0u;
    uint32_t serial = 0u;
    uint32_t dropped = 0u;
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->observation.state != NN2_ADC_RUNNING) return NN2_ERR_INVALID_STATE;
    if (d->io.take_events) {
        nn2_ads131_event_t event;
        notifications = d->io.take_events(d->io.ctx, &event, &dropped);
        edge = event.time_us; serial = event.serial;
    }
    /* An ISR can append an event during take_events(). Sample time after
     * taking the snapshot, otherwise a legitimate new edge looks future-dated. */
    now = d->io.now_us(d->io.ctx);
    if (!d->runtime_failure.valid) {
        d->runtime_failure.pending = notifications; d->runtime_failure.dropped = dropped;
        d->runtime_failure.expected_serial = d->edge_serial + 1u;
        d->runtime_failure.event_serial = serial; d->runtime_failure.edge_us = edge;
        d->runtime_failure.observed_serial = serial;
        if (d->io.take_events && d->io.drdy_latch)
            (void)d->io.drdy_latch(d->io.ctx, &d->runtime_failure.observed_serial);
    }
    if (notifications == 0u) {
        if (now - d->last_service_us < 30000u) return NN2_OK;
        d->observation.drdy_timeouts++; d->observation.loss_unknown++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_DRDY);
    }
    d->observation.sequence += notifications;
    if (notifications > 1u || (d->settling == 0u && now - d->last_service_us > 250u)) {
        d->observation.adc_missed += notifications - 1u;
        d->observation.lost += notifications; d->observation.loss_unknown++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_GAP);
    }
    if (d->io.drdy_latch) {
        uint32_t latest_serial;
        uint64_t latest_edge = d->io.drdy_latch(d->io.ctx, &latest_serial);
        now = d->io.now_us(d->io.ctx);
        if (!d->io.take_events) { edge = latest_edge; serial = latest_serial; }
        if (!d->runtime_failure.valid) {
            d->runtime_failure.event_serial = serial; d->runtime_failure.observed_serial = latest_serial;
            d->runtime_failure.edge_us = edge;
        }
        if (!edge || latest_serial != serial || serial - d->edge_serial != 1u || now < edge || now - edge >= 125u) {
            d->observation.lost++; d->observation.loss_unknown++;
            return nn2_ads131_fail(d, NN2_ADC_FAULT_GAP);
        }
    }
    if (exchange(d, 0u, 0u, rx) != NN2_OK) { d->observation.lost++; return NN2_ERR_IO; }
    if (d->io.drdy_latch) {
        uint32_t after_serial;
        (void)d->io.drdy_latch(d->io.ctx, &after_serial);
        if (!d->runtime_failure.valid) d->runtime_failure.observed_serial = after_serial;
        if (serial != after_serial || d->io.now_us(d->io.ctx) - edge >= 125u) {
            d->observation.lost++; d->observation.loss_unknown++;
            return nn2_ads131_fail(d, NN2_ADC_FAULT_GAP);
        }
        d->edge_serial = serial;
    }
    now = d->io.now_us(d->io.ctx);
    d->last_service_us = now;
    if (d->settling) { d->settling--; return NN2_OK; }
    if (nn2_ads131_parse(rx, now, &frame) != NN2_OK) {
        d->observation.lost++; return nn2_ads131_fail(d, NN2_ADC_FAULT_READBACK);
    }
    if (d->count == NN2_ADS131_CAPACITY) {
        d->head = (d->head + 1u) % NN2_ADS131_CAPACITY; d->count--;
        d->observation.overflow++; d->observation.lost++;
        d->quality |= NN2_ADC_QUALITY_OVERFLOW | NN2_ADC_QUALITY_GAP;
    }
    frame.sequence = d->observation.sequence; frame.session = d->observation.session;
    frame.drdy_us = edge;
    frame.quality = d->quality | (d->io.drdy_latch ? 0u : NN2_ADC_QUALITY_NO_EDGE);
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
