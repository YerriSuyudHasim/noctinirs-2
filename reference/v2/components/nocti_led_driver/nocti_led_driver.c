// ============================================================
// File: components/nocti_led_driver/nocti_led_driver.c
// ============================================================

#include "nocti_led_driver.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "nocti_log.h"

#define PCA9956_REG_MODE1     0x00u
#define PCA9956_REG_MODE2     0x01u
#define PCA9956_REG_LEDOUT0   0x02u
#define PCA9956_REG_LEDOUT5   0x07u
#define PCA9956_REG_GRPPWM    0x08u
#define PCA9956_REG_GRPFREQ   0x09u
#define PCA9956_REG_IREF0     0x22u
#define PCA9956_REG_PWMALL    0x3Fu

#define PCA9956_LDR_OFF       0x0u
#define PCA9956_LDR_ON        0x1u

#define PCA9956_MODE1_AI1     (1u << 6u)

// MODE1 bit 7 (AIF) is read-only, so verify only writable bits 6:0.
#define PCA9956_MODE1_VERIFY_MASK   (0x7Fu)

// MODE2 bits 7:6 are live status flags, bit 4 is write-only/self-clearing,
// and reserved bits 2:0 read back as 0b101. Only DMBLNK and OCH are
// configuration bits that must match the value programmed by this driver.
#define PCA9956_MODE2_DMBLNK        (1u << 5u)
#define PCA9956_MODE2_OCH           (1u << 3u)
#define PCA9956_MODE2_VERIFY_MASK   (PCA9956_MODE2_DMBLNK | PCA9956_MODE2_OCH)
#define PCA9956_MODE2_RESERVED_MASK (0x07u)
#define PCA9956_MODE2_RESERVED_READ (0x05u)

#define PCA_INIT_TIMEOUT_DEFAULT_MS     (50u)
#define PCA_RUNTIME_TIMEOUT_DEFAULT_MS  (2u)
#define PCA_RESET_LOW_DEFAULT_US        (5u)
#define PCA_RESET_RECOVERY_DEFAULT_US   (1600u)

static const char *TAG = "nocti_led";

typedef struct {
    nocti_led_driver_cfg_t cfg;
    bool inited;
    bool device_ready;

    uint8_t iref_cache[NOCTI_LED_PCA_CHANNELS];
    uint8_t ledout_cache[6];
    bool iref_cache_valid;
    bool ledout_cache_valid;

    uint8_t slot_map[NOCTI_LED_SLOTS];
    uint8_t last_on_ch;

    nocti_hal_i2c_async_reg8_t fast_ledout;
    bool fast_path_ready;

    nocti_led_driver_stats_t stats;
} led_ctx_t;

static led_ctx_t s;

static const uint8_t k_default_slot_to_pca[NOCTI_LED_SLOTS] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
    14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u,
};

static uint32_t init_timeout_ms(void)
{
    return s.cfg.init_i2c_timeout_ms ?
        s.cfg.init_i2c_timeout_ms :
        PCA_INIT_TIMEOUT_DEFAULT_MS;
}

static uint32_t runtime_timeout_ms(void)
{
    return s.cfg.runtime_i2c_timeout_ms ?
        s.cfg.runtime_i2c_timeout_ms :
        PCA_RUNTIME_TIMEOUT_DEFAULT_MS;
}

static uint16_t clamp_ma(uint16_t ma)
{
    if (s.cfg.max_current_ma == 0u) return ma;
    return ma > s.cfg.max_current_ma ? s.cfg.max_current_ma : ma;
}

static bool is_valid_pca_ch(uint8_t ch)
{
    return ch < NOCTI_LED_PCA_CHANNELS;
}

static bool is_gap_channel(uint8_t ch)
{
    return ch >= 10u && ch <= 13u;
}

static bool is_valid_wavelength(nocti_wavelength_t wavelength)
{
    return wavelength == NOCTI_WL_735 || wavelength == NOCTI_WL_850;
}

static uint8_t ma_to_iref(uint16_t ma, uint32_t rext_ohm)
{
    if (ma == 0u || rext_ohm == 0u) return 0u;

    const uint32_t current_uA = (uint32_t)ma * 1000u;
    const uint64_t numerator =
        (uint64_t)current_uA * (uint64_t)(4u * rext_ohm);
    const uint32_t denominator = 900000u;

    uint32_t iref =
        (uint32_t)((numerator + (denominator / 2u)) / denominator);
    if (iref > 255u) iref = 255u;
    return (uint8_t)iref;
}

static esp_err_t pca_write_reg8_init(uint8_t reg, uint8_t value)
{
    return nocti_hal_i2c_write_u8(s.cfg.i2c,
                                  s.cfg.i2c_addr_7bit,
                                  reg,
                                  value,
                                  init_timeout_ms());
}

static esp_err_t pca_read_reg8_init(uint8_t reg, uint8_t *out_value)
{
    return nocti_hal_i2c_read_u8(s.cfg.i2c,
                                 s.cfg.i2c_addr_7bit,
                                 reg,
                                 out_value,
                                 init_timeout_ms());
}

// Use a MODE1 register read as a stronger presence check than address-only
// probing. At this point the HAL is deliberately in true synchronous mode, so
// the repeated-START read is complete before the local RX byte is inspected.
static esp_err_t pca_detect_by_mode1_read(void)
{
    uint8_t mode1 = 0u;
    const esp_err_t err = pca_read_reg8_init(PCA9956_REG_MODE1, &mode1);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG,
                   "PCA register detect failed addr=0x%02X reg=MODE1: %s",
                   (unsigned)s.cfg.i2c_addr_7bit,
                   esp_err_to_name(err));
        return err;
    }

    NOCTI_LOGI(TAG,
               "PCA register detect ok addr=0x%02X MODE1=0x%02X",
               (unsigned)s.cfg.i2c_addr_7bit,
               (unsigned)mode1);
    return ESP_OK;
}

static uint32_t async_queue_depth(void)
{
    if (!s.cfg.enable_async_fast_path) return 0u;
    return s.cfg.async_queue_depth != 0u ?
        (uint32_t)s.cfg.async_queue_depth : 4u;
}

static esp_err_t pca_runtime_write_raw(const uint8_t *data,
                                       size_t len,
                                       nocti_time_us_t *out_done_us)
{
    if (out_done_us) *out_done_us = 0;
    if (!data || len == 0u) return ESP_ERR_INVALID_ARG;

    s.stats.runtime_write_count++;
    esp_err_t err = ESP_OK;

    if (s.fast_path_ready) {
        s.stats.fast_path_write_count++;
        int64_t done_us = 0;
        err = nocti_hal_i2c_async_write_wait(
            &s.fast_ledout,
            data,
            len,
            runtime_timeout_ms(),
            &done_us);
        if (err == ESP_OK && out_done_us) {
            *out_done_us = (nocti_time_us_t)done_us;
        }
    } else if (nocti_hal_i2c_is_async(s.cfg.i2c)) {
        // Never fall back to generic stack-buffer APIs on an async bus.
        err = ESP_ERR_INVALID_STATE;
    } else {
        err = nocti_hal_i2c_write(s.cfg.i2c,
                                  s.cfg.i2c_addr_7bit,
                                  data,
                                  len,
                                  runtime_timeout_ms());
    }

    if (err != ESP_OK) {
        s.stats.runtime_write_error_count++;
        if (s.fast_path_ready) {
            s.stats.fast_path_error_count++;
        }
    }
    return err;
}

static esp_err_t pca_write_reg8_runtime(uint8_t reg, uint8_t value)
{
    const uint8_t transaction[2] = {reg, value};
    return pca_runtime_write_raw(transaction,
                                 sizeof(transaction),
                                 NULL);
}

static esp_err_t pca_write_reg8_runtime_fast(uint8_t reg,
                                             uint8_t value,
                                             nocti_time_us_t *out_done_us)
{
    const uint8_t transaction[2] = {reg, value};
    return pca_runtime_write_raw(transaction,
                                 sizeof(transaction),
                                 out_done_us);
}

static esp_err_t enter_sync_configuration_phase(void)
{
    s.fast_path_ready = false;
    s.stats.fast_path_ready = false;

    if (s.fast_ledout.registered) {
        const esp_err_t deinit_err =
            nocti_hal_i2c_async_reg8_deinit(&s.fast_ledout);
        if (deinit_err != ESP_OK) return deinit_err;
    }

    if (s.cfg.i2c->trans_queue_depth != 0u) {
        return nocti_hal_i2c_reconfigure_queue_depth(s.cfg.i2c, 0u);
    }
    return ESP_OK;
}

static esp_err_t configure_fast_path(void)
{
    s.fast_path_ready = false;
    s.stats.fast_path_ready = false;

    if (!s.cfg.enable_async_fast_path) {
        return ESP_OK;
    }

    if (s.fast_ledout.registered) {
        const esp_err_t deinit_err =
            nocti_hal_i2c_async_reg8_deinit(&s.fast_ledout);
        if (deinit_err != ESP_OK) return deinit_err;
    }

    const uint32_t queue_depth = async_queue_depth();
    if (queue_depth == 0u) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nocti_hal_i2c_reconfigure_queue_depth(
        s.cfg.i2c, queue_depth);
    if (err != ESP_OK) return err;

    err = nocti_hal_i2c_async_reg8_init(
        s.cfg.i2c, s.cfg.i2c_addr_7bit, &s.fast_ledout);
    if (err == ESP_OK) {
        s.fast_path_ready = true;
        s.stats.fast_path_ready = true;
        return ESP_OK;
    }

    // Do not leave a half-configured asynchronous bus behind. The PCA remains
    // programmed and OFF, while the HAL returns to the safe sync phase.
    (void)nocti_hal_i2c_reconfigure_queue_depth(s.cfg.i2c, 0u);
    return err;
}

static esp_err_t pca_write_burst_runtime(uint8_t start_reg,
                                         const uint8_t *data,
                                         uint16_t len,
                                         nocti_time_us_t *out_done_us)
{
    if (!data || len == 0u) return ESP_ERR_INVALID_ARG;
    if (len > NOCTI_LED_PCA_CHANNELS) return ESP_ERR_INVALID_SIZE;

    uint8_t transaction[1u + NOCTI_LED_PCA_CHANNELS];
    // AIF=1 selects the auto-increment behavior configured by MODE1.AI[1:0].
    transaction[0] = (uint8_t)(0x80u | (start_reg & 0x7Fu));
    memcpy(&transaction[1], data, len);
    return pca_runtime_write_raw(transaction,
                                 (size_t)(1u + len),
                                 out_done_us);
}


// Initialization and readback deliberately run while the HAL is in true
// synchronous mode (queue depth zero). Runtime writes move to a separate bus
// instance with persistent buffers and an ISR completion callback.
static esp_err_t pca_write_reg8_init_checked(uint8_t reg,
                                             uint8_t value,
                                             const char *group)
{
    for (uint8_t attempt = 0u; attempt < 2u; attempt++) {
        esp_err_t err = pca_write_reg8_init(reg, value);
        if (err != ESP_OK) return err;

        uint8_t readback = (uint8_t)~value;
        err = pca_read_reg8_init(reg, &readback);
        if (err != ESP_OK) return err;
        if (readback == value) return ESP_OK;

        NOCTI_LOGW(TAG,
                   "%s init readback retry reg=0x%02X wrote=0x%02X read=0x%02X attempt=%u",
                   group ? group : "PCA",
                   (unsigned)reg,
                   (unsigned)value,
                   (unsigned)readback,
                   (unsigned)(attempt + 1u));
        esp_rom_delay_us(50u);
    }

    uint8_t final_readback = 0xFFu;
    const esp_err_t read_err = pca_read_reg8_init(reg, &final_readback);
    if (read_err != ESP_OK) return read_err;
    NOCTI_LOGE(TAG,
               "%s init readback failed reg=0x%02X expected=0x%02X got=0x%02X",
               group ? group : "PCA",
               (unsigned)reg,
               (unsigned)value,
               (unsigned)final_readback);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t pca_write_linear_init(uint8_t start_reg,
                                       const uint8_t *data,
                                       uint16_t len,
                                       const char *group)
{
    if (!data || len == 0u) return ESP_ERR_INVALID_ARG;
    for (uint16_t i = 0u; i < len; i++) {
        const esp_err_t err = pca_write_reg8_init_checked(
            (uint8_t)(start_reg + i), data[i], group);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static inline void ledout_set_field_in_image(uint8_t image[6],
                                             uint8_t channel,
                                             uint8_t mode)
{
    const uint8_t register_index = (uint8_t)(channel / 4u);
    const uint8_t bit_position = (uint8_t)((channel % 4u) * 2u);
    const uint8_t mask = (uint8_t)(0x3u << bit_position);

    image[register_index] =
        (uint8_t)((image[register_index] & (uint8_t)(~mask)) |
                  (uint8_t)((mode & 0x3u) << bit_position));
}

static inline uint8_t ledout_get_field_from_cache(uint8_t channel)
{
    const uint8_t register_index = (uint8_t)(channel / 4u);
    const uint8_t bit_position = (uint8_t)((channel % 4u) * 2u);
    return (uint8_t)((s.ledout_cache[register_index] >> bit_position) & 0x3u);
}

static bool image_is_all_off(const uint8_t image[6])
{
    for (uint8_t i = 0u; i < 6u; i++) {
        if (image[i] != 0u) return false;
    }
    return true;
}

static uint32_t reset_low_us(void)
{
    return s.cfg.hardware_reset_low_us ?
        s.cfg.hardware_reset_low_us :
        PCA_RESET_LOW_DEFAULT_US;
}

static uint32_t reset_recovery_us(void)
{
    return s.cfg.hardware_reset_recovery_us ?
        s.cfg.hardware_reset_recovery_us :
        PCA_RESET_RECOVERY_DEFAULT_US;
}

static void mark_ledout_unknown(void)
{
    s.ledout_cache_valid = false;
    s.last_on_ch = NOCTI_LED_MAP_UNUSED;
    s.stats.ledout_state_known = false;
}

static esp_err_t hardware_reset_to_safe_off(void)
{
    if (s.cfg.gpio_reset < 0) return ESP_ERR_NOT_SUPPORTED;

    esp_err_t err = gpio_set_level(s.cfg.gpio_reset, 0);
    if (err != ESP_OK) {
        mark_ledout_unknown();
        s.device_ready = false;
        return err;
    }

    esp_rom_delay_us(reset_low_us());

    err = gpio_set_level(s.cfg.gpio_reset, 1);
    if (err != ESP_OK) {
        mark_ledout_unknown();
        s.device_ready = false;
        return err;
    }

    // RESET returns all registers to their power-up state. Keep the desired
    // IREF cache for recovery, but mark the device unconfigured until its
    // registers have been written again.
    esp_rom_delay_us(reset_recovery_us());
    memset(s.ledout_cache, 0, sizeof(s.ledout_cache));
    s.ledout_cache_valid = true;
    s.last_on_ch = NOCTI_LED_MAP_UNUSED;
    s.device_ready = false;
    s.stats.hardware_reset_count++;
    s.stats.ledout_state_known = true;
    s.stats.device_ready = false;
    return ESP_OK;
}

static void handle_runtime_i2c_failure(const char *operation)
{
    mark_ledout_unknown();

    if (!s.cfg.hardware_reset_fail_safe) {
        return;
    }

    const esp_err_t reset_err = hardware_reset_to_safe_off();
    if (reset_err != ESP_OK) {
        NOCTI_LOGE(TAG,
                   "%s I2C failure and hardware reset failed: %s",
                   operation ? operation : "PCA9956B",
                   esp_err_to_name(reset_err));
    } else {
        NOCTI_LOGE(TAG,
                   "%s I2C failure: PCA9956B reset to safe OFF; recovery required",
                   operation ? operation : "PCA9956B");
    }
}

static esp_err_t resolve_slot_map(void)
{
    for (uint8_t i = 0u; i < NOCTI_LED_SLOTS; i++) {
        s.slot_map[i] = NOCTI_LED_MAP_UNUSED;
    }

    if (s.cfg.use_nocti_default_map) {
        memcpy(s.slot_map, k_default_slot_to_pca, sizeof(s.slot_map));
    }

    for (uint8_t led = 0u; led < NOCTI_LED_SOURCES; led++) {
        for (uint8_t wavelength_index = 0u;
             wavelength_index < NOCTI_LED_WAVELENGTHS;
             wavelength_index++) {
            const uint8_t channel = s.cfg.chan_map[led][wavelength_index];
            if (channel == NOCTI_LED_MAP_UNUSED) continue;
            if (!is_valid_pca_ch(channel) || is_gap_channel(channel)) {
                return ESP_ERR_INVALID_ARG;
            }
            s.slot_map[led * 2u + wavelength_index] = channel;
        }
    }

    for (uint8_t slot = 0u; slot < NOCTI_LED_SLOTS; slot++) {
        const uint8_t channel = s.cfg.slot_to_pca[slot];
        if (channel == NOCTI_LED_MAP_UNUSED) continue;
        if (!is_valid_pca_ch(channel) || is_gap_channel(channel)) {
            return ESP_ERR_INVALID_ARG;
        }
        s.slot_map[slot] = channel;
    }

    bool used[NOCTI_LED_PCA_CHANNELS];
    memset(used, 0, sizeof(used));

    for (uint8_t slot = 0u; slot < NOCTI_LED_SLOTS; slot++) {
        const uint8_t channel = s.slot_map[slot];
        if (channel == NOCTI_LED_MAP_UNUSED) return ESP_ERR_INVALID_STATE;
        if (!is_valid_pca_ch(channel) || is_gap_channel(channel)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (used[channel]) {
            NOCTI_LOGE(TAG,
                       "duplicate mapping: slot %u shares PCA channel %u",
                       (unsigned)slot,
                       (unsigned)channel);
            return ESP_ERR_INVALID_ARG;
        }
        used[channel] = true;
    }

    return ESP_OK;
}

static esp_err_t slot_struct_to_index(const nocti_slot_t *slot,
                                      uint8_t *out_slot_index)
{
    if (!slot || !out_slot_index) return ESP_ERR_INVALID_ARG;
    if (slot->led_id >= NOCTI_LED_SOURCES) return ESP_ERR_INVALID_ARG;
    if (!is_valid_wavelength(slot->wavelength)) return ESP_ERR_INVALID_ARG;

    *out_slot_index = NOCTI_SLOT_INDEX(slot->led_id, slot->wavelength);
    if (*out_slot_index >= NOCTI_LED_SLOTS) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t verify_init_registers(uint8_t expected_mode1,
                                       uint8_t expected_mode2)
{
    uint8_t mode1 = 0u;
    uint8_t mode2 = 0u;

    esp_err_t err = pca_read_reg8_init(PCA9956_REG_MODE1, &mode1);
    if (err != ESP_OK) return err;
    err = pca_read_reg8_init(PCA9956_REG_MODE2, &mode2);
    if (err != ESP_OK) return err;

    const bool mode1_match =
        (mode1 & PCA9956_MODE1_VERIFY_MASK) ==
        (expected_mode1 & PCA9956_MODE1_VERIFY_MASK);

    const bool mode2_config_match =
        (mode2 & PCA9956_MODE2_VERIFY_MASK) ==
        (expected_mode2 & PCA9956_MODE2_VERIFY_MASK);

    const uint8_t mode2_reserved_read =
        (uint8_t)(mode2 & PCA9956_MODE2_RESERVED_MASK);

    // MODE2[2:0] are reserved/read-only. NXP documents 0b101, but the
    // Noctic_v1 populated device consistently reads 0b000 while all writable
    // configuration bits and functional registers behave correctly. Reserved
    // bits must not be used as a hard silicon-identity check. Reject only a
    // mismatch in writable configuration fields.
    if (!mode1_match || !mode2_config_match) {
        NOCTI_LOGE(TAG,
                   "register readback mismatch "
                   "MODE1=0x%02X expected_cfg=0x%02X mask=0x%02X "
                   "MODE2=0x%02X expected_cfg=0x%02X mask=0x%02X reserved=0x%02X",
                   (unsigned)mode1,
                   (unsigned)expected_mode1,
                   (unsigned)PCA9956_MODE1_VERIFY_MASK,
                   (unsigned)mode2,
                   (unsigned)expected_mode2,
                   (unsigned)PCA9956_MODE2_VERIFY_MASK,
                   (unsigned)mode2_reserved_read);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (mode2_reserved_read != PCA9956_MODE2_RESERVED_READ) {
        NOCTI_LOGW(TAG,
                   "MODE2 reserved bits read 0x%02X (datasheet 0x%02X); "
                   "accepted because writable DMBLNK/OCH bits match",
                   (unsigned)mode2_reserved_read,
                   (unsigned)PCA9956_MODE2_RESERVED_READ);
    }

    // OVERTEMP and ERROR are live status bits, not configuration readback.
    // Report them without rejecting initialization solely because a status flag is set.
    if ((mode2 & 0xC0u) != 0u) {
        NOCTI_LOGW(TAG,
                   "PCA9956B MODE2 status flags active after init: 0x%02X",
                   (unsigned)(mode2 & 0xC0u));
    }

    for (uint8_t reg = PCA9956_REG_LEDOUT0;
         reg <= PCA9956_REG_LEDOUT5;
         reg++) {
        uint8_t value = 0xFFu;
        err = pca_read_reg8_init(reg, &value);
        if (err != ESP_OK) return err;
        if (value != 0u) {
            NOCTI_LOGE(TAG,
                       "LEDOUT readback not OFF reg=0x%02X value=0x%02X",
                       (unsigned)reg,
                       (unsigned)value);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    return ESP_OK;
}

static esp_err_t program_device_registers(void)
{
    if (nocti_hal_i2c_is_async(s.cfg.i2c)) {
        NOCTI_LOGE(TAG, "PCA configuration refused on async bus");
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t mode1 = PCA9956_MODE1_AI1;
    const uint8_t mode2 = 0x00u; // OCH=0: outputs update together at STOP.
    const uint8_t all_off[6] = {0u, 0u, 0u, 0u, 0u, 0u};

    esp_err_t err = pca_write_reg8_init(PCA9956_REG_MODE1, mode1);
    if (err != ESP_OK) return err;
    err = pca_write_reg8_init(PCA9956_REG_MODE2, mode2);
    if (err != ESP_OK) return err;

    if (s.cfg.force_disable_pwm_regs) {
        err = pca_write_reg8_init(PCA9956_REG_GRPPWM, 0x00u);
        if (err != ESP_OK) return err;
        err = pca_write_reg8_init(PCA9956_REG_GRPFREQ, 0x00u);
        if (err != ESP_OK) return err;
        err = pca_write_reg8_init(PCA9956_REG_PWMALL, 0x00u);
        if (err != ESP_OK) return err;
    }

    err = pca_write_linear_init(PCA9956_REG_LEDOUT0,
                                all_off,
                                sizeof(all_off),
                                "LEDOUT");
    if (err != ESP_OK) return err;

    err = pca_write_linear_init(PCA9956_REG_IREF0,
                                s.iref_cache,
                                NOCTI_LED_PCA_CHANNELS,
                                "IREF");
    if (err != ESP_OK) {
        s.iref_cache_valid = false;
        return err;
    }

    memcpy(s.ledout_cache, all_off, sizeof(s.ledout_cache));
    s.ledout_cache_valid = true;
    s.iref_cache_valid = true;
    s.last_on_ch = NOCTI_LED_MAP_UNUSED;

    if (s.cfg.verify_init_registers) {
        err = verify_init_registers(mode1, mode2);
        if (err != ESP_OK) {
            s.device_ready = false;
            return err;
        }
    }

    s.device_ready = true;
    s.stats.ledout_state_known = true;
    s.stats.device_ready = true;
    return ESP_OK;
}

void nocti_led_driver_cfg_set_defaults(nocti_led_driver_cfg_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    for (uint8_t led = 0u; led < NOCTI_LED_SOURCES; led++) {
        for (uint8_t wavelength_index = 0u;
             wavelength_index < NOCTI_LED_WAVELENGTHS;
             wavelength_index++) {
            cfg->chan_map[led][wavelength_index] = NOCTI_LED_MAP_UNUSED;
        }
    }
    for (uint8_t slot = 0u; slot < NOCTI_LED_SLOTS; slot++) {
        cfg->slot_to_pca[slot] = NOCTI_LED_MAP_UNUSED;
    }

    cfg->use_nocti_default_map = true;
    cfg->force_off_before_on = false;
    cfg->apply_current_in_slot = false;
    cfg->cleanup_missed_slot_end = true;
    cfg->force_disable_pwm_regs = true;
    cfg->atomic_one_hot_slots = true;
    cfg->enable_async_fast_path = false;
    cfg->async_queue_depth = 4u;
    cfg->init_i2c_timeout_ms = PCA_INIT_TIMEOUT_DEFAULT_MS;
    cfg->runtime_i2c_timeout_ms = PCA_RUNTIME_TIMEOUT_DEFAULT_MS;
    cfg->verify_init_registers = true;
    cfg->gpio_reset = -1;
    cfg->hardware_reset_fail_safe = false;
    cfg->hardware_reset_low_us = PCA_RESET_LOW_DEFAULT_US;
    cfg->hardware_reset_recovery_us = PCA_RESET_RECOVERY_DEFAULT_US;
}

esp_err_t nocti_led_driver_init(const nocti_led_driver_cfg_t *cfg)
{
    if (!cfg || !cfg->i2c || cfg->rext_ohm == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->hardware_reset_fail_safe && cfg->gpio_reset < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s, 0, sizeof(s));
    s.cfg = *cfg;
    s.last_on_ch = NOCTI_LED_MAP_UNUSED;

    esp_err_t err = resolve_slot_map();
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "mapping resolve failed: %s", esp_err_to_name(err));
        return err;
    }

    memset(s.ledout_cache, 0, sizeof(s.ledout_cache));
    memset(s.iref_cache, 0, sizeof(s.iref_cache));

    const uint16_t current_735_ma =
        clamp_ma(s.cfg.default_current_ma_735);
    const uint16_t current_850_ma =
        clamp_ma(s.cfg.default_current_ma_850);

    for (uint8_t led = 0u; led < NOCTI_LED_SOURCES; led++) {
        const uint8_t channel_735 = s.slot_map[led * 2u];
        const uint8_t channel_850 = s.slot_map[led * 2u + 1u];
        s.iref_cache[channel_735] =
            ma_to_iref(current_735_ma, s.cfg.rext_ohm);
        s.iref_cache[channel_850] =
            ma_to_iref(current_850_ma, s.cfg.rext_ohm);
    }
    s.iref_cache_valid = true;
    s.inited = true;

    // ESP-IDF 5.5.x selects asynchronous behavior from the bus queue depth,
    // even before a callback is registered. All local-buffer writes and reads
    // therefore run on a separate qdepth=0 bus instance.
    err = enter_sync_configuration_phase();
    if (err != ESP_OK) {
        s.inited = false;
        return err;
    }

    NOCTI_LOGI(TAG,
               "PCA configuration phase: synchronous bus qdepth=0");

    // Start from a known hardware state when RESET# is available. This is also
    // able to recover a PCA9956B whose I2C state machine was left uncertain by
    // an earlier interrupted run.
    if (s.cfg.gpio_reset >= 0) {
        err = hardware_reset_to_safe_off();
        if (err != ESP_OK) {
            s.inited = false;
            return err;
        }
    }

    err = pca_detect_by_mode1_read();
    if (err != ESP_OK) {
        s.inited = false;
        return err;
    }

    err = program_device_registers();
    if (err != ESP_OK) {
        s.inited = false;
        return err;
    }

    err = configure_fast_path();
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "async LEDOUT fast path init failed: %s",
                   esp_err_to_name(err));
        s.device_ready = false;
        s.stats.device_ready = false;
        s.inited = false;
        return err;
    }

    NOCTI_LOGI(
        TAG,
        "init ok addr=0x%02X atomic_one_hot=%u async_fast=%u "
        "init_phase=sync runtime_phase=%s runtime_qdepth=%u "
        "persistent_tx=1 callback_timestamp=1 init_writes=single_reg_verified "
        "reset_fail_safe=%u init_to=%lu ms runtime_to=%lu ms",
        (unsigned)s.cfg.i2c_addr_7bit,
        s.cfg.atomic_one_hot_slots ? 1u : 0u,
        s.fast_path_ready ? 1u : 0u,
        s.fast_path_ready ? "async" : "sync",
        (unsigned)s.cfg.i2c->trans_queue_depth,
        s.cfg.hardware_reset_fail_safe ? 1u : 0u,
        (unsigned long)init_timeout_ms(),
        (unsigned long)runtime_timeout_ms());
    return ESP_OK;
}

esp_err_t nocti_led_driver_recover(void)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;

    // Acquisition must already be stopped. Tear down the callback context and
    // recreate the controller in true synchronous mode before touching any
    // register readback or stack-backed configuration buffer.
    esp_err_t err = enter_sync_configuration_phase();
    if (err != ESP_OK) return err;

    s.device_ready = false;
    s.stats.device_ready = false;

    if (s.cfg.gpio_reset >= 0) {
        err = hardware_reset_to_safe_off();
        if (err != ESP_OK) return err;
    } else {
        mark_ledout_unknown();
    }

    err = pca_detect_by_mode1_read();
    if (err != ESP_OK) return err;

    err = program_device_registers();
    if (err != ESP_OK) return err;

    err = configure_fast_path();
    if (err == ESP_OK) {
        NOCTI_LOGW(TAG,
                   "PCA9956B recovered: sync configuration -> async runtime qdepth=%u",
                   (unsigned)s.cfg.i2c->trans_queue_depth);
    } else {
        s.device_ready = false;
        s.stats.device_ready = false;
    }
    return err;
}

bool nocti_led_driver_is_ready(void)
{
    return s.inited && s.device_ready;
}

esp_err_t nocti_led_driver_all_off(void)
{
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;

    if (s.ledout_cache_valid && image_is_all_off(s.ledout_cache)) {
        s.last_on_ch = NOCTI_LED_MAP_UNUSED;
        s.stats.ledout_state_known = true;
        return ESP_OK;
    }

    const uint8_t all_off[6] = {0u, 0u, 0u, 0u, 0u, 0u};
    esp_err_t err = ESP_OK;
    for (uint8_t i = 0u; i < 6u; i++) {
        if (s.ledout_cache_valid && s.ledout_cache[i] == 0u) continue;
        err = pca_write_reg8_runtime(
            (uint8_t)(PCA9956_REG_LEDOUT0 + i), 0u);
        if (err != ESP_OK) break;
    }

    if (err == ESP_OK) {
        memcpy(s.ledout_cache, all_off, sizeof(s.ledout_cache));
        s.ledout_cache_valid = true;
        s.last_on_ch = NOCTI_LED_MAP_UNUSED;
        s.stats.all_off_write_count++;
        s.stats.ledout_state_known = true;
    } else {
        handle_runtime_i2c_failure("LEDOUT");
    }
    return err;
}

esp_err_t nocti_led_driver_set_channel_current_ma(uint8_t pca_ch,
                                                   uint16_t ma)
{
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;
    if (!is_valid_pca_ch(pca_ch) || is_gap_channel(pca_ch)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t iref = ma_to_iref(clamp_ma(ma), s.cfg.rext_ohm);
    if (s.iref_cache_valid && s.iref_cache[pca_ch] == iref) {
        return ESP_OK;
    }

    const esp_err_t err = pca_write_reg8_runtime(
        (uint8_t)(PCA9956_REG_IREF0 + pca_ch), iref);
    if (err == ESP_OK) {
        s.iref_cache[pca_ch] = iref;
    } else {
        s.iref_cache_valid = false;
        handle_runtime_i2c_failure("IREF");
    }
    return err;
}

esp_err_t nocti_led_driver_set_logical_current_ma(uint8_t led_id,
                                                   nocti_wavelength_t wl,
                                                   uint16_t ma)
{
    uint8_t channel = 0u;
    const esp_err_t err =
        nocti_led_driver_map_logical(led_id, wl, &channel);
    if (err != ESP_OK) return err;
    return nocti_led_driver_set_channel_current_ma(channel, ma);
}

esp_err_t nocti_led_driver_apply_slot_currents_ma(
    const uint16_t slot_ma[NOCTI_LED_SLOTS])
{
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;
    if (!slot_ma) return ESP_ERR_INVALID_ARG;

    uint8_t next_iref[NOCTI_LED_PCA_CHANNELS];
    memset(next_iref, 0, sizeof(next_iref));

    for (uint8_t slot = 0u; slot < NOCTI_LED_SLOTS; slot++) {
        const uint8_t channel = s.slot_map[slot];
        next_iref[channel] =
            ma_to_iref(clamp_ma(slot_ma[slot]), s.cfg.rext_ohm);
    }

    esp_err_t err = ESP_OK;
    for (uint8_t channel = 0u; channel < NOCTI_LED_PCA_CHANNELS; channel++) {
        if (s.iref_cache_valid && s.iref_cache[channel] == next_iref[channel]) {
            continue;
        }
        err = pca_write_reg8_runtime(
            (uint8_t)(PCA9956_REG_IREF0 + channel), next_iref[channel]);
        if (err != ESP_OK) break;
    }

    if (err == ESP_OK) {
        memcpy(s.iref_cache, next_iref, sizeof(s.iref_cache));
        s.iref_cache_valid = true;
    } else {
        s.iref_cache_valid = false;
        handle_runtime_i2c_failure("IREF");
    }
    return err;
}

static esp_err_t channel_on_timed(uint8_t pca_ch,
                                  nocti_time_us_t *out_edge_us)
{
    if (out_edge_us) *out_edge_us = 0;
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;
    if (!is_valid_pca_ch(pca_ch) || is_gap_channel(pca_ch)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s.ledout_cache_valid) {
        const esp_err_t recover_err = nocti_led_driver_all_off();
        if (recover_err != ESP_OK) return recover_err;
    }

    const uint8_t register_index = (uint8_t)(pca_ch / 4u);
    uint8_t next_register = s.ledout_cache[register_index];
    const uint8_t bit_position = (uint8_t)((pca_ch % 4u) * 2u);
    const uint8_t mask = (uint8_t)(0x3u << bit_position);
    next_register =
        (uint8_t)((next_register & (uint8_t)(~mask)) |
                  (uint8_t)(PCA9956_LDR_ON << bit_position));

    if (next_register == s.ledout_cache[register_index]) return ESP_OK;

    const esp_err_t err = pca_write_reg8_runtime_fast(
        (uint8_t)(PCA9956_REG_LEDOUT0 + register_index),
        next_register,
        out_edge_us);
    if (err == ESP_OK) {
        s.ledout_cache[register_index] = next_register;
        s.last_on_ch = pca_ch;
        s.stats.ledout_state_known = true;
    } else {
        handle_runtime_i2c_failure("LEDOUT");
    }
    return err;
}

static esp_err_t channel_off_timed(uint8_t pca_ch,
                                   nocti_time_us_t *out_edge_us)
{
    if (out_edge_us) *out_edge_us = 0;
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;
    if (!is_valid_pca_ch(pca_ch) || is_gap_channel(pca_ch)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s.ledout_cache_valid) {
        return nocti_led_driver_all_off();
    }

    const uint8_t register_index = (uint8_t)(pca_ch / 4u);
    uint8_t next_register = s.ledout_cache[register_index];
    const uint8_t bit_position = (uint8_t)((pca_ch % 4u) * 2u);
    const uint8_t mask = (uint8_t)(0x3u << bit_position);
    next_register &= (uint8_t)(~mask);

    if (next_register == s.ledout_cache[register_index]) return ESP_OK;

    const esp_err_t err = pca_write_reg8_runtime_fast(
        (uint8_t)(PCA9956_REG_LEDOUT0 + register_index),
        next_register,
        out_edge_us);
    if (err == ESP_OK) {
        s.ledout_cache[register_index] = next_register;
        if (s.last_on_ch == pca_ch) {
            s.last_on_ch = NOCTI_LED_MAP_UNUSED;
        }
        s.stats.ledout_state_known = true;
    } else {
        handle_runtime_i2c_failure("LEDOUT");
    }
    return err;
}

esp_err_t nocti_led_driver_channel_on(uint8_t pca_ch)
{
    return channel_on_timed(pca_ch, NULL);
}

esp_err_t nocti_led_driver_channel_off(uint8_t pca_ch)
{
    return channel_off_timed(pca_ch, NULL);
}

esp_err_t nocti_led_driver_map_logical(uint8_t led_id,
                                       nocti_wavelength_t wl,
                                       uint8_t *out_pca_ch)
{
    if (!out_pca_ch) return ESP_ERR_INVALID_ARG;
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (led_id >= NOCTI_LED_SOURCES || !is_valid_wavelength(wl)) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_pca_ch = s.slot_map[NOCTI_SLOT_INDEX(led_id, wl)];
    return ESP_OK;
}

esp_err_t nocti_led_driver_map_slot(uint8_t slot_index,
                                    uint8_t *out_pca_ch)
{
    if (!out_pca_ch) return ESP_ERR_INVALID_ARG;
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (slot_index >= NOCTI_LED_SLOTS) return ESP_ERR_INVALID_ARG;

    *out_pca_ch = s.slot_map[slot_index];
    return ESP_OK;
}

esp_err_t nocti_led_driver_get_resolved_slot_map(
    uint8_t out_slot_to_pca[NOCTI_LED_SLOTS])
{
    if (!out_slot_to_pca) return ESP_ERR_INVALID_ARG;
    if (!s.inited) return ESP_ERR_INVALID_STATE;

    memcpy(out_slot_to_pca, s.slot_map, NOCTI_LED_SLOTS);
    return ESP_OK;
}

esp_err_t nocti_led_driver_slot_begin_timed(const nocti_slot_t *slot,
                                             nocti_time_us_t *out_edge_us)
{
    if (out_edge_us) *out_edge_us = 0;
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;

    uint8_t slot_index = 0u;
    esp_err_t err = slot_struct_to_index(slot, &slot_index);
    if (err != ESP_OK) return err;

    const uint8_t channel = s.slot_map[slot_index];

    if (s.cfg.apply_current_in_slot) {
        err = nocti_led_driver_set_channel_current_ma(channel,
                                                       slot->current_mA);
        if (err != ESP_OK) return err;
    }

    if (s.cfg.atomic_one_hot_slots) {
        // The complete one-hot image also uses the persistent callback buffer;
        // no stack pointer is retained after this function submits the transfer.
        uint8_t one_hot[6] = {0u, 0u, 0u, 0u, 0u, 0u};
        ledout_set_field_in_image(one_hot, channel, PCA9956_LDR_ON);

        const esp_err_t write_err = pca_write_burst_runtime(
            PCA9956_REG_LEDOUT0, one_hot, sizeof(one_hot), out_edge_us);
        if (write_err == ESP_OK) {
            memcpy(s.ledout_cache, one_hot, sizeof(s.ledout_cache));
            s.ledout_cache_valid = true;
            s.last_on_ch = channel;
            s.stats.atomic_slot_begin_count++;
            s.stats.ledout_state_known = true;
        } else {
            handle_runtime_i2c_failure("LEDOUT");
        }
        return write_err;
    }

    if (s.cfg.cleanup_missed_slot_end &&
        s.last_on_ch != NOCTI_LED_MAP_UNUSED &&
        s.last_on_ch != channel &&
        (!s.ledout_cache_valid ||
         ledout_get_field_from_cache(s.last_on_ch) == PCA9956_LDR_ON)) {
        nocti_time_us_t cleanup_edge_us = 0;
        err = channel_off_timed(s.last_on_ch, &cleanup_edge_us);
        if (err != ESP_OK) return err;
    }

    if (s.cfg.force_off_before_on) {
        nocti_time_us_t forced_off_edge_us = 0;
        err = channel_off_timed(channel, &forced_off_edge_us);
        if (err != ESP_OK) return err;
    }

    return channel_on_timed(channel, out_edge_us);
}

esp_err_t nocti_led_driver_slot_end_timed(const nocti_slot_t *slot,
                                           nocti_time_us_t *out_edge_us)
{
    if (out_edge_us) *out_edge_us = 0;
    if (!s.inited || !s.device_ready) return ESP_ERR_INVALID_STATE;

    uint8_t slot_index = 0u;
    const esp_err_t err = slot_struct_to_index(slot, &slot_index);
    if (err != ESP_OK) return err;

    if (s.cfg.atomic_one_hot_slots) {
        return nocti_led_driver_all_off();
    }

    return channel_off_timed(s.slot_map[slot_index], out_edge_us);
}

esp_err_t nocti_led_driver_slot_begin(const nocti_slot_t *slot)
{
    return nocti_led_driver_slot_begin_timed(slot, NULL);
}

esp_err_t nocti_led_driver_slot_end(const nocti_slot_t *slot)
{
    return nocti_led_driver_slot_end_timed(slot, NULL);
}

void nocti_led_driver_get_stats(nocti_led_driver_stats_t *out_stats)
{
    if (!out_stats) return;
    *out_stats = s.stats;
    out_stats->i2c_init_core_id = s.fast_ledout.init_core_id;
    out_stats->i2c_callback_core_id = s.fast_ledout.callback_core_id;
    out_stats->fast_path_ready = s.fast_path_ready;
    out_stats->ledout_state_known = s.ledout_cache_valid;
    out_stats->device_ready = s.inited && s.device_ready;
}

void nocti_led_driver_reset_stats(void)
{
    const bool state_known = s.ledout_cache_valid;
    const bool device_ready = s.inited && s.device_ready;
    const bool fast_path_ready = s.fast_path_ready;
    const int8_t i2c_init_core_id = s.fast_ledout.init_core_id;
    const int8_t i2c_callback_core_id = s.fast_ledout.callback_core_id;
    memset(&s.stats, 0, sizeof(s.stats));
    s.stats.i2c_init_core_id = i2c_init_core_id;
    s.stats.i2c_callback_core_id = i2c_callback_core_id;
    s.stats.fast_path_ready = fast_path_ready;
    s.stats.ledout_state_known = state_known;
    s.stats.device_ready = device_ready;
}
