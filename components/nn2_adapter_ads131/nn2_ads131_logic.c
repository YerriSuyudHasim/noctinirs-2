#include "nn2_ads131_logic.h"

#include "nn2_crc16.h"
#include "nn2_rtos_config.h"

nn2_pin_dir_t nn2_ads131_pin_dir(nn2_ads131_sig_t signal)
{
    if (signal == NN2_ADS131_SIG_MISO || signal == NN2_ADS131_SIG_DRDY) {
        return NN2_PIN_INPUT;
    }
    return NN2_PIN_OUTPUT;
}

static bool nn2_pin_in_range(int gpio)
{
    return gpio >= 0 && gpio <= NN2_ADS131_GPIO_INDEX_MAX;
}

nn2_status_t nn2_ads131_validate_pins(const nn2_ads131_pins_t *pins)
{
    int values[6];
    int i;
    int j;

    if (pins == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    values[0] = pins->cs;
    values[1] = pins->sclk;
    values[2] = pins->mosi;
    values[3] = pins->miso;
    values[4] = pins->drdy;
    values[5] = pins->sync_reset;
    for (i = 0; i < 6; i++) {
        if (!nn2_pin_in_range(values[i])) {
            return NN2_ERR_INVALID_ARG;
        }
        for (j = 0; j < i; j++) {
            if (values[j] == values[i]) {
                return NN2_ERR_INVALID_ARG;
            }
        }
    }
    if (pins->spi_host < 0) {
        return NN2_ERR_INVALID_ARG;
    }
    return NN2_OK;
}

uint32_t nn2_ads131_ring_model_depth(uint32_t data_rate_hz)
{
    uint32_t base = (data_rate_hz * 50u + 999u) / 1000u;
    return base + ((base + 3u) / 4u);
}

nn2_status_t nn2_ads131_validate_config(const nn2_adc_config_t *cfg,
                                        uint16_t storage_capacity,
                                        uint16_t *depth_out)
{
    uint16_t depth;

    if (cfg == 0 || depth_out == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (cfg->spi_hz == 0u || cfg->spi_hz > NN2_ADS131_SPI_HZ_MAX) {
        return NN2_ERR_INVALID_ARG;
    }
    if (cfg->osr == 0u || cfg->data_rate_hz == 0u) {
        return NN2_ERR_INVALID_ARG;
    }
    if (cfg->crc_kind != NN2_ADC_CRC_CCITT && cfg->crc_kind != NN2_ADC_CRC_ANSI) {
        return NN2_ERR_INVALID_ARG;
    }
    depth = cfg->ring_depth == 0u ? (uint16_t)NN2_ADS131_RING_DEPTH_DEFAULT
                                  : cfg->ring_depth;
    if (depth == 0u || depth > storage_capacity) {
        return NN2_ERR_INVALID_ARG;
    }
    *depth_out = depth;
    return NN2_OK;
}

uint32_t nn2_ads131_notify_timeout_ticks(uint32_t data_rate_hz, uint32_t tick_hz)
{
    uint64_t timeout_us;
    uint64_t tick_us;
    uint64_t ticks;

    if (data_rate_hz == 0u || tick_hz == 0u) {
        return 0u;
    }
    timeout_us = (2ull * 1000000ull + (uint64_t)data_rate_hz - 1ull) /
                 (uint64_t)data_rate_hz;
    tick_us = (1000000ull + (uint64_t)tick_hz - 1ull) / (uint64_t)tick_hz;
    if (tick_us == 0ull) {
        tick_us = 1ull;
    }
    ticks = (timeout_us + tick_us - 1ull) / tick_us;
    if (ticks < 1ull) {
        ticks = 1ull;
    }
    if (ticks > 1000000ull) {
        ticks = 1000000ull;
    }
    return (uint32_t)ticks;
}

int32_t nn2_ads131_sext24(uint32_t word)
{
    int32_t value = (int32_t)(word & 0x00FFFFFFu);
    if ((value & 0x00800000) != 0) {
        value |= ~0x00FFFFFF;
    }
    return value;
}

static uint32_t nn2_load_u24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

nn2_status_t nn2_ads131_parse_frame(const uint8_t raw[NN2_ADS131_FRAME_BYTES],
                                    nn2_adc_crc_kind_t crc_kind,
                                    uint64_t timestamp_us,
                                    nn2_adc_raw_frame_t *out)
{
    uint16_t calculated;
    uint32_t crc_word;
    int i;

    if (raw == 0 || out == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (crc_kind == NN2_ADC_CRC_CCITT) {
        calculated = nn2_crc16_ccitt(raw, 15u);
    } else if (crc_kind == NN2_ADC_CRC_ANSI) {
        calculated = nn2_crc16_ansi(raw, 15u);
    } else {
        return NN2_ERR_INVALID_ARG;
    }
    crc_word = nn2_load_u24(&raw[15]);
    out->status_word = nn2_load_u24(&raw[0]);
    for (i = 0; i < 4; i++) {
        out->channel[i] = nn2_ads131_sext24(nn2_load_u24(&raw[3 + (i * 3)]));
    }
    /* Proposed: the 16-bit CRC sits in the upper 16 bits of the last
       24-bit word. Pending Agent 04 confirmation against SBAS890D. */
    out->crc_received = (uint16_t)(crc_word >> 8);
    out->crc_calculated = calculated;
    out->crc_ok = calculated == out->crc_received;
    out->timestamp_us = timestamp_us;
    return out->crc_ok ? NN2_OK : NN2_ERR_CRC;
}

nn2_status_t nn2_ads131_logic_init(nn2_ads131_logic_t *logic,
                                   const nn2_adc_config_t *cfg,
                                   nn2_adc_raw_frame_t *storage,
                                   uint16_t storage_capacity)
{
    uint16_t depth = 0u;
    nn2_status_t status;
    bool safe_applied;

    if (logic == 0 || storage == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    status = nn2_ads131_validate_config(cfg, storage_capacity, &depth);
    if (status != NN2_OK) {
        return status;
    }
    safe_applied = logic->safe_applied;
    logic->cfg = *cfg;
    logic->cfg.ring_depth = depth;
    logic->cfg_valid = true;
    logic->safe_applied = safe_applied;
    logic->running = false;
    logic->crc_bad = 0u;
    logic->spi_errors = 0u;
    logic->drdy_timeouts = 0u;
    logic->frames_produced = 0u;
    logic->storage = storage;
    logic->storage_capacity = storage_capacity;
    return nn2_frame_ring_init(&logic->ring, storage, depth);
}

nn2_status_t nn2_ads131_logic_safe_state(nn2_ads131_logic_t *logic)
{
    if (logic == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    logic->safe_applied = true;
    logic->running = false;
    return NN2_OK;
}

nn2_status_t nn2_ads131_logic_begin_run(nn2_ads131_logic_t *logic)
{
    if (logic == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (!logic->safe_applied || !logic->cfg_valid) {
        return NN2_ERR_STATE;
    }
    logic->running = true;
    return NN2_OK;
}

nn2_status_t nn2_ads131_logic_allow_task(const nn2_ads131_logic_t *logic)
{
    if (logic == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (!logic->safe_applied || !logic->cfg_valid) {
        return NN2_ERR_STATE;
    }
    return NN2_OK;
}

nn2_status_t nn2_ads131_logic_on_timeout(nn2_ads131_logic_t *logic)
{
    if (logic == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (!logic->running) {
        return NN2_OK;
    }
    logic->drdy_timeouts++;
    return NN2_ERR_FAULT;
}

nn2_status_t nn2_ads131_logic_on_spi_error(nn2_ads131_logic_t *logic)
{
    if (logic == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    logic->spi_errors++;
    return NN2_ERR_IO;
}

nn2_status_t nn2_ads131_logic_on_frame(nn2_ads131_logic_t *logic,
                                       const uint8_t raw[NN2_ADS131_FRAME_BYTES],
                                       uint64_t timestamp_us)
{
    nn2_adc_raw_frame_t frame;
    nn2_status_t parsed;
    nn2_status_t pushed;

    if (logic == 0 || !logic->cfg_valid) {
        return NN2_ERR_INVALID_ARG;
    }
    parsed = nn2_ads131_parse_frame(raw, logic->cfg.crc_kind, timestamp_us, &frame);
    if (parsed != NN2_OK && parsed != NN2_ERR_CRC) {
        return parsed;
    }
    if (!frame.crc_ok) {
        logic->crc_bad++;
    }
    pushed = nn2_frame_ring_push(&logic->ring, &frame);
    if (pushed != NN2_OK && pushed != NN2_ERR_OVERFLOW) {
        return pushed;
    }
    logic->frames_produced++;
    if (parsed != NN2_OK) {
        return parsed;
    }
    return pushed;
}

nn2_status_t nn2_ads131_logic_pop(nn2_ads131_logic_t *logic, nn2_adc_raw_frame_t *out)
{
    if (logic == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    return nn2_frame_ring_pop(&logic->ring, out);
}

nn2_status_t nn2_ads131_logic_counters(const nn2_ads131_logic_t *logic,
                                       nn2_adc_counters_t *out)
{
    if (logic == 0 || out == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    out->crc_bad = logic->crc_bad;
    out->spi_errors = logic->spi_errors;
    out->drdy_timeouts = logic->drdy_timeouts;
    out->ring_dropped_oldest = logic->ring.dropped_oldest;
    out->frames_produced = logic->frames_produced;
    return NN2_OK;
}
