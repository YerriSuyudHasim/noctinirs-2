// ============================================================
// File: components/nocti_led_driver/include/nocti_led_driver.h
// ============================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_hal_i2c.h"
#include "nocti_scheduler.h"
#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOCTI_LED_PCA_CHANNELS          (24u)
#define NOCTI_LED_SOURCES               (10u)
#define NOCTI_LED_WAVELENGTHS           (2u)
#define NOCTI_LED_SLOTS                 (NOCTI_LED_SOURCES * NOCTI_LED_WAVELENGTHS)
#define NOCTI_LED_MAP_UNUSED            (0xFFu)

#define NOCTI_SLOT_INDEX(led_id, wl) \
    ((uint8_t)((uint8_t)(led_id) * 2u + \
    ((wl) == NOCTI_WL_735 ? 0u : 1u)))

typedef struct {
    nocti_hal_i2c_t *i2c;
    uint8_t i2c_addr_7bit;
    uint32_t rext_ohm;

    uint8_t chan_map[NOCTI_LED_SOURCES][NOCTI_LED_WAVELENGTHS];
    uint8_t slot_to_pca[NOCTI_LED_SLOTS];
    bool use_nocti_default_map;

    uint16_t default_current_ma_735;
    uint16_t default_current_ma_850;
    uint16_t max_current_ma;

    bool force_off_before_on;
    bool apply_current_in_slot;
    bool cleanup_missed_slot_end;
    bool force_disable_pwm_regs;

    // Recommended for NoctiNIRS v1. Every SLOT_BEGIN writes a complete six-byte
    // LEDOUT one-hot image. With MODE2.OCH=0, the PCA9956B changes all outputs
    // atomically at I2C STOP, preventing overlap between consecutive sources.
    bool atomic_one_hot_slots;

    // Use ESP-IDF asynchronous completion callbacks after synchronous boot
    // configuration has passed. The HAL recreates the bus before enabling this
    // phase; sync and async calls are never mixed on one controller instance.
    bool enable_async_fast_path;

    // Runtime queue depth used after PCA configuration/readback. A value of 0
    // selects the driver default (4) when enable_async_fast_path is true.
    uint16_t async_queue_depth;

    // Initialization may use a generous timeout, while runtime slot writes
    // should fail quickly rather than blocking several 5 ms optical slots.
    uint32_t init_i2c_timeout_ms;
    uint32_t runtime_i2c_timeout_ms;

    // Read back MODE1, MODE2 and LEDOUT during initialization.
    bool verify_init_registers;

    // NoctiNIRS v1 has RESET# routed to the ESP32 while OE# is tied LOW.
    // A hardware reset is therefore the only deterministic emergency path to
    // force every LED OFF if an LEDOUT I2C transaction fails.
    int gpio_reset;
    bool hardware_reset_fail_safe;
    uint16_t hardware_reset_low_us;
    uint16_t hardware_reset_recovery_us;
} nocti_led_driver_cfg_t;

typedef struct {
    uint32_t runtime_write_count;
    uint32_t runtime_write_error_count;
    uint32_t atomic_slot_begin_count;
    uint32_t all_off_write_count;
    uint32_t hardware_reset_count;
    uint32_t fast_path_write_count;
    uint32_t fast_path_error_count;
    int8_t i2c_init_core_id;
    int8_t i2c_callback_core_id;
    bool fast_path_ready;
    bool ledout_state_known;
    bool device_ready;
} nocti_led_driver_stats_t;

void nocti_led_driver_cfg_set_defaults(nocti_led_driver_cfg_t *cfg);
esp_err_t nocti_led_driver_init(const nocti_led_driver_cfg_t *cfg);

// Reprogram the PCA9956B after a fail-safe hardware reset. Acquisition must
// be stopped while this function runs.
esp_err_t nocti_led_driver_recover(void);
bool nocti_led_driver_is_ready(void);

esp_err_t nocti_led_driver_all_off(void);

esp_err_t nocti_led_driver_set_channel_current_ma(uint8_t pca_ch,
                                                   uint16_t ma);
esp_err_t nocti_led_driver_set_logical_current_ma(uint8_t led_id,
                                                   nocti_wavelength_t wl,
                                                   uint16_t ma);
esp_err_t nocti_led_driver_apply_slot_currents_ma(
    const uint16_t slot_ma[NOCTI_LED_SLOTS]);

esp_err_t nocti_led_driver_channel_on(uint8_t pca_ch);
esp_err_t nocti_led_driver_channel_off(uint8_t pca_ch);

esp_err_t nocti_led_driver_map_logical(uint8_t led_id,
                                       nocti_wavelength_t wl,
                                       uint8_t *out_pca_ch);
esp_err_t nocti_led_driver_map_slot(uint8_t slot_index,
                                    uint8_t *out_pca_ch);
esp_err_t nocti_led_driver_get_resolved_slot_map(
    uint8_t out_slot_to_pca[NOCTI_LED_SLOTS]);

esp_err_t nocti_led_driver_slot_begin(const nocti_slot_t *slot);
esp_err_t nocti_led_driver_slot_end(const nocti_slot_t *slot);

// Real-time variants return the transaction-done timestamp captured by the I2C
// ISR callback. With MODE2.OCH=0 this is a conservative post-STOP proxy for the
// PCA9956B output edge. A zero timestamp means
// no register write was required because the cached state already matched.
esp_err_t nocti_led_driver_slot_begin_timed(const nocti_slot_t *slot,
                                             nocti_time_us_t *out_edge_us);
esp_err_t nocti_led_driver_slot_end_timed(const nocti_slot_t *slot,
                                           nocti_time_us_t *out_edge_us);

void nocti_led_driver_get_stats(nocti_led_driver_stats_t *out_stats);
void nocti_led_driver_reset_stats(void);

#ifdef __cplusplus
}
#endif
