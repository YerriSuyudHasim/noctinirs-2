#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v3_adc.h"
#include "nocti_v3_common.h"
#include "nocti_v3_health.h"
#include "nocti_v3_led.h"
#include "nocti_v3_timebase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOCTI_V3_SLOT_IDLE = 0,
    NOCTI_V3_SLOT_LED_ON_PENDING,
    NOCTI_V3_SLOT_LED_ON_COMMITTED,
    NOCTI_V3_SLOT_LED_ON_SETTLING,
    NOCTI_V3_SLOT_LED_ON_COLLECT,
    NOCTI_V3_SLOT_LED_OFF_PENDING,
    NOCTI_V3_SLOT_LED_OFF_COMMITTED,
    NOCTI_V3_SLOT_LED_OFF_SETTLING,
    NOCTI_V3_SLOT_LED_OFF_COLLECT,
    NOCTI_V3_SLOT_END_GUARD,
    NOCTI_V3_SLOT_FINALIZE,
    NOCTI_V3_SLOT_INVALID,
    NOCTI_V3_SLOT_LED_RECOVERY,
} nocti_v3_slot_state_t;

typedef enum {
    NOCTI_V3_SLOT_QUALITY_INVALID = 0,
    NOCTI_V3_SLOT_QUALITY_PARTIAL,
    NOCTI_V3_SLOT_QUALITY_VALID_MINIMUM,
    NOCTI_V3_SLOT_QUALITY_VALID_TARGET,
} nocti_v3_slot_quality_t;

typedef enum {
    NOCTI_V3_FRAME_QUALITY_INVALID = 0,
    NOCTI_V3_FRAME_QUALITY_PARTIAL,
    NOCTI_V3_FRAME_QUALITY_VALID,
} nocti_v3_frame_quality_t;

typedef enum {
    NOCTI_V3_REASON_NONE = 0,
    NOCTI_V3_REASON_UNDER_K,
    NOCTI_V3_REASON_TIMING_INVALID,
    NOCTI_V3_REASON_SAMPLE_INTEGRITY,
    NOCTI_V3_REASON_SAMPLE_BUFFER_OVERFLOW,
    NOCTI_V3_REASON_MISSING_SLOT,
    NOCTI_V3_REASON_TIMING_RING_OVERFLOW,
    NOCTI_V3_REASON_FRAME_RING_OVERFLOW,
    NOCTI_V3_REASON_STALE_EPOCH,
    NOCTI_V3_REASON_LED_NACK,
    NOCTI_V3_REASON_LED_TIMEOUT,
    NOCTI_V3_REASON_LED_RECOVERY_FAILED,
    NOCTI_V3_REASON_SCHEDULER_MISS,
    NOCTI_V3_REASON_ADC_FAULT,
    NOCTI_V3_REASON_STOP_REQUEST,
    NOCTI_V3_REASON_INTERNAL,
} nocti_v3_optical_reason_t;

typedef struct {
    uint8_t slot_index;
    uint8_t source_id;
    uint8_t geometry_id;
    uint16_t wavelength_nm;
    uint8_t pca_output;
    uint16_t current_ma;
    uint32_t nominal_start_offset_us;
    uint32_t nominal_off_offset_us;
    uint32_t nominal_end_offset_us;
    uint32_t settle_on_us;
    uint32_t settle_off_us;
    uint32_t end_guard_us;
    uint8_t k_target_on;
    uint8_t k_target_off;
    uint8_t k_min_on;
    uint8_t k_min_off;
} nocti_v3_slot_descriptor_t;

typedef struct {
    nocti_v3_slot_descriptor_t descriptor;
    uint32_t session_epoch;
    uint32_t config_generation;
    uint32_t frame_index;
    uint32_t slot_epoch;
    uint32_t on_command_id;
    uint32_t off_command_id;
    uint64_t nominal_slot_start_ts;
    uint64_t nominal_led_off_ts;
    uint64_t nominal_slot_end_ts;
    uint64_t led_request_ts;
    uint64_t i2c_submit_ts;
    uint64_t i2c_complete_ts;
    uint64_t led_commit_ts;
    uint64_t led_stable_start_ts;
    uint64_t led_off_request_ts;
    uint64_t led_off_submit_ts;
    uint64_t led_off_complete_ts;
    uint64_t led_off_commit_ts;
    uint64_t led_off_stable_start_ts;
    uint64_t slot_guard_start_ts;
    uint32_t on_stable_us;
    uint32_t off_stable_us;
    bool finalized;
    bool valid_timing;
    nocti_v3_slot_state_t final_state;
    nocti_v3_optical_reason_t reason;
    esp_err_t error;
} nocti_v3_slot_timing_t;

typedef struct {
    uint8_t slot_index;
    uint8_t source_id;
    uint8_t geometry_id;
    uint16_t wavelength_nm;
    uint8_t pca_output;
    uint8_t n_on_available;
    uint8_t n_i_led_off_available;
    uint8_t n_on_used;
    uint8_t n_i_led_off_used;
    int32_t i_led_on[4];
    int32_t i_led_off[4];
    int32_t i_corrected[4];
    bool corrected_valid;
    bool crc_bad_seen;
    bool spi_error_seen;
    bool status_error_seen;
    bool sequence_gap_seen;
    nocti_v3_slot_quality_t quality;
    nocti_v3_optical_reason_t reason;
} nocti_v3_slot_result_t;

typedef struct {
    uint8_t source_id;
    uint8_t geometry_id;
    uint8_t slot_735;
    uint8_t slot_850;
    bool present_735;
    bool present_850;
    bool valid_735;
    bool valid_850;
    bool pair_valid;
    nocti_v3_slot_quality_t quality_735;
    nocti_v3_slot_quality_t quality_850;
    int32_t channel_i735[4];
    int32_t channel_i850[4];
} nocti_v3_wavelength_pair_t;

typedef struct {
    uint32_t session_epoch;
    uint32_t config_generation;
    uint32_t frame_index;
    uint64_t frame_start_ts;
    uint64_t frame_end_ts;
    uint32_t present_mask;
    uint32_t target_mask;
    uint32_t minimum_mask;
    uint32_t partial_mask;
    uint32_t invalid_mask;
    uint32_t underk_on_mask;
    uint32_t underk_i_led_off_mask;
    uint32_t subtarget_on_mask;
    uint32_t subtarget_i_led_off_mask;
    uint32_t timing_invalid_mask;
    uint16_t pair_present_mask;
    uint16_t pair_valid_mask;
    uint16_t pair_invalid_mask;
    uint16_t reserved_pair_mask;
    uint32_t crc_mask;
    uint32_t spi_error_mask;
    uint32_t status_error_mask;
    nocti_v3_slot_result_t slot[NOCTI_V3_SLOT_COUNT];
    nocti_v3_wavelength_pair_t pair[NOCTI_V3_SLOT_COUNT / 2u];
    nocti_v3_frame_quality_t quality;
    nocti_v3_optical_reason_t reason;
} nocti_v3_completed_frame_t;

typedef struct {
    uint64_t led_errors;
    uint64_t stale_i2c_completion;
    uint64_t sched_missed;
    uint64_t sched_abort;
    uint64_t scheduler_event_overflow;
    uint64_t frames_total;
    uint64_t frames_valid;
    uint64_t partial_frames;
    uint64_t invalid_frames;
    uint64_t slots_total;
    uint64_t underk_on;
    uint64_t underk_off;
    uint64_t subtarget_on;
    uint64_t subtarget_off;
    uint64_t raw_stable_on;
    uint64_t raw_stable_i_led_off;
    uint64_t raw_sample_rejected_transition;
    uint64_t raw_sample_rejected_settle;
    uint64_t raw_sample_rejected_guard;
    uint64_t raw_sample_rejected_stale_epoch;
    uint64_t raw_sample_rejected_ambiguous;
    uint64_t slot_timing_ring_overflow;
    uint64_t sample_scratch_overflow;
    uint64_t completed_frame_ring_overflow;
    uint64_t completed_frames_consumed;
    uint64_t network_drop;
    uint64_t stream_errors;
    uint64_t network_frames_sent;
    uint64_t network_records_sent;
    uint32_t scheduler_lateness_max_us;
    uint32_t led_on_stable_min_us;
    uint32_t led_off_stable_min_us;
    uint32_t completed_frame_ring_depth;
    uint32_t completed_frame_ring_depth_max;
    uint32_t min_n_on;
    uint32_t min_n_off;
    uint32_t session_epoch;
    uint32_t config_generation;
    uint32_t frame_index;
    uint32_t slot_epoch;
    uint8_t slot_index;
    int scheduler_task_core;
    int processing_task_core;
    int frame_sink_task_core;
    int frame_consumer_task_core;
    uint32_t scheduler_stack_watermark_bytes;
    uint32_t processing_stack_watermark_bytes;
    uint32_t frame_sink_stack_watermark_bytes;
    bool scan_active;
    bool timing_history_valid;
    bool completed_frame_available;
    nocti_v3_slot_state_t slot_state;
    nocti_v3_lifecycle_state_t lifecycle;
    nocti_v3_optical_reason_t last_reason;
    esp_err_t last_error;
} nocti_v3_optical_health_snapshot_t;

typedef struct {
    nocti_v3_lifecycle_state_t state;
    nocti_v3_mode_t mode;
    uint32_t session_epoch;
    nocti_v3_adc_t *adc;
    nocti_v3_led_t *led;
    nocti_v3_timebase_t *timebase;
    nocti_v3_health_t *health;
    void *runtime;
} nocti_v3_optical_t;

esp_err_t nocti_v3_optical_init(nocti_v3_optical_t *optical,
                                nocti_v3_adc_t *adc,
                                nocti_v3_led_t *led,
                                nocti_v3_timebase_t *timebase,
                                nocti_v3_health_t *health);
esp_err_t nocti_v3_optical_request_start(nocti_v3_optical_t *optical,
                                         nocti_v3_mode_t mode);
esp_err_t nocti_v3_optical_stop(nocti_v3_optical_t *optical);
void nocti_v3_optical_health_snapshot(nocti_v3_optical_t *optical,
                                      nocti_v3_optical_health_snapshot_t *out);
esp_err_t nocti_v3_optical_reset_counters(nocti_v3_optical_t *optical);
esp_err_t nocti_v3_optical_format_slot_timing(nocti_v3_optical_t *optical,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *out_length);
bool nocti_v3_optical_latest_frame_snapshot(
    nocti_v3_optical_t *optical,
    nocti_v3_completed_frame_t *out_frame);
esp_err_t nocti_v3_optical_bind_frame_consumer(nocti_v3_optical_t *optical,
                                                TaskHandle_t task);
bool nocti_v3_optical_take_completed_frame(nocti_v3_optical_t *optical,
                                            nocti_v3_completed_frame_t *out_frame);
uint32_t nocti_v3_optical_flush_completed_frames(nocti_v3_optical_t *optical);
void nocti_v3_optical_record_network_result(nocti_v3_optical_t *optical,
                                            bool frame_sent,
                                            uint32_t records_sent,
                                            bool stream_error);
void nocti_v3_optical_record_stream_error(nocti_v3_optical_t *optical);
esp_err_t nocti_v3_optical_set_current_profile(nocti_v3_optical_t *optical,
                                                uint16_t current_735_ma,
                                                uint16_t current_850_ma);
void nocti_v3_optical_get_current_profile(nocti_v3_optical_t *optical,
                                           uint16_t *current_735_ma,
                                           uint16_t *current_850_ma);
const nocti_v3_slot_descriptor_t *nocti_v3_optical_slot_descriptors(void);
const char *nocti_v3_slot_state_name(nocti_v3_slot_state_t state);
const char *nocti_v3_slot_quality_name(nocti_v3_slot_quality_t quality);
const char *nocti_v3_frame_quality_name(nocti_v3_frame_quality_t quality);
const char *nocti_v3_optical_reason_name(nocti_v3_optical_reason_t reason);

#ifdef __cplusplus
}
#endif
