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
    d->observation.state = NN2_ADC_FAULT;
    d->observation.fault = reason;
    d->quality |= NN2_ADC_QUALITY_GAP;
    if (d->io.reset(d->io.ctx, false) != NN2_OK) d->observation.fault = NN2_ADC_FAULT_RESET;
    return NN2_ERR_IO;
}
static nn2_status_t exchange(nn2_ads131_logic_t *d, uint16_t cmd, uint16_t data, uint8_t *rx)
{
    uint8_t tx[NN2_ADS131_BYTES] = {0};
    tx[0] = (uint8_t)(cmd >> 8); tx[1] = (uint8_t)cmd;
    tx[3] = (uint8_t)(data >> 8); tx[4] = (uint8_t)data;
    if (d->io.transfer(d->io.ctx, tx, rx) != NN2_OK) {
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
    nn2_status_t s = exchange(d, (uint16_t)(0xa000u | (address << 7)), 0u, rx);
    if (s == NN2_OK) s = exchange(d, 0u, 0u, rx);
    if (s == NN2_OK) *out = word(rx);
    return s;
}
static nn2_status_t program(nn2_ads131_logic_t *d, unsigned address, uint16_t value, uint16_t mask)
{
    uint8_t rx[NN2_ADS131_BYTES]; uint16_t actual;
    nn2_status_t s = exchange(d, (uint16_t)(0x6000u | (address << 7)), value, rx);
    if (s == NN2_OK) s = exchange(d, 0u, 0u, rx);
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
    if (d->io.reset(d->io.ctx, false) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->io.delay_us(d->io.ctx, 251u);
    if (d->io.reset(d->io.ctx, true) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
    d->io.delay_us(d->io.ctx, 5u);
    s = read_reg(d, 0u, &id);
    if (s == NN2_OK && (id & 0xff00u) != 0x2400u) return nn2_ads131_fail(d, NN2_ADC_FAULT_ID);
    if (s == NN2_OK) s = program(d, 2u, 0x0110u, 0x3f1fu);
    if (s == NN2_OK) s = program(d, 3u, 0x0f0au, 0x0f3fu);
    if (s == NN2_OK) s = program(d, 4u, 0u, 0x7777u);
    if (s == NN2_OK && d->io.wait_ready(d->io.ctx, 20u) != NN2_OK) {
        d->observation.drdy_timeouts++;
        return nn2_ads131_fail(d, NN2_ADC_FAULT_DRDY);
    }
    if (s == NN2_OK) {
        d->observation.state = NN2_ADC_READY; d->observation.fault = NN2_ADC_FAULT_NONE;
        d->observation.session++; d->quality = 0u;
    }
    return s;
}
nn2_status_t nn2_ads131_start(nn2_ads131_logic_t *d)
{
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->observation.state != NN2_ADC_READY) return NN2_ERR_INVALID_STATE;
    if (d->io.sync(d->io.ctx) != NN2_OK) return nn2_ads131_fail(d, NN2_ADC_FAULT_RESET);
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
    uint64_t now;
    if (!d) return NN2_ERR_INVALID_ARG;
    if (d->observation.state != NN2_ADC_RUNNING) return NN2_ERR_INVALID_STATE;
    now = d->io.now_us(d->io.ctx);
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
    if (exchange(d, 0u, 0u, rx) != NN2_OK) { d->observation.lost++; return NN2_ERR_IO; }
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
    frame.quality = d->quality;
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
