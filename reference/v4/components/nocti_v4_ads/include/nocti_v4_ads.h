#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v4_ads_config.h"
#include "nocti_v4_board.h"
#include "nocti_v4_health.h"
#include "nocti_v4_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOCTI_V4_ADS_RAW_STATS_WINDOW_SAMPLES 1024u
#define NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES 4u
#define NOCTI_V4_ADS_CH0_SELFTEST_SAMPLES 32u

typedef enum {
    NOCTI_V4_ADS_STOPPED = 0,
    NOCTI_V4_ADS_STARTING_DARK,
    NOCTI_V4_ADS_RUNNING_DARK,
    NOCTI_V4_ADS_STOPPING_DARK,
    NOCTI_V4_ADS_FAULTED,
} nocti_v4_ads_lifecycle_t;

/* The first fault is retained through DARK_STOP for post-mortem inspection and
 * is cleared only by the next accepted DARK_START. */
typedef enum {
    NOCTI_V4_ADS_FAULT_NONE = 0,
    NOCTI_V4_ADS_FAULT_START_BUS_ACQUIRE,
    NOCTI_V4_ADS_FAULT_START_SYNC,
    NOCTI_V4_ADS_FAULT_START_INTERRUPT,
    NOCTI_V4_ADS_FAULT_POST_SYNC_TIMEOUT,
    NOCTI_V4_ADS_FAULT_ISR_TIMESTAMP,
    NOCTI_V4_ADS_FAULT_EVENT_RING_OVERFLOW,
    NOCTI_V4_ADS_FAULT_EVENT_BACKLOG,
    NOCTI_V4_ADS_FAULT_EVENT_EPOCH,
    NOCTI_V4_ADS_FAULT_SEQUENCE_GAP,
    NOCTI_V4_ADS_FAULT_SPI_READ,
    NOCTI_V4_ADS_FAULT_CRC,
    NOCTI_V4_ADS_FAULT_STATUS,
    NOCTI_V4_ADS_FAULT_F_RESYNC,
    NOCTI_V4_ADS_FAULT_POST_SYNC_STATE,
    NOCTI_V4_ADS_FAULT_RAW_RING_OVERFLOW,
    NOCTI_V4_ADS_FAULT_DRDY_STALL,
    NOCTI_V4_ADS_FAULT_STOP_DRAIN_TIMEOUT,
} nocti_v4_ads_fault_code_t;

typedef enum {
    NOCTI_V4_ADS_FAULT_ORIGIN_NONE = 0,
    NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK,
    NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_ISR,
    NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT,
    NOCTI_V4_ADS_FAULT_ORIGIN_RUNTIME_SERVICE,
    NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_WATCHDOG,
    NOCTI_V4_ADS_FAULT_ORIGIN_STOP_DARK,
} nocti_v4_ads_fault_origin_t;

typedef struct {
    uint64_t drdy_timestamp_us;
    uint64_t spi_complete_timestamp_us;
    uint32_t drdy_sequence;
    uint16_t ads_status;
    int32_t channel[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t crc_received;
    bool crc_valid;
    bool status_valid;
    esp_err_t spi_result;
    uint32_t session_epoch;
} nocti_v4_raw_sample_t;

typedef enum {
    /* Default DARK diagnostic consumer owns raw SPSC-ring pops. */
    NOCTI_V4_ADS_RAW_CONSUMER_DARK_DIAGNOSTIC = 0,
    /* During V4-6 SCAN the optical task is the one and only raw-ring consumer. */
    NOCTI_V4_ADS_RAW_CONSUMER_OPTICAL,
} nocti_v4_ads_raw_consumer_mode_t;

typedef struct {
    uint16_t id;
    uint16_t status;
    uint16_t mode;
    uint16_t clock;
    uint16_t gain;
    uint16_t cfg;
    uint16_t channel_cfg[NOCTI_V4_ADS_CHANNEL_COUNT];
} nocti_v4_ads_register_snapshot_t;

typedef struct {
    bool interface_initialized;
    bool hardware_initialized;
    bool ready;
    bool rate_valid;
    bool session_integrity_fault;
    nocti_v4_ads_lifecycle_t lifecycle;
    uint32_t session_epoch;
    uint32_t configured_sps;
    uint32_t measured_sps;
    uint32_t validation_period_avg_us;
    uint32_t validation_period_min_us;
    uint32_t validation_period_max_us;
    uint32_t validation_samples;
    uint32_t raw_ring_depth;
    uint32_t raw_ring_depth_max;
    uint32_t drdy_event_ring_depth;
    uint32_t drdy_event_ring_depth_max;
    uint32_t sync_discard_remaining;
    uint32_t expected_resync_remaining;
    int32_t ads_task_core;
    int32_t dark_consumer_task_core;
    nocti_v4_ads_raw_consumer_mode_t raw_consumer_mode;
    int32_t gpio_isr_install_core;
    int32_t gpio_isr_callback_core;
    esp_err_t last_error;
    uint32_t drdy_isr_count;
    uint32_t drdy_events_pushed;
    uint32_t drdy_events_popped;
    uint32_t drdy_isr_timestamp_errors;
    uint32_t drdy_event_overflow;
    uint32_t ads_frames_read_total;
    uint32_t ads_frames_read_runtime;
    uint32_t post_sync_frames_read;
    uint32_t post_sync_frames_discarded;
    uint32_t raw_samples_pushed;
    uint32_t raw_samples_consumed;
    uint32_t adc_missed_sequence;
    uint32_t adc_missed_backlog;
    uint32_t adc_missed_other;
    uint32_t last_drdy_sequence;
    uint64_t last_drdy_timestamp_us;
    nocti_v4_ads_fault_code_t last_fault_code;
    nocti_v4_ads_fault_origin_t last_fault_origin;
    esp_err_t last_fault_error;
    uint32_t runtime_period_count;
    uint32_t runtime_period_us[NOCTI_V4_ADS_RUNTIME_PERIOD_SNAPSHOT_COUNT];
    uint32_t drdy_period_lt_100_us;
    uint32_t drdy_period_100_to_150_us;
    uint32_t drdy_period_150_to_250_us;
    uint32_t drdy_period_gt_250_us;
    uint32_t spi_duration_gt_50_us;
    uint32_t spi_duration_gt_100_us;
    uint32_t spi_duration_gt_250_us;
} nocti_v4_ads_status_snapshot_t;

typedef struct {
    bool valid;
    uint32_t session_epoch;
    uint32_t window_sequence;
    uint32_t count;
    int32_t minimum[NOCTI_V4_ADS_CHANNEL_COUNT];
    int32_t maximum[NOCTI_V4_ADS_CHANNEL_COUNT];
    int64_t mean[NOCTI_V4_ADS_CHANNEL_COUNT];
} nocti_v4_ads_raw_stats_snapshot_t;

typedef struct {
    bool valid;
    uint64_t drdy_timestamp_us;
    uint32_t drdy_sequence;
    uint32_t spi_duration_us;
    uint8_t raw[NOCTI_V4_ADS_FRAME_BYTES];
    uint32_t raw_word[NOCTI_V4_ADS_FRAME_WORDS];
    uint16_t status;
    int32_t channel[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t crc_received;
    uint16_t crc_calculated;
    bool crc_valid;
    bool status_valid;
    bool resync_occurred;
} nocti_v4_ads_frame_dump_record_t;

typedef struct {
    bool valid;
    uint8_t requested;
    uint8_t captured;
    esp_err_t result;
    nocti_v4_ads_frame_dump_record_t frame[NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES];
} nocti_v4_ads_frame_dump_snapshot_t;

typedef struct {
    bool valid;
    esp_err_t result;
    uint16_t channel_cfg[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t ocal_msb[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t ocal_lsb[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t gcal_msb[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t gcal_lsb[NOCTI_V4_ADS_CHANNEL_COUNT];
} nocti_v4_ads_channel_register_snapshot_t;

typedef struct {
    bool valid;
    bool signal_nonzero;
    bool restore_verified;
    esp_err_t result;
    esp_err_t restore_result;
    uint16_t original_ch0_cfg;
    uint16_t test_ch0_cfg;
    uint16_t restored_ch0_cfg;
    uint32_t samples_requested;
    uint32_t samples_captured;
    uint32_t crc_valid_frames;
    uint32_t status_valid_frames;
    uint32_t drdy0_new_data_frames;
    int32_t minimum;
    int32_t maximum;
    int64_t mean;
    nocti_v4_ads_frame_dump_record_t
        frame[NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES];
} nocti_v4_ads_ch0_selftest_snapshot_t;

typedef struct {
    bool interface_initialized;
    bool hardware_initialized;
    bool ready;
    void *runtime;
} nocti_v4_ads_t;

/* Creates the two persistent tasks and performs the isolated ADS bring-up and
 * measured-rate validation. It does not start a DARK session. */
esp_err_t nocti_v4_ads_init(nocti_v4_ads_t *ads,
                             nocti_v4_board_t *board,
                             nocti_v4_time_t *timebase,
                             nocti_v4_health_t *health);
esp_err_t nocti_v4_ads_start_dark(nocti_v4_ads_t *ads);
esp_err_t nocti_v4_ads_stop_dark(nocti_v4_ads_t *ads);
/* Bind the sole raw SPSC-ring consumer only while stopped and the ring is
 * empty. Passing NULL restores the DARK diagnostic consumer. The task
 * notification is only a wake edge; the ring remains the data authority. */
esp_err_t nocti_v4_ads_bind_raw_consumer(nocti_v4_ads_t *ads,
                                         TaskHandle_t optical_task);
/* Valid only for the currently bound external consumer. This is the single
 * raw-ring pop operation used by V4-6 optical processing during SCAN. */
bool nocti_v4_ads_pop_raw_for_bound_consumer(nocti_v4_ads_t *ads,
                                             nocti_v4_raw_sample_t *out_sample);
/* STOP asks the current owner to drain. The external optical owner calls this
 * after its final raw-ring drain; ADS retains transport/lifecycle ownership. */
bool nocti_v4_ads_bound_consumer_drain_requested(const nocti_v4_ads_t *ads);
void nocti_v4_ads_bound_consumer_ack_drain(nocti_v4_ads_t *ads);
bool nocti_v4_ads_is_active(const nocti_v4_ads_t *ads);
bool nocti_v4_ads_ready(const nocti_v4_ads_t *ads);
esp_err_t nocti_v4_ads_reset_counters(nocti_v4_ads_t *ads);
/* Explicit diagnostic sampling only. This deliberately never runs from either
 * acquisition task's DRDY/raw-sample service loop. */
void nocti_v4_ads_sample_stack_watermarks(nocti_v4_ads_t *ads);
void nocti_v4_ads_get_status(const nocti_v4_ads_t *ads,
                              nocti_v4_ads_status_snapshot_t *out_status);
void nocti_v4_ads_get_registers(const nocti_v4_ads_t *ads,
                                 nocti_v4_ads_register_snapshot_t *out_regs);
/* Last complete fixed 1,024-sample runtime window. Collection is passive in
 * the ADS producer; this snapshot never consumes or prints raw-ring data. */
void nocti_v4_ads_get_raw_stats(const nocti_v4_ads_t *ads,
                                 nocti_v4_ads_raw_stats_snapshot_t *out_stats);
/* STOPPED-only bounded wire diagnostic. The ADS owner task acquires SPI,
 * SYNCs, physically reads the three settling frames, then captures 1..4
 * complete 18-byte frames into persistent storage. No SCAN/DARK hot path
 * prints or allocates. */
esp_err_t nocti_v4_ads_capture_frame_dump(nocti_v4_ads_t *ads, uint8_t frame_count);
void nocti_v4_ads_print_frame_dump(const nocti_v4_ads_t *ads);
/* STOPPED-only readback of each five-register channel block. These APIs never
 * write calibration registers. */
esp_err_t nocti_v4_ads_capture_channel_registers(nocti_v4_ads_t *ads);
void nocti_v4_ads_print_channel_registers(const nocti_v4_ads_t *ads);
/* STOPPED-only, bounded CH0 positive-DC MUX test. Only CH0_CFG is changed;
 * the exact original value is restored and read-verified on every exit path. */
esp_err_t nocti_v4_ads_run_ch0_selftest(nocti_v4_ads_t *ads);
void nocti_v4_ads_print_ch0_selftest(const nocti_v4_ads_t *ads);
const char *nocti_v4_ads_lifecycle_name(nocti_v4_ads_lifecycle_t state);
const char *nocti_v4_ads_fault_code_name(nocti_v4_ads_fault_code_t code);
const char *nocti_v4_ads_fault_origin_name(nocti_v4_ads_fault_origin_t origin);

#ifdef __cplusplus
}
#endif
