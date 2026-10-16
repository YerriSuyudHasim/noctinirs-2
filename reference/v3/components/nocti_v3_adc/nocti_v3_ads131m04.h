#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "nocti_v3_board.h"
#include "nocti_v3_config.h"
#include "nocti_v3_timebase.h"

typedef struct {
    uint16_t response;
    uint16_t status;
    int32_t channel[4];
    uint16_t crc_received;
    uint16_t crc_calculated;
    bool crc_valid;
    bool resync_occurred;
    bool status_valid;
} nocti_v3_ads_frame_t;

typedef struct {
    uint16_t id;
    uint16_t status;
    uint16_t mode;
    uint16_t clock;
    uint16_t gain;
    uint16_t cfg;
} nocti_v3_ads_registers_t;

typedef struct {
    bool bus_initialized;
    bool device_added;
    bool bus_acquired;
    nocti_v3_board_t *board;
    nocti_v3_timebase_t *timebase;
    spi_device_handle_t device;
    spi_transaction_t transaction;
    uint8_t tx[NOCTI_V3_ADS_FRAME_BYTES];
    uint8_t rx[NOCTI_V3_ADS_FRAME_BYTES];
    nocti_v3_ads_registers_t registers;
} nocti_v3_ads131m04_t;

esp_err_t nocti_v3_ads131m04_init(nocti_v3_ads131m04_t *ads,
                                  nocti_v3_board_t *board,
                                  nocti_v3_timebase_t *timebase);
esp_err_t nocti_v3_ads131m04_deinit(nocti_v3_ads131m04_t *ads);
esp_err_t nocti_v3_ads131m04_acquire_bus(nocti_v3_ads131m04_t *ads);
void nocti_v3_ads131m04_release_bus(nocti_v3_ads131m04_t *ads);
esp_err_t nocti_v3_ads131m04_hardware_reset(nocti_v3_ads131m04_t *ads);
esp_err_t nocti_v3_ads131m04_sync(nocti_v3_ads131m04_t *ads);
esp_err_t nocti_v3_ads131m04_configure_8ksps(nocti_v3_ads131m04_t *ads);
esp_err_t nocti_v3_ads131m04_read_frame(nocti_v3_ads131m04_t *ads,
                                        nocti_v3_ads_frame_t *out_frame,
                                        uint32_t *out_spi_duration_us,
                                        uint64_t *out_spi_complete_timestamp_us);
esp_err_t nocti_v3_ads131m04_read_registers(nocti_v3_ads131m04_t *ads,
                                            nocti_v3_ads_registers_t *out_regs);
