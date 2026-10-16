// ============================================================
// File: components/nocti_hal_i2c/include/nocti_hal_i2c.h
// ESP-IDF 5.5.x I2C master HAL for NoctiNIRS
//
// Design rule:
// - qdepth=0 is the only mode used for probe/config/readback.
// - qdepth>0 is reserved for a persistent-buffer callback fast path.
// - The bus is recreated when changing between those phases; synchronous and
//   asynchronous traffic are never mixed on one bus instance.
// ============================================================

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NOCTI_HAL_I2C_DEFAULT_TIMEOUT_MS
#define NOCTI_HAL_I2C_DEFAULT_TIMEOUT_MS 50u
#endif

#ifndef NOCTI_HAL_I2C_MAX_DEV_CACHE
#define NOCTI_HAL_I2C_MAX_DEV_CACHE 8
#endif

#ifndef NOCTI_HAL_I2C_DEFAULT_SCL_WAIT_US
#define NOCTI_HAL_I2C_DEFAULT_SCL_WAIT_US 1000u
#endif

#ifndef NOCTI_HAL_I2C_ASYNC_TX_MAX_BYTES
// PCA9956B longest current use is one control byte plus 24 IREF bytes.
#define NOCTI_HAL_I2C_ASYNC_TX_MAX_BYTES 32u
#endif

typedef struct {
    i2c_port_t i2c_port;
    int gpio_sda;
    int gpio_scl;
    uint32_t clk_speed_hz;

    bool enable_internal_pullups;
    bool delete_driver_on_deinit;

    uint32_t default_timeout_ms;

    // 0: true synchronous ESP-IDF path.
    // >0: bus-wide asynchronous queue. Do not use generic stack-buffer APIs.
    uint32_t trans_queue_depth;

    uint32_t scl_wait_us;
} nocti_hal_i2c_cfg_t;

typedef struct {
    uint8_t addr_7bit;
    void *dev_handle;   // i2c_master_dev_handle_t
    bool in_use;
} nocti_hal_i2c_dev_cache_t;

typedef struct {
    i2c_port_t i2c_port;
    int gpio_sda;
    int gpio_scl;
    uint32_t clk_speed_hz;

    SemaphoreHandle_t mutex;

    bool enable_internal_pullups;
    bool delete_driver_on_deinit;
    bool installed;

    uint8_t glitch_ignore_cnt;
    int intr_priority;

    void *bus_handle;   // i2c_master_bus_handle_t

    nocti_hal_i2c_dev_cache_t dev_cache[NOCTI_HAL_I2C_MAX_DEV_CACHE];

    uint32_t default_timeout_ms;
    uint32_t trans_queue_depth;
    uint32_t scl_wait_us;

    // Enforce the ESP-IDF restriction: only one callback-driven device per bus.
    void *async_context;
} nocti_hal_i2c_t;

// Persistent-buffer asynchronous writer. A single context owns the callback
// for one cached device handle and allows only one active transaction.
typedef struct {
    nocti_hal_i2c_t *owner;
    void *dev_handle;             // i2c_master_dev_handle_t
    SemaphoreHandle_t done_sem;
    volatile i2c_master_event_t last_event;
    volatile int64_t completion_us;
    volatile bool completion_time_valid;
    int8_t init_core_id;
    volatile int8_t callback_core_id;
    uint8_t tx_buffer[NOCTI_HAL_I2C_ASYNC_TX_MAX_BYTES];
    size_t tx_length;
    bool registered;
    volatile bool transaction_active;
    bool poisoned;
} nocti_hal_i2c_async_reg8_t;

// Lifecycle
esp_err_t nocti_hal_i2c_init(nocti_hal_i2c_t *h,
                             const nocti_hal_i2c_cfg_t *cfg);
esp_err_t nocti_hal_i2c_deinit(nocti_hal_i2c_t *h);

// Recreate the controller on the same port/pins with a new queue depth.
// Call only while higher-level users are idle. All cached device handles are
// invalidated and recreated lazily after this call.
esp_err_t nocti_hal_i2c_reconfigure_queue_depth(nocti_hal_i2c_t *h,
                                                 uint32_t new_queue_depth);
bool nocti_hal_i2c_is_async(const nocti_hal_i2c_t *h);

// Lock
esp_err_t nocti_hal_i2c_lock(nocti_hal_i2c_t *h, uint32_t timeout_ms);
void nocti_hal_i2c_unlock(nocti_hal_i2c_t *h);

// Probe. Supported only while queue depth is zero.
esp_err_t nocti_hal_i2c_probe(nocti_hal_i2c_t *h,
                              uint8_t dev_addr_7bit,
                              uint32_t timeout_ms);

// Generic primitives. These deliberately reject an asynchronous bus because
// ESP-IDF 5.5.x can return before stack/local buffers are complete whenever
// trans_queue_depth is non-zero.
esp_err_t nocti_hal_i2c_write(nocti_hal_i2c_t *h,
                              uint8_t dev_addr_7bit,
                              const uint8_t *data,
                              size_t len,
                              uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_read(nocti_hal_i2c_t *h,
                             uint8_t dev_addr_7bit,
                             uint8_t *data,
                             size_t len,
                             uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_write_then_read(nocti_hal_i2c_t *h,
                                        uint8_t dev_addr_7bit,
                                        const uint8_t *tx,
                                        size_t tx_len,
                                        uint8_t *rx,
                                        size_t rx_len,
                                        uint32_t timeout_ms);

// Register helpers
esp_err_t nocti_hal_i2c_write_reg8(nocti_hal_i2c_t *h,
                                   uint8_t dev_addr_7bit,
                                   uint8_t reg,
                                   const uint8_t *data,
                                   size_t len,
                                   uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_read_reg8(nocti_hal_i2c_t *h,
                                  uint8_t dev_addr_7bit,
                                  uint8_t reg,
                                  uint8_t *data,
                                  size_t len,
                                  uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_write_reg16(nocti_hal_i2c_t *h,
                                    uint8_t dev_addr_7bit,
                                    uint16_t reg,
                                    const uint8_t *data,
                                    size_t len,
                                    uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_read_reg16(nocti_hal_i2c_t *h,
                                   uint8_t dev_addr_7bit,
                                   uint16_t reg,
                                   uint8_t *data,
                                   size_t len,
                                   uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_read_u8(nocti_hal_i2c_t *h,
                                uint8_t dev_addr_7bit,
                                uint8_t reg,
                                uint8_t *val,
                                uint32_t timeout_ms);

esp_err_t nocti_hal_i2c_write_u8(nocti_hal_i2c_t *h,
                                 uint8_t dev_addr_7bit,
                                 uint8_t reg,
                                 uint8_t val,
                                 uint32_t timeout_ms);

// Register one device callback on an already asynchronous bus.
esp_err_t nocti_hal_i2c_async_reg8_init(nocti_hal_i2c_t *h,
                                        uint8_t dev_addr_7bit,
                                        nocti_hal_i2c_async_reg8_t *ctx);

// Copy caller data into persistent storage, submit one asynchronous transfer,
// wait for its ISR callback, and return the callback timestamp from the
// common Nocti GPTimer timebase.
esp_err_t nocti_hal_i2c_async_write_wait(
    nocti_hal_i2c_async_reg8_t *ctx,
    const uint8_t *data,
    size_t len,
    uint32_t timeout_ms,
    int64_t *out_completion_us);

// Convenience [register,value] wrapper.
esp_err_t nocti_hal_i2c_async_reg8_write_wait(
    nocti_hal_i2c_async_reg8_t *ctx,
    uint8_t reg,
    uint8_t value,
    uint32_t timeout_ms,
    int64_t *out_completion_us);

esp_err_t nocti_hal_i2c_async_reg8_deinit(
    nocti_hal_i2c_async_reg8_t *ctx);

#ifdef __cplusplus
}
#endif
