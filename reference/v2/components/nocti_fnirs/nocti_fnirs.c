// ============================================================
// File: components/nocti_fnirs/nocti_fnirs.c
// ============================================================

#include "nocti_fnirs.h"

#include <limits.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"            // slash: IRAM_ATTR

#include "freertos/task.h"
#include "freertos/portmacro.h"

#include "nocti_log.h"
#include "nocti_timebase.h"      // slash: nocti_timebase_now_us, nocti_timebase_busywait_us

static const char *TAG = "nocti_fnirs";

// ============================================================
// ADS131M04 constants
// ============================================================

// slash: register address
#define ADS131_REG_MODE   0x02u
#define ADS131_REG_CLOCK  0x03u
#define ADS131_REG_GAIN   0x04u

// slash: 16-bit command word
#define ADS131_CMD_NULL    0x0000u
#define ADS131_CMD_RESET   0x0011u
#define ADS131_CMD_LOCK    0x0555u
#define ADS131_CMD_UNLOCK  0x0655u

// ADS131M04 RREG/WREG command format (datasheet):
//   RREG = 101a aaaa annn nnnn
//   WREG = 011a aaaa annn nnnn
// The register address is SIX bits at [12:7] and the count-minus-one field is
// SEVEN bits at [6:0]. The previous implementation shifted the address by 8,
// which doubled every register address (for example CLOCK 0x03 -> CFG 0x06).
#define ADS131_CMD_RREG(addr, n_minus1) \
    ((uint16_t)((0x5u << 13) | (((uint16_t)(addr) & 0x3Fu) << 7) | \
                ((uint16_t)(n_minus1) & 0x7Fu)))
#define ADS131_CMD_WREG(addr, n_minus1) \
    ((uint16_t)((0x3u << 13) | (((uint16_t)(addr) & 0x3Fu) << 7) | \
                ((uint16_t)(n_minus1) & 0x7Fu)))

// Compile-time guards against another address-field regression.
_Static_assert(ADS131_CMD_RREG(ADS131_REG_MODE,  0u) == 0xA100u,
               "ADS131 RREG MODE encoding must be 0xA100");
_Static_assert(ADS131_CMD_RREG(ADS131_REG_CLOCK, 0u) == 0xA180u,
               "ADS131 RREG CLOCK encoding must be 0xA180");
_Static_assert(ADS131_CMD_WREG(ADS131_REG_MODE,  0u) == 0x6100u,
               "ADS131 WREG MODE encoding must be 0x6100");
_Static_assert(ADS131_CMD_WREG(ADS131_REG_CLOCK, 0u) == 0x6180u,
               "ADS131 WREG CLOCK encoding must be 0x6180");
_Static_assert(ADS131_CMD_WREG(ADS131_REG_GAIN,  0u) == 0x6200u,
               "ADS131 WREG GAIN encoding must be 0x6200");

// slash: word size 24-bit, frame normal 6 words (STATUS, CH0..CH3, CRC)
#define ADS131_WORD_BYTES    3u
#define ADS131_FRAME_WORDS   6u
#define ADS131_FRAME_BYTES   (ADS131_WORD_BYTES * ADS131_FRAME_WORDS)

// slash: MODE bit field
#define ADS131_MODE_TIMEOUT_BIT   (1u << 4)   // slash: TIMEOUT enable
#define ADS131_MODE_WLENGTH_SHIFT 8u          // slash: WLENGTH[1:0] ada di bit 9:8
#define ADS131_MODE_RX_CRC_BIT    (1u << 12)
#define ADS131_MODE_CRC_TYPE_BIT  (1u << 11)
#define ADS131_MODE_DRDY_HIZ_BIT  (1u << 1)
#define ADS131_MODE_DRDY_FMT_BIT  (1u << 0)

// 5 us is >1 CLKIN period and far below the 2048-CLKIN reset threshold for
// the expected 8.192-MHz master clock. It therefore performs synchronization
// without resetting the register map.
#define ADS131_SYNC_PULSE_US           (5u)

// Single-producer (GPIO ISR), single-consumer (ADC task) timestamp trail.
// This ring preserves event evidence without heap allocation, but it does NOT
// extend the ADS131M04 hardware FIFO: backlog deeper than two triggers SYNC
// recovery instead of pairing an old timestamp with ambiguous ADC data.
#define ADS131_DRDY_EVENT_RING_SIZE    (128u)
#define ADS131_DRDY_EVENT_RING_MASK    (ADS131_DRDY_EVENT_RING_SIZE - 1u)

_Static_assert((ADS131_DRDY_EVENT_RING_SIZE & ADS131_DRDY_EVENT_RING_MASK) == 0u,
               "ADS131 DRDY ring size must be a power of two");

// ============================================================
// Internal context
// ============================================================

typedef struct {
    uint32_t sequence;
    uint32_t backlog_depth;
    nocti_time_us_t timestamp_us;
} drdy_event_t;

typedef struct {
    nocti_fnirs_cfg_t cfg;
    bool inited;

    // DRDY event engine. The ISR is the only writer of event_head; the ADC
    // consumer task is the only writer of event_tail.
    TaskHandle_t consumer_task;
    volatile bool capture_enabled;
    volatile uint32_t event_head;
    volatile uint32_t event_tail;
    drdy_event_t events[ADS131_DRDY_EVENT_RING_SIZE];
    bool last_event_valid;
    uint32_t last_event_sequence;
    nocti_time_us_t last_event_timestamp_us;
    nocti_fnirs_stats_t stats;
    portMUX_TYPE mux;

    // slash: optional queue
    QueueHandle_t q;

    // slash: SPI buffers
    uint8_t tx[ADS131_FRAME_BYTES];
    uint8_t rx[ADS131_FRAME_BYTES];
} fnirs_ctx_t;

static fnirs_ctx_t s = {
    .mux = portMUX_INITIALIZER_UNLOCKED
};

// ============================================================
// Time helpers (pakai nocti_timebase)
// ============================================================

static inline uint64_t now_us64(void)
{
    // slash: satu bahasa waktu dengan scheduler dan modul lain
    return (uint64_t)nocti_timebase_now_us();
}

static inline void delay_us(uint32_t us)
{
    // slash: busywait presisi untuk pulse reset
    nocti_timebase_busywait_us(us);
}

// ============================================================
// 24-bit packing helpers
// ============================================================

static inline void pack_cmd24(uint16_t cmd16, uint8_t out3[3])
{
    // slash: cmd16 masuk ke bit 23..8, byte terakhir 0
    out3[0] = (uint8_t)((cmd16 >> 8) & 0xFFu);
    out3[1] = (uint8_t)(cmd16 & 0xFFu);
    out3[2] = 0x00u;
}

static inline uint32_t unpack_u24(const uint8_t in3[3])
{
    return ((uint32_t)in3[0] << 16) | ((uint32_t)in3[1] << 8) | (uint32_t)in3[2];
}

static inline int32_t sign_extend_24(uint32_t v24)
{
    // slash: 24-bit signed 2's complement
    if (v24 & 0x800000u) return (int32_t)(v24 | 0xFF000000u);
    return (int32_t)v24;
}

// ============================================================
// CRC16 CCITT (polynomial 0x1021, init 0xFFFF)
// ============================================================

static uint16_t crc16_ccitt_0xffff(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;

        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ============================================================
// Slot metadata validation
// ============================================================

static inline bool is_valid_wavelength(nocti_wavelength_t wl)
{
    return (wl == NOCTI_WL_735) || (wl == NOCTI_WL_850);
}

static esp_err_t calc_slot_index(uint8_t led_id, nocti_wavelength_t wl, uint8_t *out_slot_index)
{
    if (!out_slot_index) return ESP_ERR_INVALID_ARG;
    if (led_id >= NOCTI_MAX_FNIRS_SOURCES) return ESP_ERR_INVALID_ARG;
    if (!is_valid_wavelength(wl)) return ESP_ERR_INVALID_ARG;

    uint8_t wl_idx = (wl == NOCTI_WL_735) ? 0u : 1u;
    uint8_t idx = (uint8_t)(led_id * (uint8_t)NOCTI_MAX_FNIRS_WAVELENGTHS + wl_idx);

    if (idx >= (uint8_t)NOCTI_FNIRS_SLOTS_PER_FRAME) return ESP_ERR_INVALID_ARG;
    *out_slot_index = idx;
    return ESP_OK;
}

// ============================================================
// SPI transfer
// ============================================================

static esp_err_t spi_frame_xfer(const uint8_t *tx, uint8_t *rx)
{
    // slash: 1 frame lengkap 6 word
    // slash: paksa 1 transaksi SPI agar CS tidak toggle di tengah frame
    return nocti_hal_spi_transfer_contig(s.cfg.spi, tx, rx, ADS131_FRAME_BYTES, s.cfg.spi_timeout_ms);
}

// ============================================================
// DRDY ISR and event ring
// ============================================================

static inline uint32_t saturating_duration_u32(nocti_time_us_t start_us,
                                                nocti_time_us_t end_us)
{
    if (end_us <= start_us) return 0u;
    const nocti_time_us_t delta = end_us - start_us;
    return delta > (nocti_time_us_t)UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
}

static inline void update_max_u32(volatile uint32_t *target, uint32_t value)
{
    uint32_t previous = __atomic_load_n(target, __ATOMIC_RELAXED);
    while (value > previous &&
           !__atomic_compare_exchange_n(target,
                                        &previous,
                                        value,
                                        false,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
}

static void IRAM_ATTR drdy_isr(void *arg)
{
    (void)arg;

    const nocti_time_us_t edge_timestamp_us = nocti_timebase_now_us_isr();
    BaseType_t high_priority_woken = pdFALSE;
    TaskHandle_t consumer = NULL;

    taskENTER_CRITICAL_ISR(&s.mux);

    s.stats.irq_count += 1u;
    if (s.stats.isr_core_id < 0) {
        s.stats.isr_core_id = (int8_t)xPortGetCoreID();
    }

    if (s.capture_enabled) {
        const uint32_t head = s.event_head;
        const uint32_t tail = s.event_tail;
        const uint32_t depth = head - tail;

        if (depth >= ADS131_DRDY_EVENT_RING_SIZE) {
            // Do not overwrite an unread timestamp. The monotonically
            // increasing IRQ sequence makes the loss visible to the consumer.
            s.stats.event_overflow_count += 1u;
        } else {
            drdy_event_t *event = &s.events[head & ADS131_DRDY_EVENT_RING_MASK];
            event->sequence = s.stats.irq_count;
            event->timestamp_us = edge_timestamp_us;
            s.event_head = head + 1u;
            s.stats.event_push_count += 1u;

            const uint32_t new_depth = depth + 1u;
            if (new_depth > s.stats.event_depth_max) {
                s.stats.event_depth_max = new_depth;
            }
        }

        consumer = s.consumer_task;
    }

    taskEXIT_CRITICAL_ISR(&s.mux);

    if (consumer) {
        vTaskNotifyGiveFromISR(consumer, &high_priority_woken);
    }
    if (high_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void drdy_ring_clear_locked(void)
{
    s.event_head = 0u;
    s.event_tail = 0u;
    s.last_event_valid = false;
    s.last_event_sequence = 0u;
    s.last_event_timestamp_us = 0;
}

static bool drdy_event_try_pop(drdy_event_t *out_event)
{
    if (!out_event) return false;

    bool have_event = false;
    taskENTER_CRITICAL(&s.mux);
    if (s.event_tail != s.event_head) {
        const uint32_t depth = s.event_head - s.event_tail;
        *out_event = s.events[s.event_tail & ADS131_DRDY_EVENT_RING_MASK];
        out_event->backlog_depth = depth;
        s.event_tail += 1u;
        s.stats.event_pop_count += 1u;
        have_event = true;
    }
    taskEXIT_CRITICAL(&s.mux);
    return have_event;
}

static esp_err_t wait_drdy_event(uint32_t timeout_us, drdy_event_t *out_event)
{
    if (!out_event) return ESP_ERR_INVALID_ARG;

    const nocti_time_us_t start_us = nocti_timebase_now_us();
    const nocti_time_us_t deadline_us = timeout_us > 0u
        ? start_us + (nocti_time_us_t)timeout_us
        : 0u;

    while (true) {
        if (drdy_event_try_pop(out_event)) {
            return ESP_OK;
        }

        taskENTER_CRITICAL(&s.mux);
        const bool running = s.capture_enabled;
        taskEXIT_CRITICAL(&s.mux);
        if (!running) {
            return ESP_ERR_INVALID_STATE;
        }

        TickType_t wait_ticks = portMAX_DELAY;
        if (timeout_us > 0u) {
            const nocti_time_us_t now_us = nocti_timebase_now_us();
            if (now_us >= deadline_us) {
                taskENTER_CRITICAL(&s.mux);
                s.stats.wait_timeout_count += 1u;
                taskEXIT_CRITICAL(&s.mux);
                return ESP_ERR_TIMEOUT;
            }

            const uint32_t remaining_us = (uint32_t)(deadline_us - now_us);
            const uint32_t remaining_ms = (remaining_us + 999u) / 1000u;
            wait_ticks = pdMS_TO_TICKS(remaining_ms);
            if (wait_ticks == 0u) wait_ticks = 1u;
        }

        // The notification is only a wake-up hint. The ring remains the source
        // of truth and can safely contain multiple DRDY events.
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
    }
}

static inline uint32_t drdy_count_get(void)
{
    taskENTER_CRITICAL(&s.mux);
    const uint32_t count = s.stats.irq_count;
    taskEXIT_CRITICAL(&s.mux);
    return count;
}

// ============================================================
// Register helpers
// ============================================================

static esp_err_t ads131_cmd_simple(uint16_t cmd)
{
    memset(s.tx, 0, sizeof(s.tx));
    pack_cmd24(cmd, &s.tx[0]);
    return spi_frame_xfer(s.tx, s.rx);
}

static esp_err_t ads131_write_reg_u16_raw(uint8_t addr, uint16_t val)
{
    memset(s.tx, 0, sizeof(s.tx));

    // slash: WREG 1 reg, payload 16-bit MSB aligned di word1
    pack_cmd24(ADS131_CMD_WREG(addr, 0u), &s.tx[0]);

    s.tx[3] = (uint8_t)((val >> 8) & 0xFFu);
    s.tx[4] = (uint8_t)(val & 0xFFu);
    s.tx[5] = 0x00u;

    return spi_frame_xfer(s.tx, s.rx);
}

static esp_err_t ads131_read_reg_u16(uint8_t addr, uint16_t *out_u16)
{
    if (!out_u16) return ESP_ERR_INVALID_ARG;

    // slash: RREG, response muncul di frame berikutnya
    memset(s.tx, 0, sizeof(s.tx));
    pack_cmd24(ADS131_CMD_RREG(addr, 0u), &s.tx[0]);

    esp_err_t err = spi_frame_xfer(s.tx, s.rx);
    if (err != ESP_OK) return err;

    // slash: NULL untuk ambil response
    memset(s.tx, 0, sizeof(s.tx));
    pack_cmd24(ADS131_CMD_NULL, &s.tx[0]);

    err = spi_frame_xfer(s.tx, s.rx);
    if (err != ESP_OK) return err;

    uint32_t resp24 = unpack_u24(&s.rx[0]);
    *out_u16 = (uint16_t)((resp24 >> 8) & 0xFFFFu);
    return ESP_OK;
}

static esp_err_t ads131_write_reg_u16(uint8_t addr, uint16_t val)
{
    esp_err_t err = ads131_write_reg_u16_raw(addr, val);
    if (err != ESP_OK) return err;

    // TI's reference driver recommends reading every writable register back.
    // This catches a malformed command frame, a locked register map, or a
    // word-length mismatch immediately instead of allowing acquisition to run
    // with a silently different configuration.
    uint16_t readback = 0u;
    err = ads131_read_reg_u16(addr, &readback);
    if (err != ESP_OK) return err;
    if (readback != val) {
        NOCTI_LOGE(TAG,
                   "ADS131 write verify failed addr=0x%02X wrote=0x%04X read=0x%04X",
                   (unsigned)addr,
                   (unsigned)val,
                   (unsigned)readback);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

// ============================================================
// SYNC/RESET helpers
// ============================================================

static int pick_sync_reset_pin(void)
{
    // slash: kalau hardware 1 pin gabungan, gpio_sync dan gpio_reset sama
    if (s.cfg.gpio_reset >= 0 && s.cfg.gpio_sync >= 0 && s.cfg.gpio_reset == s.cfg.gpio_sync) {
        return s.cfg.gpio_reset;
    }
    if (s.cfg.gpio_reset >= 0) return s.cfg.gpio_reset;
    if (s.cfg.gpio_sync >= 0)  return s.cfg.gpio_sync;
    return -1;
}

static void do_hw_reset_pulse(void)
{
    int pin_sr = pick_sync_reset_pin();
    if (pin_sr < 0) return;

    uint32_t clkin = (s.cfg.clkin_hz == 0u) ? 8192000u : s.cfg.clkin_hz;
    uint32_t t_reset_us = (uint32_t)((2048u * 1000000ull) / (uint64_t)clkin);
    if (t_reset_us < 300u) t_reset_us = 300u;

    gpio_set_level(pin_sr, 0);
    delay_us(t_reset_us);
    gpio_set_level(pin_sr, 1);

    delay_us(20u);
}

static esp_err_t do_sync_pulse(void)
{
    const int pin_sr = pick_sync_reset_pin();
    if (pin_sr < 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // The producer must be stopped before this API is called. Strobe
    // SYNC/RESET low for a non-reset pulse; register configuration is retained.
    gpio_set_level(pin_sr, 0);
    delay_us(ADS131_SYNC_PULSE_US);
    gpio_set_level(pin_sr, 1);

    taskENTER_CRITICAL(&s.mux);
    s.stats.sync_count += 1u;
    taskEXIT_CRITICAL(&s.mux);

    // Conversions restart automatically. The next read waits for the next DRDY.
    return ESP_OK;
}

// ============================================================
// Public API
// ============================================================

void nocti_fnirs_cfg_set_defaults(nocti_fnirs_cfg_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    cfg->spi = NULL;
    cfg->gpio_drdy = -1;
    cfg->gpio_sync = -1;
    cfg->gpio_reset = -1;

    cfg->clkin_hz = 8192000u;
    cfg->drdy_timeout_us = 20000u;
    cfg->expected_drdy_period_us = 0u;
    cfg->drdy_period_tolerance_us = 0u;
    cfg->spi_timeout_ms = 20u;

    cfg->do_hw_reset_pulse = true;
    cfg->do_cmd_reset = false;
    cfg->do_adc_register_init = true;

    cfg->crc_check_enable = true;   // slash: default ON untuk buang frame korup
    cfg->crc_retry = 1;             // slash: 1x retry cukup untuk debug

    cfg->queue_len = 0;

    cfg->reg_mode = 0u;
    cfg->reg_clock = 0u;
    cfg->reg_gain = 0u;
}

esp_err_t nocti_fnirs_init(const nocti_fnirs_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (!cfg->spi) return ESP_ERR_INVALID_ARG;
    if (cfg->gpio_drdy < 0) return ESP_ERR_INVALID_ARG;
    if (cfg->expected_drdy_period_us > 0u &&
        cfg->drdy_period_tolerance_us >= cfg->expected_drdy_period_us) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s, 0, sizeof(s));
    s.mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    s.cfg = *cfg;
    s.stats.isr_core_id = -1;
    s.stats.consumer_core_id = -1;

    // slash: DRDY input + falling-edge interrupt. The interrupt remains
    // disabled until acquisition_start() binds a permanent consumer task.
    gpio_config_t io;
    memset(&io, 0, sizeof(io));
    io.pin_bit_mask = (1ULL << (uint32_t)s.cfg.gpio_drdy);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    err = gpio_isr_handler_add(s.cfg.gpio_drdy, drdy_isr, NULL);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG,
                   "gpio_isr_handler_add failed: install GPIO ISR service before fNIRS init");
        return err;
    }
    (void)gpio_intr_disable(s.cfg.gpio_drdy);

    // slash: SYNC/RESET output idle high
    const int pin_sr = pick_sync_reset_pin();
    if (pin_sr >= 0) {
        gpio_config_t r;
        memset(&r, 0, sizeof(r));
        r.pin_bit_mask = (1ULL << (uint32_t)pin_sr);
        r.mode = GPIO_MODE_OUTPUT;
        r.intr_type = GPIO_INTR_DISABLE;
        err = gpio_config(&r);
        if (err != ESP_OK) goto fail_remove_isr;
        gpio_set_level(pin_sr, 1);
    }

    // slash: optional queue
    s.q = NULL;
    if (s.cfg.queue_len > 0u) {
        s.q = xQueueCreate((UBaseType_t)s.cfg.queue_len,
                           sizeof(nocti_fnirs_sample_t));
        if (!s.q) {
            err = ESP_ERR_NO_MEM;
            goto fail_remove_isr;
        }
    }

    // slash: hardware reset pulse
    if (s.cfg.do_hw_reset_pulse) {
        do_hw_reset_pulse();
    }

    // TI requires RESET to occupy a complete six-word frame. ads131_cmd_simple()
    // always transfers exactly one complete ADS131M04 frame.
    if (s.cfg.do_cmd_reset) {
        err = ads131_cmd_simple(ADS131_CMD_RESET);
        if (err != ESP_OK) goto fail_queue;
        delay_us(20u);
    }

    // slash: probe register untuk verifikasi SPI hidup
    uint16_t mode0 = 0u;
    uint16_t clock0 = 0u;

    err = ads131_read_reg_u16(ADS131_REG_MODE, &mode0);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "SPI probe MODE failed: %s", esp_err_to_name(err));
        goto fail_queue;
    }

    err = ads131_read_reg_u16(ADS131_REG_CLOCK, &clock0);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "SPI probe CLOCK failed: %s", esp_err_to_name(err));
        goto fail_queue;
    }

    NOCTI_LOGI(TAG,
               "ADS131 probe ok MODE=0x%04X CLOCK=0x%04X",
               (unsigned)mode0,
               (unsigned)clock0);

    if (s.cfg.do_adc_register_init) {
        err = ads131_cmd_simple(ADS131_CMD_UNLOCK);
        if (err != ESP_OK) goto fail_queue;

        // Keep the user's non-format MODE choices, but enforce the protocol
        // contract required by the event engine and by TI's official example:
        // 24-bit words, CRC-IN disabled, CCITT CRC-OUT, push-pull DRDY, and
        // fixed-width negative DRDY pulses. Pulse format guarantees one falling
        // edge per continuously serviced output conversion; level format cannot
        // uniquely timestamp a second sample already waiting in the two-deep FIFO.
        uint16_t mode = (s.cfg.reg_mode != 0u) ? s.cfg.reg_mode : mode0;
        mode &= (uint16_t)~(0x3u << ADS131_MODE_WLENGTH_SHIFT);
        mode |= (uint16_t)(0x1u << ADS131_MODE_WLENGTH_SHIFT);
        mode &= (uint16_t)~ADS131_MODE_RX_CRC_BIT;
        mode &= (uint16_t)~ADS131_MODE_CRC_TYPE_BIT;
        mode &= (uint16_t)~ADS131_MODE_DRDY_HIZ_BIT;
        mode |= ADS131_MODE_DRDY_FMT_BIT;

        err = ads131_write_reg_u16(ADS131_REG_MODE, mode);
        if (err != ESP_OK) goto fail_queue;

        if (s.cfg.reg_clock != 0u) {
            err = ads131_write_reg_u16(ADS131_REG_CLOCK, s.cfg.reg_clock);
            if (err != ESP_OK) goto fail_queue;
        }
        if (s.cfg.reg_gain != 0u) {
            err = ads131_write_reg_u16(ADS131_REG_GAIN, s.cfg.reg_gain);
            if (err != ESP_OK) goto fail_queue;
        }

        delay_us(20u);
    }

    s.inited = true;
    NOCTI_LOGI(TAG,
               "init ok queue=%s crc=%s retry=%u DRDY=pulse ring=%u",
               s.q ? "on" : "off",
               s.cfg.crc_check_enable ? "on" : "off",
               (unsigned)s.cfg.crc_retry,
               (unsigned)ADS131_DRDY_EVENT_RING_SIZE);
    return ESP_OK;

fail_queue:
    if (s.q) {
        vQueueDelete(s.q);
        s.q = NULL;
    }
fail_remove_isr:
    (void)gpio_intr_disable(s.cfg.gpio_drdy);
    (void)gpio_isr_handler_remove(s.cfg.gpio_drdy);
    memset(&s, 0, sizeof(s));
    s.mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return err;
}

esp_err_t nocti_fnirs_deinit(void)
{
    if (!s.inited) return ESP_OK;

    nocti_fnirs_acquisition_stop();
    (void)gpio_intr_disable(s.cfg.gpio_drdy);
    (void)gpio_isr_handler_remove(s.cfg.gpio_drdy);

    if (s.q) {
        vQueueDelete(s.q);
        s.q = NULL;
    }

    memset(&s, 0, sizeof(s));
    s.mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return ESP_OK;
}

esp_err_t nocti_fnirs_sync_conversions(void)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (nocti_fnirs_acquisition_is_running()) return ESP_ERR_INVALID_STATE;

    (void)gpio_intr_disable(s.cfg.gpio_drdy);
    taskENTER_CRITICAL(&s.mux);
    drdy_ring_clear_locked();
    taskEXIT_CRITICAL(&s.mux);
    return do_sync_pulse();
}

esp_err_t nocti_fnirs_acquisition_start(TaskHandle_t consumer_task)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (!consumer_task) return ESP_ERR_INVALID_ARG;

    taskENTER_CRITICAL(&s.mux);
    const bool already_running = s.capture_enabled;
    taskEXIT_CRITICAL(&s.mux);
    if (already_running) return ESP_ERR_INVALID_STATE;

    // Stop edge delivery before resetting both the software event epoch and the
    // ADS131M04 two-deep output FIFO.
    (void)gpio_intr_disable(s.cfg.gpio_drdy);

    taskENTER_CRITICAL(&s.mux);
    s.capture_enabled = false;
    s.consumer_task = consumer_task;
    drdy_ring_clear_locked();
    taskEXIT_CRITICAL(&s.mux);

    esp_err_t err = do_sync_pulse();
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s.mux);
        s.consumer_task = NULL;
        taskEXIT_CRITICAL(&s.mux);
        return err;
    }

    taskENTER_CRITICAL(&s.mux);
    s.capture_enabled = true;
    s.stats.capture_enabled = true;
    s.stats.capture_start_count += 1u;
    taskEXIT_CRITICAL(&s.mux);

    err = gpio_intr_enable(s.cfg.gpio_drdy);
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s.mux);
        s.capture_enabled = false;
        s.stats.capture_enabled = false;
        s.consumer_task = NULL;
        drdy_ring_clear_locked();
        taskEXIT_CRITICAL(&s.mux);
        return err;
    }

    return ESP_OK;
}

void nocti_fnirs_acquisition_stop(void)
{
    if (!s.inited) return;

    (void)gpio_intr_disable(s.cfg.gpio_drdy);

    TaskHandle_t consumer = NULL;
    taskENTER_CRITICAL(&s.mux);
    const bool was_running = s.capture_enabled;
    s.capture_enabled = false;
    s.stats.capture_enabled = false;
    consumer = s.consumer_task;
    s.consumer_task = NULL;
    drdy_ring_clear_locked();
    if (was_running) {
        s.stats.capture_stop_count += 1u;
    }
    taskEXIT_CRITICAL(&s.mux);

    // Wake a consumer blocked in wait_drdy_event(); it will observe the stopped
    // state and return ESP_ERR_INVALID_STATE immediately.
    if (consumer) {
        xTaskNotifyGive(consumer);
    }
}

bool nocti_fnirs_acquisition_is_running(void)
{
    if (!s.inited) return false;
    taskENTER_CRITICAL(&s.mux);
    const bool running = s.capture_enabled;
    taskEXIT_CRITICAL(&s.mux);
    return running;
}

QueueHandle_t nocti_fnirs_get_queue(void)
{
    return s.q;
}

uint32_t nocti_fnirs_dbg_drdy_count(void)
{
    return drdy_count_get();
}

void nocti_fnirs_get_stats(nocti_fnirs_stats_t *out_stats)
{
    if (!out_stats) return;
    taskENTER_CRITICAL(&s.mux);
    *out_stats = s.stats;
    out_stats->capture_enabled = s.capture_enabled;
    taskEXIT_CRITICAL(&s.mux);
}

void nocti_fnirs_reset_stats(void)
{
    if (!s.inited) return;

    taskENTER_CRITICAL(&s.mux);
    const bool capture_enabled = s.capture_enabled;
    const int8_t isr_core_id = s.stats.isr_core_id;
    const int8_t consumer_core_id = s.stats.consumer_core_id;
    memset(&s.stats, 0, sizeof(s.stats));
    s.stats.capture_enabled = capture_enabled;
    s.stats.isr_core_id = isr_core_id;
    s.stats.consumer_core_id = consumer_core_id;
    taskEXIT_CRITICAL(&s.mux);
}

esp_err_t nocti_fnirs_dbg_read_reg_u16(uint8_t addr, uint16_t *out_u16)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (nocti_fnirs_acquisition_is_running()) return ESP_ERR_INVALID_STATE;
    return ads131_read_reg_u16(addr, out_u16);
}

esp_err_t nocti_fnirs_dbg_write_reg_u16(uint8_t addr, uint16_t val)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (nocti_fnirs_acquisition_is_running()) return ESP_ERR_INVALID_STATE;
    return ads131_write_reg_u16(addr, val);
}

static esp_err_t recover_event_epoch(bool fifo_risk,
                                      bool timing_fault,
                                      bool spi_fault)
{
    // Once software evidence no longer maps one-to-one to the ADS131M04's
    // two-deep output FIFO, continuing to read would attach the wrong DRDY
    // timestamp to a conversion. Stop edge delivery, clear both epochs with a
    // non-reset SYNC pulse, and resume only from the next unambiguous DRDY.
    (void)gpio_intr_disable(s.cfg.gpio_drdy);

    taskENTER_CRITICAL(&s.mux);
    if (!s.capture_enabled) {
        taskEXIT_CRITICAL(&s.mux);
        return ESP_ERR_INVALID_STATE;
    }
    s.capture_enabled = false;
    s.stats.capture_enabled = false;
    if (fifo_risk) s.stats.fifo_risk_count += 1u;
    if (timing_fault) s.stats.timing_fault_count += 1u;
    if (spi_fault) s.stats.spi_recovery_count += 1u;
    drdy_ring_clear_locked();
    taskEXIT_CRITICAL(&s.mux);

    esp_err_t err = do_sync_pulse();

    taskENTER_CRITICAL(&s.mux);
    if (err == ESP_OK && s.consumer_task != NULL) {
        s.capture_enabled = true;
        s.stats.capture_enabled = true;
        s.stats.recovery_count += 1u;
    }
    taskEXIT_CRITICAL(&s.mux);

    if (err == ESP_OK) {
        err = gpio_intr_enable(s.cfg.gpio_drdy);
        if (err != ESP_OK) {
            taskENTER_CRITICAL(&s.mux);
            s.capture_enabled = false;
            s.stats.capture_enabled = false;
            taskEXIT_CRITICAL(&s.mux);
        }
    }
    return err;
}

static bool event_continuity_is_valid(const drdy_event_t *event)
{
    if (!event) return false;

    bool valid = true;
    taskENTER_CRITICAL(&s.mux);
    if (s.last_event_valid) {
        const uint32_t irq_step = event->sequence - s.last_event_sequence;
        if (irq_step != 1u || event->timestamp_us <= s.last_event_timestamp_us) {
            valid = false;
        } else if (s.cfg.expected_drdy_period_us > 0u) {
            const nocti_time_us_t delta64 =
                event->timestamp_us - s.last_event_timestamp_us;
            const uint32_t delta_us = delta64 > (nocti_time_us_t)UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)delta64;
            const uint32_t expected = s.cfg.expected_drdy_period_us;
            const uint32_t tolerance = s.cfg.drdy_period_tolerance_us > 0u
                ? s.cfg.drdy_period_tolerance_us
                : ((expected / 4u) > 2u ? (expected / 4u) : 2u);
            const uint32_t minimum = expected > tolerance
                ? expected - tolerance
                : 1u;
            const uint32_t maximum = expected <= UINT32_MAX - tolerance
                ? expected + tolerance
                : UINT32_MAX;
            if (delta_us < minimum || delta_us > maximum) {
                valid = false;
            }
        }
    }
    taskEXIT_CRITICAL(&s.mux);
    return valid;
}

static void commit_event_continuity(const drdy_event_t *event)
{
    if (!event) return;
    taskENTER_CRITICAL(&s.mux);
    s.last_event_valid = true;
    s.last_event_sequence = event->sequence;
    s.last_event_timestamp_us = event->timestamp_us;
    taskEXIT_CRITICAL(&s.mux);
}

static void parse_frame_bytes(nocti_fnirs_frame_t *rec)
{
    rec->status24 = unpack_u24(&s.rx[0]);

    for (uint32_t channel = 0u; channel < 4u; channel++) {
        const uint32_t value = unpack_u24(&s.rx[3u + channel * 3u]);
        rec->ch_u24[channel] = value;
        rec->ch[channel] = sign_extend_24(value);
    }

    rec->crc24 = unpack_u24(&s.rx[15]);
    rec->crc16_rx = (uint16_t)((rec->crc24 >> 8) & 0xFFFFu);
    rec->crc16_calc = crc16_ccitt_0xffff(&s.rx[0], 15u);
    rec->crc_ok = (rec->crc16_rx == rec->crc16_calc);
}

static esp_err_t read_frame_from_next_event(nocti_fnirs_frame_t *out_frame)
{
    if (!out_frame) return ESP_ERR_INVALID_ARG;

    drdy_event_t event;
    esp_err_t err = wait_drdy_event(s.cfg.drdy_timeout_us, &event);
    if (err != ESP_OK) return err;

    if (event.backlog_depth > 2u) {
        const esp_err_t recovery_err =
            recover_event_epoch(true, false, false);
        return recovery_err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : recovery_err;
    }

    // In pulse mode the ADS131M04 can suppress a later DRDY pulse when a
    // conversion was not serviced. A timestamp gap is therefore evidence that
    // the physical FIFO and the software event trail may be out of phase even
    // when the ring depth is only one. Recover before clocking out ambiguous
    // data, rather than allowing a permanent one-sample timestamp shift.
    if (!event_continuity_is_valid(&event)) {
        const esp_err_t recovery_err =
            recover_event_epoch(false, true, false);
        return recovery_err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : recovery_err;
    }

    taskENTER_CRITICAL(&s.mux);
    if (s.stats.consumer_core_id < 0) {
        s.stats.consumer_core_id = (int8_t)xPortGetCoreID();
    }
    taskEXIT_CRITICAL(&s.mux);

    const nocti_time_us_t spi_start_us = nocti_timebase_now_us();
    update_max_u32(&s.stats.max_irq_to_spi_start_us,
                   saturating_duration_u32(event.timestamp_us, spi_start_us));

    memset(s.tx, 0, sizeof(s.tx));
    pack_cmd24(ADS131_CMD_NULL, &s.tx[0]);

    err = spi_frame_xfer(s.tx, s.rx);
    const nocti_time_us_t spi_done_us = nocti_timebase_now_us();
    update_max_u32(&s.stats.max_spi_xfer_us,
                   saturating_duration_u32(spi_start_us, spi_done_us));
    update_max_u32(&s.stats.max_frame_service_us,
                   saturating_duration_u32(event.timestamp_us, spi_done_us));
    if (err != ESP_OK) {
        const esp_err_t recovery_err =
            recover_event_epoch(false, false, true);
        return recovery_err == ESP_OK ? err : recovery_err;
    }

    commit_event_continuity(&event);

    memset(out_frame, 0, sizeof(*out_frame));
    out_frame->timestamp_us = event.timestamp_us;
    out_frame->drdy_sequence = event.sequence;
    parse_frame_bytes(out_frame);
    return ESP_OK;
}

esp_err_t nocti_fnirs_read_sample(uint8_t led_id,
                                 nocti_wavelength_t wl,
                                 uint8_t current_mA,
                                 nocti_fnirs_sample_t *out_opt)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;

    uint8_t slot_index = 0u;
    esp_err_t err = calc_slot_index(led_id, wl, &slot_index);
    if (err != ESP_OK) return err;

    nocti_fnirs_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    const uint8_t attempts = (uint8_t)(1u + s.cfg.crc_retry);
    for (uint8_t attempt = 0u; attempt < attempts; attempt++) {
        err = read_frame_from_next_event(&frame);
        if (err != ESP_OK) return err;
        if (!s.cfg.crc_check_enable || frame.crc_ok) break;

        if (attempt == 0u && attempts > 1u) {
            NOCTI_LOGW(TAG,
                       "CRC fail rx=0x%04X calc=0x%04X; consuming next DRDY event",
                       (unsigned)frame.crc16_rx,
                       (unsigned)frame.crc16_calc);
        }
    }

    nocti_fnirs_sample_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_us = frame.timestamp_us;
    rec.drdy_sequence = frame.drdy_sequence;
    rec.slot_index = slot_index;
    rec.led_id = led_id;
    rec.wavelength = (uint8_t)wl;
    rec.current_mA = current_mA;
    rec.status24 = frame.status24;
    memcpy(rec.ch, frame.ch, sizeof(rec.ch));
    memcpy(rec.ch_u24, frame.ch_u24, sizeof(rec.ch_u24));
    rec.crc24 = frame.crc24;
    rec.crc16_rx = frame.crc16_rx;
    rec.crc16_calc = frame.crc16_calc;
    rec.crc_ok = frame.crc_ok;

    if (out_opt) *out_opt = rec;
    if (s.q) (void)xQueueSend(s.q, &rec, 0);
    return ESP_OK;
}

esp_err_t nocti_fnirs_read_frame(nocti_fnirs_frame_t *out_opt)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (!out_opt) return ESP_ERR_INVALID_ARG;

    const uint8_t attempts = (uint8_t)(1u + s.cfg.crc_retry);
    nocti_fnirs_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    for (uint8_t attempt = 0u; attempt < attempts; attempt++) {
        const esp_err_t err = read_frame_from_next_event(&frame);
        if (err != ESP_OK) return err;
        if (!s.cfg.crc_check_enable || frame.crc_ok) break;
    }

    *out_opt = frame;
    return ESP_OK;
}
