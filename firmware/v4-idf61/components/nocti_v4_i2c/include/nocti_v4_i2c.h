#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "nocti_v4_board.h"
#include "nocti_v4_health.h"

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t pca;
    bool initialized;
    uint32_t clock_hz;
    uint32_t queue_depth;
    esp_err_t last_result;
} nocti_v4_i2c_t;

esp_err_t nocti_v4_i2c_init_sync(nocti_v4_i2c_t *i2c, nocti_v4_health_t *health);
/* Creates a fresh bus/device pair at an explicit clock. This is used only by
 * bounded mode transitions; callers must not retime a live handle. */
esp_err_t nocti_v4_i2c_init_at_speed(nocti_v4_i2c_t *i2c,
                                      nocti_v4_health_t *health,
                                      uint32_t queue_depth,
                                      uint32_t clock_hz);
/* Creates a fresh I2C0/PCA device pair at the specified queue depth. Callers
 * must destroy a prior pair first; this compatibility entry point uses the
 * 100 kHz synchronous diagnostic clock. */
esp_err_t nocti_v4_i2c_init(nocti_v4_i2c_t *i2c, nocti_v4_health_t *health,
                             uint32_t queue_depth);
/* The caller must first quiesce transactions and unregister any callback.
 * On success, both the device and bus handles are invalidated. */
esp_err_t nocti_v4_i2c_deinit(nocti_v4_i2c_t *i2c);
esp_err_t nocti_v4_i2c_write_reg(nocti_v4_i2c_t *i2c, uint8_t reg, uint8_t value,
                                  uint32_t timeout_ms, nocti_v4_health_t *health);
esp_err_t nocti_v4_i2c_read_reg(nocti_v4_i2c_t *i2c, uint8_t reg, uint8_t *value,
                                 uint32_t timeout_ms, nocti_v4_health_t *health);
