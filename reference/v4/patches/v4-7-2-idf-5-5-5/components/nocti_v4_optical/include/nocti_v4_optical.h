#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v4_ads.h"
#include "nocti_v4_health.h"
#include "nocti_v4_scheduler.h"
#include "nocti_v4_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The priority-5 frame consumer has had zero overflow at 10 Hz. Two complete
 * records retain 200 ms of buffering and make Wi-Fi fan-out memory-bounded. */
#define NOCTI_V4_OPTICAL_FRAME_RING_CAPACITY 2u
#define NOCTI_V4_K_ANOMALY_RING_CAPACITY 32u
#define NOCTI_V4_CLASSIFY_MISMATCH_RING_CAPACITY 16u
#define NOCTI_V4_SCAN_WARMUP_FRAMES 1u

typedef enum {
    NOCTI_V4_SLOT_QUALITY_INVALID_UNDER_K = 0,
    NOCTI_V4_SLOT_QUALITY_MINIMUM_VALID,
    NOCTI_V4_SLOT_QUALITY_TARGET_VALID,
    NOCTI_V4_SLOT_QUALITY_ABORTED_BY_STOP,
    NOCTI_V4_SLOT_QUALITY_STARTUP_WARMUP,
} nocti_v4_slot_quality_t;

typedef enum {
    NOCTI_V4_FRAME_QUALITY_INVALID = 0,
    NOCTI_V4_FRAME_QUALITY_PARTIAL,
    NOCTI_V4_FRAME_QUALITY_TARGET_VALID,
    NOCTI_V4_FRAME_QUALITY_ABORTED_BY_STOP,
    NOCTI_V4_FRAME_QUALITY_STARTUP_WARMUP,
} nocti_v4_frame_quality_t;

enum {
    NOCTI_V4_SLOT_REASON_NONE              = 0u,
    NOCTI_V4_SLOT_REASON_UNDER_K_ON        = 1u << 0,
    NOCTI_V4_SLOT_REASON_UNDER_K_OFF       = 1u << 1,
    NOCTI_V4_SLOT_REASON_LED_ON_MISSING    = 1u << 2,
    NOCTI_V4_SLOT_REASON_LED_OFF_MISSING   = 1u << 3,
    NOCTI_V4_SLOT_REASON_RUNTIME_TIMING    = 1u << 4,
    NOCTI_V4_SLOT_REASON_ADS_INTEGRITY     = 1u << 5,
    NOCTI_V4_SLOT_REASON_STOP_ABORT        = 1u << 6,
    NOCTI_V4_SLOT_REASON_STALE_EPOCH       = 1u << 7,
    NOCTI_V4_SLOT_REASON_INTERNAL          = 1u << 8,
    NOCTI_V4_SLOT_REASON_STARTUP_WARMUP    = 1u << 9,
};

typedef struct {
    uint8_t slot_index;
    uint8_t logical_source;
    uint8_t geometry_id;
    uint8_t pca_channel;
    uint16_t wavelength_nm;
    uint8_t detector_count;
    uint8_t k_target_on;
    uint8_t k_target_off;
    uint8_t k_min_on;
    uint8_t k_min_off;
} nocti_v4_optical_slot_descriptor_t;

typedef struct {
    uint8_t slot_index;
    uint8_t logical_source;
    uint8_t geometry_id;
    uint8_t pca_channel;
    uint16_t wavelength_nm;
    uint8_t n_on_available;
    uint8_t n_off_available;
    uint8_t n_on_used;
    uint8_t n_off_used;
    nocti_v4_slot_quality_t quality;
    uint32_t reason;
    int32_t i_on[NOCTI_V4_ADS_CHANNEL_COUNT];
    int32_t i_led_off[NOCTI_V4_ADS_CHANNEL_COUNT];
    int64_t i_corrected[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint32_t scheduler_lateness_us;
    uint32_t led_on_latency_us;
    uint32_t led_off_latency_us;
} nocti_v4_optical_slot_value_t;

typedef struct {
    uint8_t geometry_id;
    uint8_t slot_735;
    uint8_t slot_850;
    bool present_735;
    bool present_850;
    bool valid_735;
    bool valid_850;
    int64_t i735[NOCTI_V4_ADS_CHANNEL_COUNT];
    int64_t i850[NOCTI_V4_ADS_CHANNEL_COUNT];
} nocti_v4_wavelength_pair_t;

typedef struct {
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint64_t nominal_frame_timestamp_us;
    uint64_t completed_timestamp_us;
    nocti_v4_frame_quality_t quality;
    uint8_t target_valid_slots;
    uint8_t minimum_valid_slots;
    uint8_t invalid_slots;
    uint32_t reason;
    nocti_v4_optical_slot_value_t slot[NOCTI_V4_SLOT_COUNT];
    nocti_v4_wavelength_pair_t pair[NOCTI_V4_SLOT_COUNT / 2u];
} nocti_v4_completed_optical_frame_t;

/* Called only by the priority-5 completed-frame consumer after it has taken a
 * private immutable frame copy. The sink must be nonblocking and bounded. */
typedef bool (*nocti_v4_completed_frame_sink_t)(
    void *context, const nocti_v4_completed_optical_frame_t *frame);

typedef struct {
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint8_t slot_index;
    uint8_t n_on;
    uint8_t n_off;
    uint32_t scheduler_lateness_us;
    uint32_t led_request_to_submit_us;
    uint32_t led_on_latency_us;
    uint32_t led_off_latency_us;
} nocti_v4_k_anomaly_t;

/* Retained only when the V4-5-equivalent candidate counters and final V4-6
 * accumulator counts disagree. With the single canonical classifier this
 * ring is expected to remain empty. */
typedef struct {
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint8_t slot_index;
    uint8_t v5_candidate_on_equivalent;
    uint8_t v5_candidate_off_equivalent;
    uint8_t v6_n_on_available;
    uint8_t v6_n_off_available;
    int8_t difference_on;
    int8_t difference_off;
} nocti_v4_classify_mismatch_t;

typedef struct {
    bool initialized;
    bool session_prepared;
    bool running;
    bool stopping;
    uint32_t session_epoch;
    uint32_t raw_processed;
    uint32_t frame_ring_depth;
    bool latest_frame_valid;
    int32_t optical_task_core;
    int32_t frame_consumer_task_core;
} nocti_v4_optical_status_t;

typedef struct {
    void *runtime;
    bool initialized;
} nocti_v4_optical_t;

esp_err_t nocti_v4_optical_init(nocti_v4_optical_t *optical,
                                 nocti_v4_ads_t *ads,
                                 nocti_v4_scheduler_t *scheduler,
                                 nocti_v4_time_t *timebase,
                                 nocti_v4_health_t *health);
/* Called while ADS is stopped. It clears ownership/state, binds the optical
 * task as the sole ADS raw consumer, and rejects mid-session reconfiguration. */
esp_err_t nocti_v4_optical_prepare_scan(nocti_v4_optical_t *optical);
/* Called after scheduler_start has fixed the scan epoch. Samples before this
 * point are explicitly counted as STARTUP_DISCARD. */
esp_err_t nocti_v4_optical_begin_scan(nocti_v4_optical_t *optical,
                                      uint32_t scheduler_session_epoch);
/* Mark normal user stop before ADS drains; remaining active work becomes
 * STOP_ABORT, never runtime-invalid. */
void nocti_v4_optical_begin_stop(nocti_v4_optical_t *optical);
/* Called after ADS is stopped. It finishes/invalidate-drains, preserves
 * completed frames, and restores default DARK raw ownership. */
esp_err_t nocti_v4_optical_finish_stop(nocti_v4_optical_t *optical);
/* Stopped-only fan-out binding. This keeps networking out of the optical task
 * and preserves the diagnostic latest-frame consumer. */
esp_err_t nocti_v4_optical_bind_completed_frame_sink(
    nocti_v4_optical_t *optical,
    nocti_v4_completed_frame_sink_t sink,
    void *sink_context);
/* Post-acquisition ownership barrier. No delay is used as a timing surrogate:
 * it waits only until the bounded completed-frame consumer is observably idle. */
esp_err_t nocti_v4_optical_wait_frame_consumer_idle(
    nocti_v4_optical_t *optical, uint32_t timeout_ms);
void nocti_v4_optical_get_status(const nocti_v4_optical_t *optical,
                                 nocti_v4_optical_status_t *out_status);
void nocti_v4_optical_print_status(const nocti_v4_optical_t *optical);
void nocti_v4_optical_print_health(const nocti_v4_optical_t *optical);
void nocti_v4_optical_dump_latest_frame(const nocti_v4_optical_t *optical);
void nocti_v4_optical_dump_k_anomalies(const nocti_v4_optical_t *optical);
void nocti_v4_optical_dump_classify_mismatches(const nocti_v4_optical_t *optical);
/* Replays actual per-slot DRDY timestamps against counterfactual OFF phases.
 * This is model output only; it never rearms or changes the live scheduler. */
void nocti_v4_optical_print_phase_sweep(const nocti_v4_optical_t *optical);
void nocti_v4_optical_dump_map(void);
void nocti_v4_optical_sample_stack_watermarks(nocti_v4_optical_t *optical);

#ifdef __cplusplus
}
#endif
