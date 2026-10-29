#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v4_ads.h"
#include "nocti_v4_async_led.h"
#include "nocti_v4_health.h"
#include "nocti_v4_scheduler_config.h"
#include "nocti_v4_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOCTI_V4_SCAN_STOPPED = 0,
    NOCTI_V4_SCAN_STARTING,
    NOCTI_V4_SCAN_RUNNING,
    NOCTI_V4_SCAN_STOPPING,
    NOCTI_V4_SCAN_RECOVERING,
    NOCTI_V4_SCAN_FAULTED,
} nocti_v4_scan_lifecycle_t;

typedef enum {
    NOCTI_V4_SLOT_IDLE = 0,
    NOCTI_V4_SLOT_LED_ON_PENDING,
    NOCTI_V4_SLOT_LED_ON_COMMITTED,
    NOCTI_V4_SLOT_LED_ON_SETTLING,
    NOCTI_V4_SLOT_LED_ON_STABLE,
    NOCTI_V4_SLOT_LED_OFF_PENDING,
    NOCTI_V4_SLOT_LED_OFF_COMMITTED,
    NOCTI_V4_SLOT_LED_OFF_SETTLING,
    NOCTI_V4_SLOT_LED_OFF_STABLE,
    NOCTI_V4_SLOT_FINALIZE,
    NOCTI_V4_SLOT_INVALID,
    NOCTI_V4_SLOT_LED_RECOVERY,
} nocti_v4_slot_state_t;

typedef struct {
    bool initialized;
    bool timer_armed;
    bool scan_active;
    nocti_v4_scan_lifecycle_t lifecycle;
    nocti_v4_slot_state_t active_slot_state;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint8_t active_slot_index;
    uint8_t active_source;
    uint64_t frame_epoch_us;
    uint64_t next_deadline_us;
    uint32_t configured_off_phase_us;
    uint32_t active_off_phase_us;
    uint32_t deadline_ring_depth;
    uint32_t deadline_ring_depth_max;
    int32_t scheduler_task_core;
    int32_t gptimer_callback_core;
    esp_err_t last_error;
    uint32_t sample_origin;
    uint8_t sample_origin_valid;
    uint32_t sample_slot_samples;
    uint32_t sample_off_phase_samples;
} nocti_v4_scheduler_status_t;

/* Immutable copy exported by the scheduler task only after it has finalized a
 * slot. It is consumed by the V4-6 optical task, never by an ISR. */
typedef struct {
    bool valid;
    bool finalized;
    bool runtime_invalid;
    bool aborted_by_stop;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint8_t slot_index;
    uint8_t logical_source;
    uint8_t pca_channel;
    nocti_v4_slot_state_t state;
    uint64_t nominal_slot_start_ts;
    uint64_t scheduler_on_deadline_ts;
    uint64_t scheduler_on_handling_ts;
    uint64_t led_on_request_ts;
    uint64_t led_on_queue_ts;
    uint64_t led_on_dequeue_ts;
    uint64_t led_on_submit_ts;
    uint64_t led_on_complete_ts;
    uint64_t led_on_commit_ts;
    uint64_t led_on_stable_start_ts;
    uint64_t nominal_led_off_ts;
    uint64_t scheduler_off_deadline_ts;
    uint64_t scheduler_off_handling_ts;
    uint64_t led_off_request_ts;
    uint64_t led_off_queue_ts;
    uint64_t led_off_dequeue_ts;
    uint64_t led_off_submit_ts;
    uint64_t led_off_complete_ts;
    uint64_t led_off_commit_ts;
    uint64_t led_off_stable_start_ts;
    uint64_t led_off_transition_boundary_ts;
    uint64_t slot_guard_start_ts;
    uint64_t nominal_slot_end_ts;
    uint64_t actual_finalize_ts;
    uint32_t led_on_command_id;
    uint32_t led_off_command_id;
    /* Retained V4-5 timing-only counters; V4-6 does not classify in the
     * scheduler and instead derives equivalent counts in optical processing. */
    uint32_t candidate_on;
    uint32_t candidate_off;
    uint32_t rejected_precommit;
    uint32_t rejected_on_settle;
    uint32_t rejected_transition;
    uint32_t rejected_off_settle;
    uint32_t rejected_guard;
    /* Sample-index plan. Classification uses these, not the microsecond fields. */
    uint8_t sample_plan_valid;
    uint8_t anchor_valid;
    uint8_t on_commit_known;
    uint8_t off_commit_known;
    uint8_t on_commit_late;
    uint8_t off_commit_late;
    uint32_t slot_start_sequence;
    uint32_t off_edge_sequence;
    uint32_t slot_end_sequence;
    uint32_t guard_sequence;
    uint32_t settle_samples;
    uint32_t on_first_affected;
    uint32_t off_first_affected;
    uint32_t on_commit_sequence;
    uint32_t off_commit_sequence;
    uint64_t anchor_timestamp_us;
} nocti_v4_slot_timing_t;

typedef struct {
    void *runtime;
    bool initialized;
} nocti_v4_scheduler_t;

esp_err_t nocti_v4_scheduler_init(nocti_v4_scheduler_t *scheduler,
                                   nocti_v4_time_t *timebase,
                                   nocti_v4_async_led_t *async_led,
                                   nocti_v4_health_t *health);
/* Requires ADS already running, PCA already in qdepth=4 async mode, and the
 * V4-6 optical raw consumer bound. It never starts an LED before the first epoch alarm. */
esp_err_t nocti_v4_scheduler_start(nocti_v4_scheduler_t *scheduler);
/* Disarms new deadlines and requests bounded async ALL_OFF. It is accepted
 * from RUNNING and FAULTED; app-level lifecycle then stops ADS and exits async. */
esp_err_t nocti_v4_scheduler_stop(nocti_v4_scheduler_t *scheduler);
/* Accepted only while STOPPED. SCAN_START snapshots the configured value into
 * immutable session state; mid-session changes are rejected. */
/* Local migration extension; stopped-only, accepted rates 2/4/5/8/10 Hz.
 * Caller also quiesces ADC/optical processing before changing geometry. */
esp_err_t nocti_v4_scheduler_set_fs(nocti_v4_scheduler_t *scheduler, uint32_t fs);
esp_err_t nocti_v4_scheduler_set_off_phase_us(nocti_v4_scheduler_t *scheduler,
                                               uint32_t off_phase_us);
bool nocti_v4_scheduler_is_active(const nocti_v4_scheduler_t *scheduler);
/* Bind V4-6 optical processing while stopped. It receives notification only
 * after a slot timing record is finalized; the record ring is static SPSC. */
esp_err_t nocti_v4_scheduler_bind_optical_task(nocti_v4_scheduler_t *scheduler,
                                               TaskHandle_t task);
bool nocti_v4_scheduler_pop_finalized_slot(nocti_v4_scheduler_t *scheduler,
                                           nocti_v4_slot_timing_t *out_timing);
void nocti_v4_scheduler_get_status(const nocti_v4_scheduler_t *scheduler,
                                   nocti_v4_scheduler_status_t *out_status);
/* Append one nn2ctl timing suffix. Values are sample indices and signed
 * edge deltas (actual first-affected minus planned edge). 'x' means the
 * commit for that slot has not been observed. Returns bytes written. */
size_t nocti_v4_scheduler_append_sync(const nocti_v4_scheduler_t *scheduler,
                                      char *out, size_t cap);
void nocti_v4_scheduler_print_status(const nocti_v4_scheduler_t *scheduler);
void nocti_v4_scheduler_print_timing(const nocti_v4_scheduler_t *scheduler);
void nocti_v4_scheduler_print_health(const nocti_v4_scheduler_t *scheduler);
void nocti_v4_scheduler_dump_slot_timing(const nocti_v4_scheduler_t *scheduler);
void nocti_v4_scheduler_sample_stack_watermark(nocti_v4_scheduler_t *scheduler);
const char *nocti_v4_scan_lifecycle_name(nocti_v4_scan_lifecycle_t state);
const char *nocti_v4_slot_state_name(nocti_v4_slot_state_t state);

#ifdef __cplusplus
}
#endif
