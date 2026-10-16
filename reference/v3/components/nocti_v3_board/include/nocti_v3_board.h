#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "nocti_v3_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOCTI_V3_BOARD_NAME "Noctic_v1"

typedef struct {
    int i2c_sda;
    int i2c_scl;
    int ads1299_drdy;
    int ads1299_miso;
    int ads1299_sclk;
    int ads1299_cs;
    int ads1299_start;
    int ads1299_reset;
    int ads1299_pwdn;
    int ads1299_mosi;
    int ads131_sync_reset;
    int ads131_cs;
    int ads131_drdy;
    int ads131_sclk;
    int ads131_miso;
    int ads131_mosi;
    int bq_ce;
    int pca_reset;
    int usb_dp;
    int usb_dm;
} nocti_v3_board_pins_t;

typedef struct {
    i2c_port_t i2c_port;
    uint32_t i2c_clock_hz;
    bool enable_internal_pullups;
    uint8_t pca9956b_address_7bit;
    uint8_t max17048_address_7bit;
    uint8_t bq24295_address_7bit;
    spi_host_device_t ads1299_spi_host;
    spi_host_device_t ads131_spi_host;
    uint32_t ads1299_spi_clock_hz;
    uint32_t ads131_spi_clock_hz;
    uint8_t ads1299_spi_mode;
    uint8_t ads131_spi_mode;
} nocti_v3_board_config_t;

typedef struct {
    bool gpio_safe;
    bool isr_service_ready;
    bool i2c_ready;
    bool pca_device_registered;
    bool pca_reset_asserted;
    uint32_t i2c_queue_depth;
    int i2c_init_core;
    nocti_v3_board_pins_t pins;
    nocti_v3_board_config_t config;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t pca_device;
} nocti_v3_board_t;

void nocti_v3_board_config_defaults(nocti_v3_board_config_t *config);
esp_err_t nocti_v3_board_early_safe_gpio(nocti_v3_board_t *board,
                                          const nocti_v3_board_config_t *config);
esp_err_t nocti_v3_board_init(nocti_v3_board_t *board,
                              const nocti_v3_board_config_t *config);
esp_err_t nocti_v3_board_deinit(nocti_v3_board_t *board);

/* I2C follows the hardware-validated V2 phased model:
 * - qdepth=0 bus for reset/configuration/readback;
 * - destroy/recreate as qdepth>0 before the callback-driven runtime path.
 * This avoids issuing stack-backed "synchronous" transactions on an async bus.
 * All phase changes occur while optical acquisition is stopped/recovering.
 */
esp_err_t nocti_v3_board_i2c_create(nocti_v3_board_t *board,
                                    uint32_t queue_depth);
esp_err_t nocti_v3_board_i2c_destroy(nocti_v3_board_t *board);
esp_err_t nocti_v3_board_i2c_reconfigure_queue_depth(nocti_v3_board_t *board,
                                                      uint32_t queue_depth);
esp_err_t nocti_v3_board_i2c_wait_all_done(nocti_v3_board_t *board,
                                           uint32_t timeout_ms);

esp_err_t nocti_v3_board_assert_pca_reset(nocti_v3_board_t *board);
esp_err_t nocti_v3_board_release_pca_reset(nocti_v3_board_t *board);
esp_err_t nocti_v3_board_assert_ads131_reset(nocti_v3_board_t *board);
esp_err_t nocti_v3_board_release_ads131_reset(nocti_v3_board_t *board);
esp_err_t nocti_v3_board_set_ads131_sync_reset(nocti_v3_board_t *board,
                                                bool high);
bool nocti_v3_board_led_fail_safe(const nocti_v3_board_t *board);
const nocti_v3_board_pins_t *nocti_v3_board_pins(const nocti_v3_board_t *board);
const nocti_v3_board_config_t *nocti_v3_board_config(const nocti_v3_board_t *board);

#ifdef __cplusplus
}
#endif
