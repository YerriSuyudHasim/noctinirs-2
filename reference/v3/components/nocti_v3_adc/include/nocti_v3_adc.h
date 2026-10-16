#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v3_board.h"
#include "nocti_v3_common.h"
#include "nocti_v3_health.h"
#include "nocti_v3_timebase.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t drdy_timestamp_us;
    uint64_t spi_complete_timestamp_us;
    uint32_t drdy_sequence;
    uint16_t ads_status;
    int32_t channel[4];
    uint16_t crc_received;
    bool crc_valid;
    bool status_valid;
    esp_err_t spi_result;
    uint32_t session_epoch;
    uint32_t optical_epoch_snapshot;
    uint16_t optical_state_snapshot;
} nocti_v3_raw_sample_t;

typedef struct {
    uint64_t adc_ok;
    uint64_t adc_missed;
    uint64_t adc_sequence_gap;
    uint64_t drdy_event_overflow;
    uint64_t adc_ring_overflow;
    uint64_t adc_period_gap;
    uint64_t crc_bad;
    uint64_t ads_status_errors;
    uint64_t spi_errors;
    uint32_t consecutive_spi_errors;
    uint32_t consecutive_crc_errors;
    uint32_t consecutive_status_errors;
    uint32_t consecutive_period_gaps;
    uint32_t adc_period_min_us;
    uint32_t adc_period_max_us;
    uint32_t adc_max_gap_us;
    uint32_t spi_duration_max_us;
    uint32_t adc_task_stack_watermark_bytes;
    uint32_t diagnostic_task_stack_watermark_bytes;
    uint32_t raw_ring_depth;
    uint32_t raw_ring_depth_max;
    uint32_t event_ring_depth_max;
    uint32_t session_epoch;
    uint32_t measured_sps;
    uint32_t measured_period_average_us_x1000;
    uint32_t inferred_clkin_hz;
    int adc_task_core;
    int diagnostic_task_core;
    int gpio_isr_init_core;
    int gpio_isr_callback_core;
    bool ready;
    bool acquisition_running;
    bool rate_valid;
    bool session_integrity_fault;
    nocti_v3_lifecycle_state_t lifecycle;
    esp_err_t last_error;
} nocti_v3_adc_health_snapshot_t;

typedef struct {
    uint16_t id;
    uint16_t status;
    uint16_t mode;
    uint16_t clock;
    uint16_t gain;
    uint16_t cfg;
} nocti_v3_adc_register_snapshot_t;

typedef struct {
    bool interface_initialized;
    bool hardware_initialized;
    bool acquisition_running;
    bool ready;
    uint32_t target_sample_rate_hz;
    uint32_t spi_clock_hz;
    uint8_t spi_mode;
    void *runtime;
} nocti_v3_adc_t;

/* Initializes persistent tasks and hardware. All allocations occur before START. */
esp_err_t nocti_v3_adc_init(nocti_v3_adc_t *adc,
                            nocti_v3_board_t *board,
                            nocti_v3_timebase_t *timebase,
                            nocti_v3_health_t *health);
esp_err_t nocti_v3_adc_start_dark(nocti_v3_adc_t *adc);
esp_err_t nocti_v3_adc_start_scan(nocti_v3_adc_t *adc);
esp_err_t nocti_v3_adc_bind_scan_consumer(nocti_v3_adc_t *adc, TaskHandle_t task);
bool nocti_v3_adc_pop_raw_sample(nocti_v3_adc_t *adc,
                                 nocti_v3_raw_sample_t *out_sample);
uint32_t nocti_v3_adc_raw_depth(nocti_v3_adc_t *adc);
bool nocti_v3_adc_drain_requested(nocti_v3_adc_t *adc);
esp_err_t nocti_v3_adc_acknowledge_drain(nocti_v3_adc_t *adc);
void nocti_v3_adc_set_optical_snapshot(nocti_v3_adc_t *adc,
                                       uint32_t optical_epoch,
                                       uint16_t optical_state);
esp_err_t nocti_v3_adc_stop(nocti_v3_adc_t *adc);
bool nocti_v3_adc_ready(const nocti_v3_adc_t *adc);
bool nocti_v3_adc_running(const nocti_v3_adc_t *adc);
void nocti_v3_adc_health_snapshot(nocti_v3_adc_t *adc,
                                  nocti_v3_adc_health_snapshot_t *out_snapshot);
void nocti_v3_adc_register_snapshot(nocti_v3_adc_t *adc,
                                    nocti_v3_adc_register_snapshot_t *out_snapshot);
esp_err_t nocti_v3_adc_reset_counters(nocti_v3_adc_t *adc);

#ifdef __cplusplus
}
#endif
