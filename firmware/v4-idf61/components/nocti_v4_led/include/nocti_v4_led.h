#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v4_board.h"
#include "nocti_v4_health.h"
#include "nocti_v4_i2c.h"

#define NOCTI_V4_OPTICAL_SOURCE_COUNT 20u
#define NOCTI_V4_PCA_CHANNEL_COUNT 24u

typedef enum {
    NOCTI_V4_SAFE_OFF_UNKNOWN = 0,
    NOCTI_V4_SAFE_OFF_REQUESTED,
    NOCTI_V4_SAFE_OFF_REGISTER_VERIFIED,
    NOCTI_V4_SAFE_OFF_RESET_ASSERTED,
} nocti_v4_safe_off_state_t;

typedef enum {
    NOCTI_V4_I2C_FAST_QUAL_NOT_RUN = 0,
    NOCTI_V4_I2C_FAST_QUAL_PASS,
    NOCTI_V4_I2C_FAST_QUAL_FAIL,
} nocti_v4_i2c_fast_qual_state_t;

typedef struct {
    nocti_v4_i2c_fast_qual_state_t state;
    uint32_t configured_hz;
    uint32_t transactions_attempted;
    uint32_t transactions_ok;
    uint32_t communication_errors;
    uint32_t readback_mismatch;
    uint32_t stress_reads_target;
    uint32_t stress_reads_completed;
    uint32_t duration_min_us;
    uint32_t duration_max_us;
    uint32_t duration_avg_us;
    uint8_t mode1;
    uint8_t mode2;
    uint8_t ledout[6];
    uint8_t eflag[6];
    uint8_t mapped_iref[NOCTI_V4_OPTICAL_SOURCE_COUNT];
    bool restored_sync_configured;
    bool electrical_rise_fall_verified;
    esp_err_t last_result;
} nocti_v4_i2c_fast_qual_status_t;

typedef struct {
    nocti_v4_board_t *board;
    nocti_v4_i2c_t *i2c;
    nocti_v4_health_t *health;
    bool initialized;
    bool ledout_cache_valid;
    uint8_t ledout[6];
    uint8_t iref[NOCTI_V4_PCA_CHANNEL_COUNT];
    int32_t selected_source;
    nocti_v4_safe_off_state_t safe_off_state;
    esp_err_t last_result;
} nocti_v4_led_t;

esp_err_t nocti_v4_led_init(nocti_v4_led_t *led, nocti_v4_board_t *board,
                             nocti_v4_i2c_t *i2c, nocti_v4_health_t *health);
esp_err_t nocti_v4_led_all_off(nocti_v4_led_t *led);
esp_err_t nocti_v4_led_on(nocti_v4_led_t *led, uint8_t source);
esp_err_t nocti_v4_led_off(nocti_v4_led_t *led);
esp_err_t nocti_v4_led_set_iref_ma(nocti_v4_led_t *led, uint8_t source, uint16_t milliamps);
esp_err_t nocti_v4_led_reset_hold(nocti_v4_led_t *led);
esp_err_t nocti_v4_led_reinit(nocti_v4_led_t *led);
esp_err_t nocti_v4_led_readback(nocti_v4_led_t *led, bool print_result);
/* STOPPED-only ownership is enforced by app_main. This temporarily performs
 * read-only PCA traffic at the 800-kbit/s qualification rate, then always
 * attempts to restore and reinitialize the 100 kHz synchronous golden path. */
esp_err_t nocti_v4_led_run_fast_i2c_qualification(nocti_v4_led_t *led);
void nocti_v4_led_get_fast_i2c_qualification(
    nocti_v4_i2c_fast_qual_status_t *out_status);
void nocti_v4_led_print_fast_i2c_qualification(void);
void nocti_v4_led_print_status(const nocti_v4_led_t *led);
void nocti_v4_led_print_map(void);
const char *nocti_v4_safe_off_state_name(nocti_v4_safe_off_state_t state);
esp_err_t nocti_v4_led_resolve_source(uint8_t source, uint8_t *pca_channel);
