#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

/* Single source of truth for the V4-1/V4-2 Noctic_v1 PCA9956B path. */
#define NOCTI_V4_I2C_SDA_GPIO             GPIO_NUM_15
#define NOCTI_V4_I2C_SCL_GPIO             GPIO_NUM_16
#define NOCTI_V4_PCA_RESET_GPIO           GPIO_NUM_18
#define NOCTI_V4_PCA_ADDRESS_7BIT         0x01u
#define NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ   100000u
/* ESP32-S3 datasheet limit: up to 800 kbit/s, constrained by pull-up
 * strength. Runtime qualification therefore uses 800 kbit/s rather than the
 * PCA9956B's 1 MHz ceiling. The synchronous golden path remains 400 kHz. */
#define NOCTI_V4_I2C_SCAN_RUNTIME_HZ       800000u
#define NOCTI_V4_I2C_CLOCK_HZ              NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ
#define NOCTI_V4_I2C_EXTERNAL_PULLUP_OHM   4700u
#define NOCTI_V4_I2C_PULLUP_RAIL_MV        3300u
#define NOCTI_V4_I2C_TRANS_QUEUE_DEPTH    0u
/* V4-4 uses this only after dismantling the qdepth=0 diagnostic instance.
 * The two handles must never be mixed. */
#define NOCTI_V4_I2C_ASYNC_TRANS_QUEUE_DEPTH 4u
#define NOCTI_V4_PCA_REXT_OHM             1000u
#define NOCTI_V4_PCA_RESET_LOW_US         5u
#define NOCTI_V4_PCA_RESET_RECOVERY_US    1600u

/* Hardware-proven V2/V3 ADS131M04 physical pin map. CLKIN is board supplied;
 * firmware neither generates nor reconfigures it in V4-3. */
#define NOCTI_V4_ADS131_SYNC_RESET_GPIO   GPIO_NUM_6
#define NOCTI_V4_ADS131_CS_GPIO           GPIO_NUM_7
#define NOCTI_V4_ADS131_DRDY_GPIO         GPIO_NUM_35
#define NOCTI_V4_ADS131_SCLK_GPIO         GPIO_NUM_36
#define NOCTI_V4_ADS131_MISO_GPIO         GPIO_NUM_37
#define NOCTI_V4_ADS131_MOSI_GPIO         GPIO_NUM_38

typedef enum {
    NOCTI_V4_OE_PHYSICAL_STATE_UNVERIFIED = 0,
} nocti_v4_oe_state_t;

typedef struct {
    bool gpio_ready;
    bool pca_reset_asserted;
    bool ads131_reset_asserted;
    nocti_v4_oe_state_t oe_state;
} nocti_v4_board_t;

esp_err_t nocti_v4_board_early_safe_init(nocti_v4_board_t *board);
esp_err_t nocti_v4_board_assert_pca_reset(nocti_v4_board_t *board);
esp_err_t nocti_v4_board_release_pca_reset(nocti_v4_board_t *board);
esp_err_t nocti_v4_board_assert_ads131_reset(nocti_v4_board_t *board);
esp_err_t nocti_v4_board_release_ads131_reset(nocti_v4_board_t *board);
const char *nocti_v4_board_oe_state_name(nocti_v4_oe_state_t state);

/* Migration control gate: cancellation forbids late PCA reset release.
 * Configuration-only restore may reopen the gate after scheduler STOP. */
esp_err_t nocti_v4_board_set_emitter_gate(nocti_v4_board_t *board, bool allowed);
