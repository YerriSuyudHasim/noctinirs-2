#include <stdio.h>
#include <string.h>

#include "nn2_ads131_logic.h"
#include "nn2_app_safe.h"
#include "nn2_crc16.h"
#include "nn2_port_adc.h"
#include "nn2_rtos_config.h"

static int g_failed;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);           \
            g_failed++;                                                       \
        }                                                                     \
    } while (0)

static int g_safe_calls;

static nn2_status_t fake_safe(void *ctx)
{
    int *calls = ctx;
    (*calls)++;
    return NN2_OK;
}

static nn2_status_t fake_missing(void *ctx, nn2_adc_raw_frame_t *out)
{
    (void)ctx;
    (void)out;
    return NN2_ERR_MISSING;
}

static void test_port_contract(void)
{
    nn2_port_adc_t port;
    nn2_adc_raw_frame_t frame;

    memset(&port, 0, sizeof(port));
    port.ctx = &g_safe_calls;
    port.safe_state = fake_safe;
    port.read_raw = fake_missing;
    EXPECT(port.safe_state(port.ctx) == NN2_OK);
    EXPECT(g_safe_calls == 1);
    EXPECT(port.read_raw(port.ctx, &frame) == NN2_ERR_MISSING);
    EXPECT(nn2_ads131_logic_safe_state(0) == NN2_ERR_INVALID_ARG);
    EXPECT(nn2_ads131_validate_config(0, 16u, 0) == NN2_ERR_INVALID_ARG);
}

static nn2_adc_config_t valid_cfg(void)
{
    nn2_adc_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.spi_hz = NN2_ADS131_SPI_HZ_MAX;
    cfg.osr = 1u;
    cfg.data_rate_hz = 1000u;
    cfg.crc_kind = NN2_ADC_CRC_CCITT;
    cfg.ring_depth = 0u;
    return cfg;
}

static void test_config(void)
{
    nn2_adc_config_t cfg = valid_cfg();
    uint16_t depth = 0u;

    EXPECT(nn2_ads131_validate_config(&cfg, NN2_ADS131_RING_DEPTH_DEFAULT,
                                      &depth) == NN2_OK);
    EXPECT(depth == NN2_ADS131_RING_DEPTH_DEFAULT);
    cfg.spi_hz = NN2_ADS131_SPI_HZ_MAX + 1u;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_ERR_INVALID_ARG);
    cfg = valid_cfg();
    cfg.spi_hz = 0u;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_ERR_INVALID_ARG);
    cfg = valid_cfg();
    cfg.osr = 0u;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_ERR_INVALID_ARG);
    cfg = valid_cfg();
    cfg.data_rate_hz = 0u;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_ERR_INVALID_ARG);
    cfg = valid_cfg();
    cfg.crc_kind = (nn2_adc_crc_kind_t)0;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_ERR_INVALID_ARG);
    cfg = valid_cfg();
    cfg.ring_depth = 17u;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_ERR_INVALID_ARG);
    cfg.osr = 512u;
    cfg.ring_depth = 4u;
    cfg.spi_hz = 1000000u;
    EXPECT(nn2_ads131_validate_config(&cfg, 16u, &depth) == NN2_OK);
    EXPECT(depth == 4u);
    EXPECT(nn2_ads131_ring_model_depth(8000u) == 500u);
    EXPECT(NN2_ADS131_RING_DEPTH_DEFAULT == 16u);
    EXPECT(nn2_ads131_ring_model_depth(8000u) > NN2_ADS131_RING_DEPTH_DEFAULT);
}

static void test_crc_and_frame(void)
{
    static const uint8_t one[] = {0x00u};
    static const uint8_t three[] = {0xFFu, 0xFFu, 0xFFu};
    static const uint8_t seq[] = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u,
                                  0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Bu,
                                  0x0Cu, 0x0Du, 0x0Eu};
    static const uint8_t five[] = {0x12u, 0x34u, 0x56u, 0x78u, 0x9Au};
    uint8_t raw[NN2_ADS131_FRAME_BYTES];
    uint8_t flipped[NN2_ADS131_FRAME_BYTES];
    nn2_adc_raw_frame_t frame;
    uint16_t crc;

    EXPECT(nn2_crc16_ccitt(one, 1u) == 0xE1F0u);
    EXPECT(nn2_crc16_ansi(one, 1u) == 0xFD02u);
    EXPECT(nn2_crc16_ccitt(three, 3u) == 0x1EF0u);
    EXPECT(nn2_crc16_ansi(three, 3u) == 0x0202u);
    EXPECT(nn2_crc16_ccitt(seq, 15u) == 0xD551u);
    EXPECT(nn2_crc16_ansi(seq, 15u) == 0xED00u);
    EXPECT(nn2_crc16_ccitt(five, 5u) == 0xF8A0u);
    EXPECT(nn2_crc16_ansi(five, 5u) == 0xA418u);
    EXPECT(nn2_ads131_sext24(0x7FFFFFu) == 8388607);
    EXPECT(nn2_ads131_sext24(0x800000u) == -8388608);
    EXPECT(nn2_ads131_sext24(0xFFFFFFu) == -1);
    EXPECT(nn2_ads131_sext24(0u) == 0);

    memset(raw, 0, sizeof(raw));
    raw[3] = 0x7Fu;
    raw[4] = 0xFFu;
    raw[5] = 0xFFu;
    raw[6] = 0x80u;
    raw[7] = 0x00u;
    raw[8] = 0x00u;
    raw[9] = 0xFFu;
    raw[10] = 0xFFu;
    raw[11] = 0xFFu;
    crc = nn2_crc16_ccitt(raw, 15u);
    raw[15] = (uint8_t)(crc >> 8);
    raw[16] = (uint8_t)crc;
    EXPECT(nn2_ads131_parse_frame(raw, NN2_ADC_CRC_CCITT, 42u, &frame) == NN2_OK);
    EXPECT(frame.crc_ok);
    EXPECT(frame.timestamp_us == 42u);
    EXPECT(frame.channel[0] == 8388607);
    EXPECT(frame.channel[1] == -8388608);
    EXPECT(frame.channel[2] == -1);
    EXPECT(frame.channel[3] == 0);

    memcpy(flipped, raw, sizeof(raw));
    flipped[0] ^= 0x01u;
    EXPECT(nn2_ads131_parse_frame(flipped, NN2_ADC_CRC_CCITT, 0u, &frame) ==
           NN2_ERR_CRC);
    EXPECT(!frame.crc_ok);
    EXPECT(nn2_crc16_ccitt(seq, 15u) !=
           nn2_crc16_ccitt((const uint8_t *)"\x00\x01\x02\x13\x04\x05\x06\x07"
                                            "\x08\x09\x0a\x0b\x0c\x0d\x0e",
                           15u));
}

static void test_ring(void)
{
    nn2_adc_raw_frame_t storage[2];
    nn2_frame_ring_t ring;
    nn2_adc_raw_frame_t frame;
    nn2_adc_raw_frame_t out;
    int i;

    EXPECT(nn2_frame_ring_init(&ring, storage, 2u) == NN2_OK);
    memset(&frame, 0, sizeof(frame));
    for (i = 0; i < 100; i++) {
        frame.timestamp_us = (uint64_t)i;
        EXPECT(nn2_frame_ring_push(&ring, &frame) ==
               (i >= 2 ? NN2_ERR_OVERFLOW : NN2_OK));
    }
    EXPECT(ring.count == 2u);
    EXPECT(ring.dropped_oldest == 98u);
    EXPECT(nn2_frame_ring_pop(&ring, &out) == NN2_OK);
    EXPECT(out.timestamp_us == 98u);
    EXPECT(nn2_frame_ring_pop(&ring, &out) == NN2_OK);
    EXPECT(out.timestamp_us == 99u);
    EXPECT(nn2_frame_ring_pop(&ring, &out) == NN2_ERR_MISSING);
}

static void test_timeout_and_safe(void)
{
    nn2_ads131_logic_t logic;
    nn2_adc_raw_frame_t storage[NN2_ADS131_RING_DEPTH_DEFAULT];
    nn2_adc_config_t cfg = valid_cfg();
    nn2_adc_counters_t counters;
    nn2_ads131_pins_t pins;
    uint8_t raw[NN2_ADS131_FRAME_BYTES];
    uint16_t crc;

    memset(&logic, 0, sizeof(logic));
    EXPECT(nn2_ads131_logic_begin_run(&logic) == NN2_ERR_STATE);
    EXPECT(nn2_ads131_logic_allow_task(&logic) == NN2_ERR_STATE);
    EXPECT(nn2_ads131_logic_init(&logic, &cfg, storage,
                                 NN2_ADS131_RING_DEPTH_DEFAULT) == NN2_OK);
    EXPECT(nn2_ads131_logic_allow_task(&logic) == NN2_ERR_STATE);
    EXPECT(nn2_ads131_logic_safe_state(&logic) == NN2_OK);
    EXPECT(nn2_ads131_logic_on_timeout(&logic) == NN2_OK);
    EXPECT(nn2_ads131_logic_allow_task(&logic) == NN2_OK);
    EXPECT(nn2_ads131_logic_begin_run(&logic) == NN2_OK);
    EXPECT(nn2_ads131_logic_on_timeout(&logic) == NN2_ERR_FAULT);
    EXPECT(nn2_ads131_logic_on_timeout(&logic) == NN2_ERR_FAULT);
    EXPECT(nn2_ads131_logic_counters(&logic, &counters) == NN2_OK);
    EXPECT(counters.drdy_timeouts == 2u);
    EXPECT(nn2_ads131_logic_safe_state(&logic) == NN2_OK);
    EXPECT(!logic.running);
    EXPECT(nn2_ads131_logic_on_timeout(&logic) == NN2_OK);
    EXPECT(nn2_ads131_logic_counters(&logic, &counters) == NN2_OK);
    EXPECT(counters.drdy_timeouts == 2u);

    memset(raw, 0, sizeof(raw));
    raw[3] = 0x00u;
    raw[4] = 0x00u;
    raw[5] = 0x01u;
    crc = nn2_crc16_ccitt(raw, 15u);
    raw[15] = (uint8_t)(crc >> 8);
    raw[16] = (uint8_t)crc;
    EXPECT(nn2_ads131_logic_begin_run(&logic) == NN2_OK);
    EXPECT(nn2_ads131_logic_on_frame(&logic, raw, 7u) == NN2_OK);
    raw[0] ^= 0x01u;
    EXPECT(nn2_ads131_logic_on_frame(&logic, raw, 8u) == NN2_ERR_CRC);
    EXPECT(nn2_ads131_logic_on_spi_error(&logic) == NN2_ERR_IO);
    EXPECT(nn2_ads131_logic_counters(&logic, &counters) == NN2_OK);
    EXPECT(counters.crc_bad == 1u);
    EXPECT(counters.spi_errors == 1u);
    EXPECT(counters.frames_produced == 2u);

    memset(&pins, 0, sizeof(pins));
    pins.spi_host = 1;
    pins.cs = 7;
    pins.sclk = 36;
    pins.mosi = 38;
    pins.miso = 37;
    pins.drdy = 35;
    pins.sync_reset = 6;
    EXPECT(nn2_ads131_validate_pins(&pins) == NN2_OK);
    pins.miso = pins.drdy;
    EXPECT(nn2_ads131_validate_pins(&pins) == NN2_ERR_INVALID_ARG);
    pins.miso = 49;
    EXPECT(nn2_ads131_validate_pins(&pins) == NN2_ERR_INVALID_ARG);
    EXPECT(nn2_ads131_pin_dir(NN2_ADS131_SIG_MISO) == NN2_PIN_INPUT);
    EXPECT(nn2_ads131_pin_dir(NN2_ADS131_SIG_DRDY) == NN2_PIN_INPUT);
    EXPECT(nn2_ads131_pin_dir(NN2_ADS131_SIG_SCLK) == NN2_PIN_OUTPUT);
    EXPECT(nn2_ads131_pin_dir(NN2_ADS131_SIG_MOSI) == NN2_PIN_OUTPUT);
    EXPECT(nn2_ads131_pin_dir(NN2_ADS131_SIG_CS) == NN2_PIN_OUTPUT);
    EXPECT(nn2_ads131_pin_dir(NN2_ADS131_SIG_SYNC) == NN2_PIN_OUTPUT);
}

static void test_timeout_ticks(void)
{
    EXPECT(nn2_ads131_notify_timeout_ticks(0u, 100u) == 0u);
    EXPECT(nn2_ads131_notify_timeout_ticks(1000u, 1000u) == 2u);
    EXPECT(nn2_ads131_notify_timeout_ticks(8000u, 100u) == 1u);
    EXPECT(nn2_ads131_notify_timeout_ticks(1u, 100u) <= 1000000u);
    EXPECT(nn2_ads131_notify_timeout_ticks(1u, 100u) != 0xFFFFFFFFu);
}

static void test_hold_policy(void)
{
    EXPECT(nn2_app_adc_hold_required(NN2_APP_BOOT));
    EXPECT(nn2_app_adc_hold_required(NN2_APP_FAULTED));
    EXPECT(nn2_app_adc_hold_required(NN2_APP_SAFE_OFF));
    EXPECT(!nn2_app_adc_hold_required(NN2_APP_STOPPED));
    EXPECT(!nn2_app_adc_hold_required(NN2_APP_RUNNING));
    EXPECT(NN2_RTOS_STACK_ADS131_BYTES >= NN2_RTOS_STACK_MARGIN_MIN_BYTES);
    EXPECT(NN2_RTOS_PRIO_LED_TBD35 > NN2_RTOS_PRIO_SCHED_TBD35);
    EXPECT(NN2_RTOS_PRIO_SCHED_TBD35 > NN2_RTOS_PRIO_ADS131_TBD35);
    EXPECT(NN2_RTOS_PRIO_ADS131_TBD35 == NN2_RTOS_PRIO_ADS1299_TBD35);
}

int main(void)
{
    test_port_contract();
    test_config();
    test_crc_and_frame();
    test_ring();
    test_timeout_and_safe();
    test_timeout_ticks();
    test_hold_policy();
    if (g_failed != 0) {
        printf("%d failure(s)\n", g_failed);
        return 1;
    }
    printf("PASS host tests\n");
    return 0;
}
