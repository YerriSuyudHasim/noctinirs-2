#include "nocti_v3_optical.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nocti_v3_config.h"


#define NOTIFY_LED_COMPLETION (1u << 1)
#define NOTIFY_LED_RECOVERY   (1u << 2)
#define NOTIFY_CONTROL        (1u << 3)
#define TIMING_RING_MASK      (NOCTI_V3_SLOT_TIMING_RING_CAPACITY - 1u)
#define PENDING_RAW_MASK      (NOCTI_V3_PENDING_RAW_CAPACITY - 1u)
#define FRAME_RING_MASK       (NOCTI_V3_COMPLETED_FRAME_RING_CAPACITY - 1u)

typedef enum {
    SCHED_EVENT_SLOT_START = 1,
    SCHED_EVENT_ON_STABLE,
    SCHED_EVENT_LED_OFF,
    SCHED_EVENT_OFF_STABLE,
    SCHED_EVENT_GUARD_START,
    SCHED_EVENT_SLOT_FINALIZE,
    SCHED_EVENT_COMMAND_TIMEOUT,
} scheduler_event_id_t;

typedef enum {
    CONTROL_START = 1,
    CONTROL_STOP,
} control_type_t;

typedef struct {
    control_type_t type;
    nocti_v3_mode_t mode;
} control_command_t;

typedef struct {
    bool active;
    nocti_v3_completed_frame_t frame;
} frame_builder_t;

typedef struct {
    nocti_v3_optical_t *public_optical;
    nocti_v3_adc_t *adc;
    nocti_v3_led_t *led;
    nocti_v3_timebase_t *timebase;
    nocti_v3_health_t *health;

    TaskHandle_t scheduler_task;
    TaskHandle_t processing_task;
    TaskHandle_t frame_sink_task;
    TaskHandle_t external_frame_consumer;
    volatile bool external_frame_consumer_bound;
    volatile int external_frame_consumer_core;
    QueueHandle_t control_queue;
    StaticQueue_t control_queue_storage;
    uint8_t control_queue_buffer[4u * sizeof(control_command_t)];
    SemaphoreHandle_t control_lock;
    SemaphoreHandle_t control_done;
    SemaphoreHandle_t processing_reset_done;
    SemaphoreHandle_t frame_sink_reset_done;
    StaticSemaphore_t control_lock_storage;
    StaticSemaphore_t control_done_storage;
    StaticSemaphore_t processing_reset_done_storage;
    StaticSemaphore_t frame_sink_reset_done_storage;
    volatile esp_err_t control_result;

    volatile bool scan_active;
    volatile bool stop_pending;
    volatile bool processing_reset_requested;
    volatile bool frame_sink_reset_requested;
    volatile bool recovery_fatal;
    volatile nocti_v3_lifecycle_state_t lifecycle;
    volatile nocti_v3_mode_t mode;
    volatile nocti_v3_slot_state_t slot_state;
    volatile nocti_v3_optical_reason_t last_reason;
    volatile esp_err_t last_error;
    volatile uint32_t generation;
    volatile uint32_t config_generation;
    uint32_t session_epoch;
    uint32_t frame_index;
    uint32_t slot_epoch;
    uint32_t next_command_id;
    uint8_t slot_index;
    uint64_t frame_start_ts;
    nocti_v3_slot_timing_t current;
    nocti_v3_slot_timing_t history[NOCTI_V3_TIMING_HISTORY_SLOTS];
    volatile uint32_t history_generation;
    volatile bool history_valid;

    /* Scheduler producer -> processing consumer. */
    volatile uint32_t timing_head;
    volatile uint32_t timing_tail;
    nocti_v3_slot_timing_t timing_ring[NOCTI_V3_SLOT_TIMING_RING_CAPACITY];

    /* Processing-task-owned raw staging. */
    uint32_t pending_raw_head;
    uint32_t pending_raw_tail;
    nocti_v3_raw_sample_t pending_raw[NOCTI_V3_PENDING_RAW_CAPACITY];
    nocti_v3_raw_sample_t slot_scratch[NOCTI_V3_MAX_RAW_SAMPLES_PER_SLOT];
    uint64_t raw_timestamp_watermark;
    bool pending_overflow_for_frame;
    frame_builder_t builder;

    /* Processing producer -> dummy frame sink consumer. */
    volatile uint32_t frame_head;
    volatile uint32_t frame_tail;
    nocti_v3_completed_frame_t frame_ring[NOCTI_V3_COMPLETED_FRAME_RING_CAPACITY];
    volatile uint32_t latest_frame_generation;
    volatile bool latest_frame_valid;
    nocti_v3_completed_frame_t latest_frame;
    nocti_v3_completed_frame_t frame_sink_scratch;

    volatile uint32_t led_errors;
    volatile uint32_t stale_i2c_completion;
    volatile uint32_t consecutive_stale_completion;
    volatile uint32_t consecutive_scheduler_faults;
    volatile uint32_t sched_missed;
    volatile uint32_t sched_abort;
    volatile uint32_t scheduler_event_overflow;
    volatile uint32_t scheduler_lateness_max_us;
    volatile uint32_t led_on_stable_min_us;
    volatile uint32_t led_off_stable_min_us;

    volatile uint32_t frames_total;
    volatile uint32_t frames_valid;
    volatile uint32_t partial_frames;
    volatile uint32_t invalid_frames;
    volatile uint32_t slots_total;
    volatile uint32_t underk_on;
    volatile uint32_t underk_off;
    volatile uint32_t subtarget_on;
    volatile uint32_t subtarget_off;
    volatile uint32_t min_n_on;
    volatile uint32_t min_n_off;

    volatile uint32_t raw_stable_on;
    volatile uint32_t raw_stable_off;
    volatile uint32_t raw_transition;
    volatile uint32_t raw_settle;
    volatile uint32_t raw_guard;
    volatile uint32_t raw_stale_epoch;
    volatile uint32_t raw_ambiguous;
    volatile uint32_t timing_ring_overflow;
    volatile uint32_t sample_scratch_overflow;
    volatile uint32_t frame_ring_overflow;
    volatile uint32_t frame_ring_depth_max;
    volatile uint32_t completed_frames_consumed;
    volatile uint32_t network_drop;
    volatile uint32_t stream_errors;
    volatile uint32_t network_frames_sent;
    volatile uint32_t network_records_sent;

    volatile uint32_t scheduler_stack_watermark;
    volatile uint32_t processing_stack_watermark;
    volatile uint32_t frame_sink_stack_watermark;
    int scheduler_core;
    int processing_core;
    int frame_sink_core;
} optical_runtime_t;

static optical_runtime_t s_runtime;

_Static_assert((NOCTI_V3_SLOT_COUNT % 2u) == 0u,
               "wavelength pairing requires an even slot count");
_Static_assert((NOCTI_V3_SLOT_COUNT / 2u) <= 16u,
               "wavelength pair masks are 16-bit");
_Static_assert(sizeof(nocti_v3_completed_frame_t) <= 3072u,
               "completed frame grew beyond the reviewed static/stack budget");
_Static_assert(sizeof(nocti_v3_raw_sample_t) <= 64u,
               "raw sample grew beyond the reviewed hot-ring budget");

#define SLOT_DESCRIPTOR(index_, source_, wavelength_, pca_, current_) \
    { .slot_index = (index_), .source_id = (source_), \
      .geometry_id = (source_), .wavelength_nm = (wavelength_), \
      .pca_output = (pca_), .current_ma = (current_), \
      .nominal_start_offset_us = (index_) * NOCTI_V3_SLOT_PERIOD_US, \
      .nominal_off_offset_us = (index_) * NOCTI_V3_SLOT_PERIOD_US + \
                               NOCTI_V3_LED_OFF_OFFSET_US, \
      .nominal_end_offset_us = ((index_) + 1u) * NOCTI_V3_SLOT_PERIOD_US, \
      .settle_on_us = NOCTI_V3_SETTLE_ON_US, \
      .settle_off_us = NOCTI_V3_SETTLE_OFF_US, \
      .end_guard_us = NOCTI_V3_END_GUARD_US, \
      .k_target_on = NOCTI_V3_K_TARGET_ON, \
      .k_target_off = NOCTI_V3_K_TARGET_OFF, \
      .k_min_on = NOCTI_V3_K_MIN_ON, .k_min_off = NOCTI_V3_K_MIN_OFF }

static nocti_v3_slot_descriptor_t s_descriptors[NOCTI_V3_SLOT_COUNT] = {
    SLOT_DESCRIPTOR(0u, 0u, 735u, 0u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(1u, 0u, 850u, 1u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(2u, 1u, 735u, 2u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(3u, 1u, 850u, 3u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(4u, 2u, 735u, 4u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(5u, 2u, 850u, 5u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(6u, 3u, 735u, 6u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(7u, 3u, 850u, 7u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(8u, 4u, 735u, 8u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(9u, 4u, 850u, 9u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(10u, 5u, 735u, 14u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(11u, 5u, 850u, 15u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(12u, 6u, 735u, 16u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(13u, 6u, 850u, 17u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(14u, 7u, 735u, 18u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(15u, 7u, 850u, 19u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(16u, 8u, 735u, 20u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(17u, 8u, 850u, 21u, NOCTI_V3_LED_CURRENT_850_MA),
    SLOT_DESCRIPTOR(18u, 9u, 735u, 22u, NOCTI_V3_LED_CURRENT_735_MA),
    SLOT_DESCRIPTOR(19u, 9u, 850u, 23u, NOCTI_V3_LED_CURRENT_850_MA),
};
#undef SLOT_DESCRIPTOR

static void update_max_u32(volatile uint32_t *value, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void update_min_zero_sentinel(volatile uint32_t *value,
                                     uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while ((current == 0u || candidate < current) &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void update_min_max_sentinel(volatile uint32_t *value,
                                    uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate < current &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static uint32_t frame_ring_depth(optical_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->frame_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&runtime->frame_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

static void set_slot_state(optical_runtime_t *runtime,
                           nocti_v3_slot_state_t state)
{
    __atomic_store_n(&runtime->slot_state, state, __ATOMIC_RELEASE);
    nocti_v3_adc_set_optical_snapshot(runtime->adc, runtime->slot_epoch,
                                      (uint16_t)state);
}

static esp_err_t arm_event(optical_runtime_t *runtime,
                           uint64_t timestamp_us,
                           scheduler_event_id_t event)
{
    const esp_err_t err = nocti_v3_timebase_arm_absolute(
        runtime->timebase, timestamp_us, (uint32_t)event,
        runtime->generation);
    if (err != ESP_OK) {
        (void)__atomic_fetch_add(&runtime->sched_missed, 1u, __ATOMIC_RELAXED);
        (void)__atomic_fetch_add(&runtime->consecutive_scheduler_faults, 1u,
                                 __ATOMIC_RELAXED);
        runtime->last_error = err;
        runtime->last_reason = NOCTI_V3_REASON_SCHEDULER_MISS;
    } else {
        __atomic_store_n(&runtime->consecutive_scheduler_faults, 0u,
                         __ATOMIC_RELEASE);
    }
    return err;
}

static bool timing_publish(optical_runtime_t *runtime,
                           const nocti_v3_slot_timing_t *timing)
{
    const uint32_t head = __atomic_load_n(&runtime->timing_head,
                                          __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->timing_tail,
                                          __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V3_SLOT_TIMING_RING_CAPACITY) {
        (void)__atomic_fetch_add(&runtime->timing_ring_overflow, 1u,
                                 __ATOMIC_RELAXED);
        runtime->last_reason = NOCTI_V3_REASON_TIMING_RING_OVERFLOW;
        runtime->last_error = ESP_ERR_NO_MEM;
        return false;
    }
    runtime->timing_ring[head & TIMING_RING_MASK] = *timing;
    __atomic_store_n(&runtime->timing_head, head + 1u, __ATOMIC_RELEASE);
    if (runtime->processing_task != NULL) xTaskNotifyGive(runtime->processing_task);
    return true;
}

static bool timing_peek(optical_runtime_t *runtime,
                        nocti_v3_slot_timing_t *out)
{
    const uint32_t tail = __atomic_load_n(&runtime->timing_tail,
                                          __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->timing_head,
                                          __ATOMIC_ACQUIRE);
    if (tail == head) return false;
    *out = runtime->timing_ring[tail & TIMING_RING_MASK];
    return true;
}

static void timing_consume(optical_runtime_t *runtime)
{
    const uint32_t tail = __atomic_load_n(&runtime->timing_tail,
                                          __ATOMIC_RELAXED);
    __atomic_store_n(&runtime->timing_tail, tail + 1u, __ATOMIC_RELEASE);
}

static bool frame_push(optical_runtime_t *runtime,
                       const nocti_v3_completed_frame_t *frame)
{
    const uint32_t head = __atomic_load_n(&runtime->frame_head,
                                          __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->frame_tail,
                                          __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V3_COMPLETED_FRAME_RING_CAPACITY) {
        (void)__atomic_fetch_add(&runtime->frame_ring_overflow, 1u,
                                 __ATOMIC_RELAXED);
        runtime->last_reason = NOCTI_V3_REASON_FRAME_RING_OVERFLOW;
        runtime->last_error = ESP_ERR_NO_MEM;
        return false;
    }
    runtime->frame_ring[head & FRAME_RING_MASK] = *frame;
    __atomic_store_n(&runtime->frame_head, head + 1u, __ATOMIC_RELEASE);
    update_max_u32(&runtime->frame_ring_depth_max, head + 1u - tail);
    TaskHandle_t consumer = __atomic_load_n(&runtime->external_frame_consumer,
                                             __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&runtime->external_frame_consumer_bound, __ATOMIC_ACQUIRE) &&
        consumer != NULL) {
        xTaskNotifyGive(consumer);
    } else if (runtime->frame_sink_task != NULL) {
        xTaskNotifyGive(runtime->frame_sink_task);
    }
    return true;
}

static bool frame_pop(optical_runtime_t *runtime,
                      nocti_v3_completed_frame_t *out)
{
    const uint32_t tail = __atomic_load_n(&runtime->frame_tail,
                                          __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->frame_head,
                                          __ATOMIC_ACQUIRE);
    if (tail == head) return false;
    *out = runtime->frame_ring[tail & FRAME_RING_MASK];
    __atomic_store_n(&runtime->frame_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static esp_err_t submit_led(optical_runtime_t *runtime,
                            nocti_v3_led_command_type_t type)
{
    uint64_t now = 0u;
    (void)nocti_v3_timebase_now_us(runtime->timebase, &now);
    nocti_v3_led_request_t request = {
        .type = type,
        .slot_index = runtime->slot_index,
        .command_id = ++runtime->next_command_id,
        .session_epoch = runtime->session_epoch,
        .frame_index = runtime->frame_index,
        .slot_epoch = runtime->slot_epoch,
        .request_timestamp_us = now,
    };
    if (type == NOCTI_V3_LED_COMMAND_SLOT_ON) {
        runtime->current.on_command_id = request.command_id;
        runtime->current.led_request_ts = now;
        set_slot_state(runtime, NOCTI_V3_SLOT_LED_ON_PENDING);
    } else {
        runtime->current.off_command_id = request.command_id;
        runtime->current.led_off_request_ts = now;
        set_slot_state(runtime, NOCTI_V3_SLOT_LED_OFF_PENDING);
    }
    const esp_err_t err = nocti_v3_led_submit(runtime->led, &request);
    if (err != ESP_OK) return err;
    return arm_event(runtime, now + NOCTI_V3_LED_COMMAND_TIMEOUT_US,
                     SCHED_EVENT_COMMAND_TIMEOUT);
}

static bool finalize_current_slot(optical_runtime_t *runtime,
                                  nocti_v3_optical_reason_t reason,
                                  esp_err_t error)
{
    if (runtime->current.finalized) return true;
    if (error != ESP_OK) runtime->current.error = error;
    if (reason != NOCTI_V3_REASON_NONE) runtime->current.reason = reason;
    runtime->current.finalized = true;
    runtime->current.final_state = runtime->slot_state;
    if (runtime->current.led_stable_start_ts != 0u &&
        runtime->current.led_stable_start_ts <
            runtime->current.led_off_request_ts) {
        runtime->current.on_stable_us = (uint32_t)(
            runtime->current.led_off_request_ts -
            runtime->current.led_stable_start_ts);
    }
    if (runtime->current.led_off_stable_start_ts != 0u &&
        runtime->current.led_off_stable_start_ts <
            runtime->current.slot_guard_start_ts) {
        runtime->current.off_stable_us = (uint32_t)(
            runtime->current.slot_guard_start_ts -
            runtime->current.led_off_stable_start_ts);
    }
    runtime->current.valid_timing = runtime->current.error == ESP_OK &&
        runtime->current.reason == NOCTI_V3_REASON_NONE &&
        runtime->current.led_commit_ts != 0u &&
        runtime->current.led_off_commit_ts != 0u &&
        runtime->current.led_stable_start_ts <
            runtime->current.led_off_request_ts &&
        runtime->current.led_off_stable_start_ts <
            runtime->current.slot_guard_start_ts &&
        runtime->current.on_stable_us >= NOCTI_V3_MIN_WINDOW_ON_US &&
        runtime->current.off_stable_us >= NOCTI_V3_MIN_WINDOW_OFF_US;
    update_min_zero_sentinel(&runtime->led_on_stable_min_us,
                             runtime->current.on_stable_us);
    update_min_zero_sentinel(&runtime->led_off_stable_min_us,
                             runtime->current.off_stable_us);
    __atomic_add_fetch(&runtime->history_generation, 1u, __ATOMIC_ACQ_REL);
    runtime->history[runtime->slot_index] = runtime->current;
    __atomic_add_fetch(&runtime->history_generation, 1u, __ATOMIC_RELEASE);
    if (runtime->slot_index == NOCTI_V3_SLOT_COUNT - 1u) {
        __atomic_store_n(&runtime->history_valid, true, __ATOMIC_RELEASE);
    }
    return timing_publish(runtime, &runtime->current);
}

static esp_err_t begin_slot(optical_runtime_t *runtime,
                            uint8_t slot_index,
                            uint64_t nominal_start)
{
    runtime->slot_index = slot_index;
    runtime->slot_epoch++;
    memset(&runtime->current, 0, sizeof(runtime->current));
    runtime->current.descriptor = s_descriptors[slot_index];
    runtime->current.session_epoch = runtime->session_epoch;
    runtime->current.config_generation = runtime->config_generation;
    runtime->current.frame_index = runtime->frame_index;
    runtime->current.slot_epoch = runtime->slot_epoch;
    runtime->current.nominal_slot_start_ts = nominal_start;
    runtime->current.nominal_led_off_ts = nominal_start +
                                          NOCTI_V3_LED_OFF_OFFSET_US;
    runtime->current.nominal_slot_end_ts = nominal_start +
                                           NOCTI_V3_SLOT_PERIOD_US;
    runtime->current.slot_guard_start_ts =
        runtime->current.nominal_slot_end_ts - NOCTI_V3_END_GUARD_US;
    runtime->current.error = ESP_OK;
    runtime->current.reason = NOCTI_V3_REASON_NONE;
    return submit_led(runtime, NOCTI_V3_LED_COMMAND_SLOT_ON);
}

static esp_err_t request_processing_reset(optical_runtime_t *runtime);
static esp_err_t request_frame_sink_reset(optical_runtime_t *runtime);

static void update_public_lifecycle(optical_runtime_t *runtime)
{
    runtime->public_optical->mode = runtime->mode;
    runtime->public_optical->state = runtime->lifecycle;
    runtime->public_optical->session_epoch = runtime->session_epoch;
    nocti_v3_health_set_lifecycle(runtime->health, runtime->lifecycle,
                                  runtime->mode);
}

static void complete_stop(optical_runtime_t *runtime, esp_err_t led_result)
{
    esp_err_t adc_result = ESP_OK;
    if (nocti_v3_adc_running(runtime->adc)) {
        adc_result = nocti_v3_adc_stop(runtime->adc);
    }
    esp_err_t processing_result = ESP_OK;
    esp_err_t frame_sink_result = ESP_OK;
    if (adc_result == ESP_OK) {
        processing_result = request_processing_reset(runtime);
    }
    if (adc_result == ESP_OK && processing_result == ESP_OK) {
        frame_sink_result = request_frame_sink_reset(runtime);
    }
    runtime->control_result = led_result != ESP_OK ? led_result :
        adc_result != ESP_OK ? adc_result :
        processing_result != ESP_OK ? processing_result : frame_sink_result;
    runtime->scan_active = false;
    runtime->stop_pending = false;
    runtime->mode = NOCTI_V3_MODE_STOPPED;
    runtime->lifecycle = runtime->control_result == ESP_OK ?
        NOCTI_V3_STATE_STOPPED : NOCTI_V3_STATE_FAULTED;
    set_slot_state(runtime, NOCTI_V3_SLOT_IDLE);
    update_public_lifecycle(runtime);
    (void)xSemaphoreGive(runtime->control_done);
}

static void begin_recovery(optical_runtime_t *runtime,
                           nocti_v3_fault_code_t fault,
                           nocti_v3_optical_reason_t reason,
                           esp_err_t error,
                           bool fatal)
{
    if (runtime->lifecycle == NOCTI_V3_STATE_RECOVERING ||
        runtime->lifecycle == NOCTI_V3_STATE_FAULTED) return;
    runtime->last_error = error;
    runtime->last_reason = reason;
    runtime->scan_active = false;
    runtime->generation++;
    (void)nocti_v3_timebase_disarm_deadline(runtime->timebase);
    set_slot_state(runtime, NOCTI_V3_SLOT_INVALID);
    if (!runtime->current.finalized) {
        (void)finalize_current_slot(runtime, reason, error);
    }
    runtime->lifecycle = NOCTI_V3_STATE_RECOVERING;
    runtime->recovery_fatal = fatal;
    update_public_lifecycle(runtime);
    (void)__atomic_fetch_add(&runtime->sched_abort, 1u, __ATOMIC_RELAXED);
    (void)__atomic_fetch_add(&runtime->led_errors, 1u, __ATOMIC_RELAXED);
    const esp_err_t recovery_result =
        nocti_v3_led_request_recovery(runtime->led, error);
    if (recovery_result != ESP_OK) runtime->recovery_fatal = true;
    if (nocti_v3_adc_running(runtime->adc)) {
        const esp_err_t adc_stop = nocti_v3_adc_stop(runtime->adc);
        if (adc_stop != ESP_OK) runtime->recovery_fatal = true;
    }
    nocti_v3_health_record_fault(runtime->health, fault, error);
    update_public_lifecycle(runtime);
}

static void process_recovery_state(optical_runtime_t *runtime)
{
    if (runtime->lifecycle != NOCTI_V3_STATE_RECOVERING) return;
    nocti_v3_led_health_snapshot_t led_health;
    nocti_v3_led_health_snapshot(runtime->led, &led_health);
    if (led_health.recovery_pending) return;
    if (!runtime->recovery_fatal) {
        const esp_err_t processing_result = request_processing_reset(runtime);
        const esp_err_t frame_sink_result = processing_result == ESP_OK ?
            request_frame_sink_reset(runtime) : processing_result;
        if (processing_result != ESP_OK || frame_sink_result != ESP_OK) {
            runtime->recovery_fatal = true;
            runtime->last_error = processing_result != ESP_OK ?
                processing_result : frame_sink_result;
            runtime->last_reason = NOCTI_V3_REASON_INTERNAL;
        }
    }
    if (led_health.runtime_ready && !runtime->recovery_fatal) {
        runtime->lifecycle = NOCTI_V3_STATE_STOPPED;
        runtime->mode = NOCTI_V3_MODE_STOPPED;
        runtime->last_error = ESP_OK;
        set_slot_state(runtime, NOCTI_V3_SLOT_IDLE);
    } else {
        runtime->lifecycle = NOCTI_V3_STATE_FAULTED;
        runtime->mode = NOCTI_V3_MODE_STOPPED;
        runtime->last_reason = NOCTI_V3_REASON_LED_RECOVERY_FAILED;
    }
    update_public_lifecycle(runtime);
    if (runtime->stop_pending) {
        runtime->control_result = runtime->last_error;
        runtime->stop_pending = false;
        (void)xSemaphoreGive(runtime->control_done);
    }
}

static void handle_led_error(optical_runtime_t *runtime,
                             const nocti_v3_led_completion_t *completion)
{
    const bool timeout = completion->result == NOCTI_V3_LED_RESULT_TIMEOUT;
    const bool nack = completion->result == NOCTI_V3_LED_RESULT_NACK;
    const nocti_v3_optical_reason_t reason = timeout ?
        NOCTI_V3_REASON_LED_TIMEOUT : nack ? NOCTI_V3_REASON_LED_NACK :
        NOCTI_V3_REASON_INTERNAL;
    const nocti_v3_fault_code_t fault = timeout ?
        NOCTI_V3_FAULT_LED_TIMEOUT : NOCTI_V3_FAULT_LED_I2C;
    const esp_err_t error = completion->submit_result != ESP_OK ?
        completion->submit_result : timeout ? ESP_ERR_TIMEOUT : ESP_FAIL;
    begin_recovery(runtime, fault, reason, error, false);
}

static void process_led_completions(optical_runtime_t *runtime)
{
    nocti_v3_led_completion_t completion;
    while (nocti_v3_led_take_completion(runtime->led, &completion)) {
        if (completion.request.session_epoch != runtime->session_epoch ||
            completion.request.slot_epoch != runtime->slot_epoch ||
            (!runtime->scan_active && !runtime->stop_pending)) {
            const uint32_t stale = __atomic_add_fetch(
                &runtime->stale_i2c_completion, 1u, __ATOMIC_RELAXED);
            const uint32_t consecutive = __atomic_add_fetch(
                &runtime->consecutive_stale_completion, 1u,
                __ATOMIC_RELAXED);
            (void)stale;
            if (consecutive >= NOCTI_V3_STALE_CALLBACK_FATAL_THRESHOLD &&
                runtime->lifecycle == NOCTI_V3_STATE_RUNNING_SCAN) {
                begin_recovery(runtime, NOCTI_V3_FAULT_STALE_CALLBACK,
                               NOCTI_V3_REASON_STALE_EPOCH,
                               ESP_ERR_INVALID_STATE, true);
                return;
            }
            continue;
        }
        __atomic_store_n(&runtime->consecutive_stale_completion, 0u,
                         __ATOMIC_RELEASE);
        if (completion.result != NOCTI_V3_LED_RESULT_DONE ||
            completion.submit_result != ESP_OK) {
            handle_led_error(runtime, &completion);
            return;
        }
        const uint64_t command_deadline =
            completion.request.request_timestamp_us +
            NOCTI_V3_LED_COMMAND_TIMEOUT_US;
        if (completion.i2c_complete_timestamp_us > command_deadline) {
            begin_recovery(runtime, NOCTI_V3_FAULT_LED_TIMEOUT,
                           NOCTI_V3_REASON_LED_TIMEOUT,
                           ESP_ERR_TIMEOUT, false);
            return;
        }
        if (runtime->stop_pending) {
            if (completion.request.type == NOCTI_V3_LED_COMMAND_SLOT_ON) {
                continue;
            }
            complete_stop(runtime, ESP_OK);
            return;
        }
        if (completion.request.command_id == runtime->current.on_command_id &&
            runtime->slot_state == NOCTI_V3_SLOT_LED_ON_PENDING) {
            runtime->current.i2c_submit_ts =
                completion.i2c_submit_timestamp_us;
            runtime->current.i2c_complete_ts =
                completion.i2c_complete_timestamp_us;
            runtime->current.led_commit_ts =
                completion.i2c_complete_timestamp_us;
            runtime->current.led_stable_start_ts =
                completion.i2c_complete_timestamp_us + NOCTI_V3_SETTLE_ON_US;
            set_slot_state(runtime, NOCTI_V3_SLOT_LED_ON_COMMITTED);
            set_slot_state(runtime, NOCTI_V3_SLOT_LED_ON_SETTLING);
            if (runtime->current.led_stable_start_ts >=
                runtime->current.nominal_led_off_ts ||
                arm_event(runtime, runtime->current.led_stable_start_ts,
                          SCHED_EVENT_ON_STABLE) != ESP_OK) {
                const bool fatal = __atomic_load_n(
                    &runtime->consecutive_scheduler_faults,
                    __ATOMIC_ACQUIRE) >= NOCTI_V3_SCHEDULER_FATAL_THRESHOLD;
                begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                               NOCTI_V3_REASON_SCHEDULER_MISS,
                               ESP_ERR_INVALID_STATE, fatal);
                return;
            }
        } else if (completion.request.command_id ==
                       runtime->current.off_command_id &&
                   runtime->slot_state == NOCTI_V3_SLOT_LED_OFF_PENDING) {
            runtime->current.led_off_submit_ts =
                completion.i2c_submit_timestamp_us;
            runtime->current.led_off_complete_ts =
                completion.i2c_complete_timestamp_us;
            runtime->current.led_off_commit_ts =
                completion.i2c_complete_timestamp_us;
            runtime->current.led_off_stable_start_ts =
                completion.i2c_complete_timestamp_us + NOCTI_V3_SETTLE_OFF_US;
            set_slot_state(runtime, NOCTI_V3_SLOT_LED_OFF_COMMITTED);
            set_slot_state(runtime, NOCTI_V3_SLOT_LED_OFF_SETTLING);
            if (runtime->current.led_off_stable_start_ts >=
                runtime->current.slot_guard_start_ts ||
                arm_event(runtime,
                          runtime->current.led_off_stable_start_ts,
                          SCHED_EVENT_OFF_STABLE) != ESP_OK) {
                const bool fatal = __atomic_load_n(
                    &runtime->consecutive_scheduler_faults,
                    __ATOMIC_ACQUIRE) >= NOCTI_V3_SCHEDULER_FATAL_THRESHOLD;
                begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                               NOCTI_V3_REASON_SCHEDULER_MISS,
                               ESP_ERR_INVALID_STATE, fatal);
                return;
            }
        } else {
            (void)__atomic_fetch_add(&runtime->stale_i2c_completion, 1u,
                                     __ATOMIC_RELAXED);
        }
    }
}

static void process_deadline(optical_runtime_t *runtime)
{
    nocti_v3_deadline_event_t event;
    while (nocti_v3_timebase_take_deadline_event(runtime->timebase, &event)) {
        if (event.generation != runtime->generation ||
            (!runtime->scan_active && !runtime->stop_pending)) continue;
        uint64_t handled_us = event.callback_timestamp_us;
        (void)nocti_v3_timebase_now_us(runtime->timebase, &handled_us);
        if (handled_us > event.alarm_timestamp_us) {
            update_max_u32(&runtime->scheduler_lateness_max_us,
                (uint32_t)(handled_us - event.alarm_timestamp_us));
        }
        switch ((scheduler_event_id_t)event.event_id) {
            case SCHED_EVENT_SLOT_START:
                if (begin_slot(runtime, 0u, runtime->frame_start_ts) != ESP_OK) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_FAIL, false);
                }
                break;
            case SCHED_EVENT_ON_STABLE:
                if (runtime->slot_state != NOCTI_V3_SLOT_LED_ON_SETTLING) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_ERR_INVALID_STATE, false);
                    break;
                }
                set_slot_state(runtime, NOCTI_V3_SLOT_LED_ON_COLLECT);
                if (arm_event(runtime, runtime->current.nominal_led_off_ts,
                              SCHED_EVENT_LED_OFF) != ESP_OK) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_ERR_INVALID_STATE, false);
                }
                break;
            case SCHED_EVENT_LED_OFF:
                if (runtime->slot_state != NOCTI_V3_SLOT_LED_ON_COLLECT ||
                    submit_led(runtime, NOCTI_V3_LED_COMMAND_ALL_OFF) != ESP_OK) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_ERR_INVALID_STATE, false);
                }
                break;
            case SCHED_EVENT_OFF_STABLE:
                if (runtime->slot_state != NOCTI_V3_SLOT_LED_OFF_SETTLING) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_ERR_INVALID_STATE, false);
                    break;
                }
                set_slot_state(runtime, NOCTI_V3_SLOT_LED_OFF_COLLECT);
                if (arm_event(runtime, runtime->current.slot_guard_start_ts,
                              SCHED_EVENT_GUARD_START) != ESP_OK) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_ERR_INVALID_STATE, false);
                }
                break;
            case SCHED_EVENT_GUARD_START:
                set_slot_state(runtime, NOCTI_V3_SLOT_END_GUARD);
                if (arm_event(runtime, runtime->current.nominal_slot_end_ts,
                              SCHED_EVENT_SLOT_FINALIZE) != ESP_OK) {
                    begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                   NOCTI_V3_REASON_SCHEDULER_MISS,
                                   ESP_ERR_INVALID_STATE, false);
                }
                break;
            case SCHED_EVENT_SLOT_FINALIZE: {
                set_slot_state(runtime, NOCTI_V3_SLOT_FINALIZE);
                if (!finalize_current_slot(runtime, NOCTI_V3_REASON_NONE,
                                           ESP_OK)) {
                    begin_recovery(runtime,
                                   NOCTI_V3_FAULT_OPTICAL_PROCESSING,
                                   NOCTI_V3_REASON_TIMING_RING_OVERFLOW,
                                   ESP_ERR_NO_MEM, true);
                    break;
                }
                uint64_t next_start = runtime->current.nominal_slot_end_ts;
                if (runtime->slot_index + 1u < NOCTI_V3_SLOT_COUNT) {
                    if (begin_slot(runtime,
                            (uint8_t)(runtime->slot_index + 1u),
                            next_start) != ESP_OK) {
                        begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                       NOCTI_V3_REASON_SCHEDULER_MISS,
                                       ESP_FAIL, false);
                    }
                } else {
                    runtime->frame_index++;
                    runtime->frame_start_ts += NOCTI_V3_FRAME_PERIOD_US;
                    if (begin_slot(runtime, 0u,
                                   runtime->frame_start_ts) != ESP_OK) {
                        begin_recovery(runtime, NOCTI_V3_FAULT_SCHEDULER,
                                       NOCTI_V3_REASON_SCHEDULER_MISS,
                                       ESP_FAIL, false);
                    }
                }
                break;
            }
            case SCHED_EVENT_COMMAND_TIMEOUT:
                begin_recovery(runtime, NOCTI_V3_FAULT_LED_TIMEOUT,
                               NOCTI_V3_REASON_LED_TIMEOUT,
                               ESP_ERR_TIMEOUT, false);
                break;
            default:
                (void)__atomic_fetch_add(&runtime->scheduler_event_overflow,
                                         1u, __ATOMIC_RELAXED);
                break;
        }
    }
}

static void processing_clear_state(optical_runtime_t *runtime)
{
    runtime->pending_raw_head = 0u;
    runtime->pending_raw_tail = 0u;
    runtime->raw_timestamp_watermark = 0u;
    runtime->pending_overflow_for_frame = false;
    memset(&runtime->builder, 0, sizeof(runtime->builder));
    const uint32_t timing_head = __atomic_load_n(&runtime->timing_head,
                                                 __ATOMIC_ACQUIRE);
    __atomic_store_n(&runtime->timing_tail, timing_head, __ATOMIC_RELEASE);
}

static esp_err_t request_processing_reset(optical_runtime_t *runtime)
{
    (void)xSemaphoreTake(runtime->processing_reset_done, 0u);
    __atomic_store_n(&runtime->processing_reset_requested, true,
                     __ATOMIC_RELEASE);
    xTaskNotifyGive(runtime->processing_task);
    return xSemaphoreTake(
        runtime->processing_reset_done,
        pdMS_TO_TICKS(NOCTI_V3_PROCESSING_RESET_TIMEOUT_MS)) == pdTRUE ?
        ESP_OK : ESP_ERR_TIMEOUT;
}

static void frame_sink_clear_state(optical_runtime_t *runtime)
{
    const uint32_t frame_head = __atomic_load_n(&runtime->frame_head,
                                                __ATOMIC_ACQUIRE);
    __atomic_store_n(&runtime->frame_tail, frame_head, __ATOMIC_RELEASE);
    __atomic_add_fetch(&runtime->latest_frame_generation, 1u,
                       __ATOMIC_ACQ_REL);
    __atomic_store_n(&runtime->latest_frame_valid, false, __ATOMIC_RELEASE);
    memset(&runtime->latest_frame, 0, sizeof(runtime->latest_frame));
    __atomic_add_fetch(&runtime->latest_frame_generation, 1u,
                       __ATOMIC_RELEASE);
}

static esp_err_t request_frame_sink_reset(optical_runtime_t *runtime)
{
    (void)xSemaphoreTake(runtime->frame_sink_reset_done, 0u);
    __atomic_store_n(&runtime->frame_sink_reset_requested, true,
                     __ATOMIC_RELEASE);
    xTaskNotifyGive(runtime->frame_sink_task);
    return xSemaphoreTake(
        runtime->frame_sink_reset_done,
        pdMS_TO_TICKS(NOCTI_V3_PROCESSING_RESET_TIMEOUT_MS)) == pdTRUE ?
        ESP_OK : ESP_ERR_TIMEOUT;
}

static bool pending_push(optical_runtime_t *runtime,
                         const nocti_v3_raw_sample_t *sample)
{
    if (runtime->pending_raw_head - runtime->pending_raw_tail >=
        NOCTI_V3_PENDING_RAW_CAPACITY) {
        (void)__atomic_fetch_add(&runtime->sample_scratch_overflow, 1u,
                                 __ATOMIC_RELAXED);
        runtime->pending_overflow_for_frame = true;
        return false;
    }
    runtime->pending_raw[runtime->pending_raw_head & PENDING_RAW_MASK] = *sample;
    runtime->pending_raw_head++;
    if (sample->drdy_timestamp_us > runtime->raw_timestamp_watermark) {
        runtime->raw_timestamp_watermark = sample->drdy_timestamp_us;
    }
    return true;
}

static bool pending_peek(optical_runtime_t *runtime,
                         nocti_v3_raw_sample_t *out)
{
    if (runtime->pending_raw_tail == runtime->pending_raw_head) return false;
    *out = runtime->pending_raw[runtime->pending_raw_tail & PENDING_RAW_MASK];
    return true;
}

static void pending_consume(optical_runtime_t *runtime)
{
    if (runtime->pending_raw_tail != runtime->pending_raw_head) {
        runtime->pending_raw_tail++;
    }
}

static bool sample_integrity_valid(const nocti_v3_raw_sample_t *sample)
{
    return sample->spi_result == ESP_OK && sample->crc_valid &&
           sample->status_valid;
}

static uint8_t latest_contiguous_mean(
    const nocti_v3_raw_sample_t *samples,
    size_t count,
    uint64_t start_ts,
    uint64_t end_ts,
    uint8_t target_k,
    uint32_t session_epoch,
    int32_t out_average[4],
    bool *out_crc_bad,
    bool *out_spi_bad,
    bool *out_status_bad,
    bool *out_sequence_gap)
{
    int64_t sum[4] = {0, 0, 0, 0};
    uint8_t used = 0u;
    bool have_newer = false;
    uint32_t newer_sequence = 0u;
    if (out_crc_bad != NULL) *out_crc_bad = false;
    if (out_spi_bad != NULL) *out_spi_bad = false;
    if (out_status_bad != NULL) *out_status_bad = false;
    if (out_sequence_gap != NULL) *out_sequence_gap = false;
    for (size_t reverse = count; reverse > 0u; --reverse) {
        const nocti_v3_raw_sample_t *sample = &samples[reverse - 1u];
        if (sample->drdy_timestamp_us >= end_ts ||
            sample->drdy_timestamp_us < start_ts) continue;
        if (sample->session_epoch != session_epoch) break;
        if (!sample_integrity_valid(sample)) {
            if (!sample->crc_valid && out_crc_bad != NULL) *out_crc_bad = true;
            if (sample->spi_result != ESP_OK && out_spi_bad != NULL) *out_spi_bad = true;
            if (!sample->status_valid && out_status_bad != NULL) *out_status_bad = true;
            break;
        }
        if (have_newer && newer_sequence - sample->drdy_sequence != 1u) {
            if (out_sequence_gap != NULL) *out_sequence_gap = true;
            break;
        }
        for (size_t channel = 0u; channel < 4u; ++channel) {
            sum[channel] += sample->channel[channel];
        }
        used++;
        newer_sequence = sample->drdy_sequence;
        have_newer = true;
        if (used >= target_k) break;
    }
    if (used == 0u) {
        memset(out_average, 0, 4u * sizeof(out_average[0]));
        return 0u;
    }
    for (size_t channel = 0u; channel < 4u; ++channel) {
        out_average[channel] = (int32_t)(sum[channel] / (int64_t)used);
    }
    return used;
}

static int32_t saturating_sub_i32(int32_t left, int32_t right)
{
    const int64_t value = (int64_t)left - (int64_t)right;
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return (int32_t)value;
}

static void classify_sample_counter(optical_runtime_t *runtime,
                                    const nocti_v3_slot_timing_t *timing,
                                    const nocti_v3_raw_sample_t *sample,
                                    uint8_t *on_available,
                                    uint8_t *off_available)
{
    const uint64_t ts = sample->drdy_timestamp_us;
    if (sample->session_epoch != timing->session_epoch) {
        (void)__atomic_fetch_add(&runtime->raw_stale_epoch, 1u,
                                 __ATOMIC_RELAXED);
        return;
    }
    if (ts >= timing->slot_guard_start_ts &&
        ts < timing->nominal_slot_end_ts) {
        (void)__atomic_fetch_add(&runtime->raw_guard, 1u, __ATOMIC_RELAXED);
        return;
    }
    if (!timing->valid_timing || timing->led_commit_ts == 0u ||
        timing->led_off_commit_ts == 0u) {
        (void)__atomic_fetch_add(&runtime->raw_ambiguous, 1u,
                                 __ATOMIC_RELAXED);
        return;
    }
    if ((ts >= timing->nominal_slot_start_ts && ts < timing->led_commit_ts) ||
        (ts >= timing->led_off_request_ts &&
         ts < timing->led_off_commit_ts)) {
        (void)__atomic_fetch_add(&runtime->raw_transition, 1u,
                                 __ATOMIC_RELAXED);
        return;
    }
    if ((ts >= timing->led_commit_ts &&
         ts < timing->led_stable_start_ts) ||
        (ts >= timing->led_off_commit_ts &&
         ts < timing->led_off_stable_start_ts)) {
        (void)__atomic_fetch_add(&runtime->raw_settle, 1u,
                                 __ATOMIC_RELAXED);
        return;
    }
    if (ts >= timing->led_stable_start_ts &&
        ts < timing->led_off_request_ts) {
        if (*on_available < UINT8_MAX) (*on_available)++;
        (void)__atomic_fetch_add(&runtime->raw_stable_on, 1u,
                                 __ATOMIC_RELAXED);
        return;
    }
    if (ts >= timing->led_off_stable_start_ts &&
        ts < timing->slot_guard_start_ts) {
        if (*off_available < UINT8_MAX) (*off_available)++;
        (void)__atomic_fetch_add(&runtime->raw_stable_off, 1u,
                                 __ATOMIC_RELAXED);
        return;
    }
    (void)__atomic_fetch_add(&runtime->raw_ambiguous, 1u,
                             __ATOMIC_RELAXED);
}

static nocti_v3_slot_result_t process_slot_timing(
    optical_runtime_t *runtime,
    const nocti_v3_slot_timing_t *timing)
{
    nocti_v3_slot_result_t result;
    memset(&result, 0, sizeof(result));
    result.slot_index = timing->descriptor.slot_index;
    result.source_id = timing->descriptor.source_id;
    result.geometry_id = timing->descriptor.geometry_id;
    result.wavelength_nm = timing->descriptor.wavelength_nm;
    result.pca_output = timing->descriptor.pca_output;
    result.reason = timing->reason;

    nocti_v3_raw_sample_t *slot_samples = runtime->slot_scratch;
    size_t count = 0u;
    nocti_v3_raw_sample_t sample;
    while (pending_peek(runtime, &sample) &&
           sample.drdy_timestamp_us < timing->nominal_slot_end_ts) {
        pending_consume(runtime);
        if (sample.drdy_timestamp_us < timing->nominal_slot_start_ts) {
            (void)__atomic_fetch_add(&runtime->raw_ambiguous, 1u,
                                     __ATOMIC_RELAXED);
            continue;
        }
        if (count < NOCTI_V3_MAX_RAW_SAMPLES_PER_SLOT) {
            slot_samples[count++] = sample;
        } else {
            (void)__atomic_fetch_add(&runtime->sample_scratch_overflow, 1u,
                                     __ATOMIC_RELAXED);
            runtime->pending_overflow_for_frame = true;
        }
    }

    for (size_t i = 0u; i < count; ++i) {
        classify_sample_counter(runtime, timing, &slot_samples[i],
                                &result.n_on_available,
                                &result.n_i_led_off_available);
    }

    bool on_crc = false, on_spi = false, on_status = false, on_gap = false;
    bool off_crc = false, off_spi = false, off_status = false, off_gap = false;
    if (timing->valid_timing) {
        result.n_on_used = latest_contiguous_mean(
            slot_samples, count,
            timing->led_stable_start_ts,
            timing->led_off_request_ts,
            timing->descriptor.k_target_on,
            timing->session_epoch,
            result.i_led_on,
            &on_crc, &on_spi, &on_status, &on_gap);
        result.n_i_led_off_used = latest_contiguous_mean(
            slot_samples, count,
            timing->led_off_stable_start_ts,
            timing->slot_guard_start_ts,
            timing->descriptor.k_target_off,
            timing->session_epoch,
            result.i_led_off,
            &off_crc, &off_spi, &off_status, &off_gap);
    }
    result.crc_bad_seen = on_crc || off_crc;
    result.spi_error_seen = on_spi || off_spi;
    result.status_error_seen = on_status || off_status;
    result.sequence_gap_seen = on_gap || off_gap;

    update_min_max_sentinel(&runtime->min_n_on, result.n_on_used);
    update_min_max_sentinel(&runtime->min_n_off,
                            result.n_i_led_off_used);
    if (result.n_on_used < timing->descriptor.k_min_on) {
        (void)__atomic_fetch_add(&runtime->underk_on, 1u,
                                 __ATOMIC_RELAXED);
    } else if (result.n_on_used < timing->descriptor.k_target_on) {
        (void)__atomic_fetch_add(&runtime->subtarget_on, 1u,
                                 __ATOMIC_RELAXED);
    }
    if (result.n_i_led_off_used < timing->descriptor.k_min_off) {
        (void)__atomic_fetch_add(&runtime->underk_off, 1u,
                                 __ATOMIC_RELAXED);
    } else if (result.n_i_led_off_used < timing->descriptor.k_target_off) {
        (void)__atomic_fetch_add(&runtime->subtarget_off, 1u,
                                 __ATOMIC_RELAXED);
    }

    if (!timing->valid_timing) {
        result.quality = NOCTI_V3_SLOT_QUALITY_INVALID;
        result.reason = timing->reason == NOCTI_V3_REASON_NONE ?
            NOCTI_V3_REASON_TIMING_INVALID : timing->reason;
    } else if (runtime->pending_overflow_for_frame) {
        result.quality = NOCTI_V3_SLOT_QUALITY_INVALID;
        result.reason = NOCTI_V3_REASON_SAMPLE_BUFFER_OVERFLOW;
    } else if (result.n_on_used >= timing->descriptor.k_target_on &&
               result.n_i_led_off_used >= timing->descriptor.k_target_off) {
        result.quality = NOCTI_V3_SLOT_QUALITY_VALID_TARGET;
        result.reason = NOCTI_V3_REASON_NONE;
    } else if (result.n_on_used >= timing->descriptor.k_min_on &&
               result.n_i_led_off_used >= timing->descriptor.k_min_off) {
        result.quality = NOCTI_V3_SLOT_QUALITY_VALID_MINIMUM;
        result.reason = NOCTI_V3_REASON_NONE;
    } else if (result.n_on_used > 0u || result.n_i_led_off_used > 0u) {
        result.quality = NOCTI_V3_SLOT_QUALITY_PARTIAL;
        result.reason = NOCTI_V3_REASON_UNDER_K;
    } else {
        result.quality = NOCTI_V3_SLOT_QUALITY_INVALID;
        result.reason = result.crc_bad_seen || result.spi_error_seen ||
                        result.status_error_seen || result.sequence_gap_seen ?
            NOCTI_V3_REASON_SAMPLE_INTEGRITY : NOCTI_V3_REASON_UNDER_K;
    }

    result.corrected_valid =
        result.quality == NOCTI_V3_SLOT_QUALITY_VALID_TARGET ||
        result.quality == NOCTI_V3_SLOT_QUALITY_VALID_MINIMUM;
    if (result.corrected_valid) {
        for (size_t channel = 0u; channel < 4u; ++channel) {
            result.i_corrected[channel] = saturating_sub_i32(
                result.i_led_on[channel], result.i_led_off[channel]);
        }
    }
    (void)__atomic_fetch_add(&runtime->slots_total, 1u, __ATOMIC_RELAXED);
    return result;
}

static void frame_builder_start(frame_builder_t *builder,
                                const nocti_v3_slot_timing_t *timing)
{
    memset(builder, 0, sizeof(*builder));
    builder->active = true;
    builder->frame.session_epoch = timing->session_epoch;
    builder->frame.config_generation = timing->config_generation;
    builder->frame.frame_index = timing->frame_index;
    builder->frame.frame_start_ts = timing->nominal_slot_start_ts -
        timing->descriptor.nominal_start_offset_us;
    builder->frame.frame_end_ts = builder->frame.frame_start_ts +
                                  NOCTI_V3_FRAME_PERIOD_US;
    builder->frame.reason = NOCTI_V3_REASON_NONE;
}

static void finalize_builder(optical_runtime_t *runtime,
                             nocti_v3_optical_reason_t forced_reason)
{
    if (!runtime->builder.active) return;
    nocti_v3_completed_frame_t *frame = &runtime->builder.frame;
    const uint32_t missing = NOCTI_V3_FRAME_ALL_SLOTS_MASK &
                             ~frame->present_mask;
    if (missing != 0u) {
        frame->invalid_mask |= missing;
        frame->reason = forced_reason == NOCTI_V3_REASON_NONE ?
            NOCTI_V3_REASON_MISSING_SLOT : forced_reason;
    }
    const uint16_t expected_pairs =
        (uint16_t)((1u << (NOCTI_V3_SLOT_COUNT / 2u)) - 1u);
    const uint16_t missing_pairs = (uint16_t)(expected_pairs &
                                             ~frame->pair_present_mask);
    if (missing_pairs != 0u) {
        frame->pair_invalid_mask |= missing_pairs;
        if (frame->reason == NOCTI_V3_REASON_NONE)
            frame->reason = NOCTI_V3_REASON_MISSING_SLOT;
    }
    if (frame->invalid_mask != 0u || missing_pairs != 0u) {
        frame->quality = NOCTI_V3_FRAME_QUALITY_INVALID;
        if (frame->reason == NOCTI_V3_REASON_NONE)
            frame->reason = NOCTI_V3_REASON_INTERNAL;
        (void)__atomic_fetch_add(&runtime->invalid_frames, 1u,
                                 __ATOMIC_RELAXED);
    } else if (frame->partial_mask != 0u) {
        frame->quality = NOCTI_V3_FRAME_QUALITY_PARTIAL;
        frame->reason = NOCTI_V3_REASON_UNDER_K;
        (void)__atomic_fetch_add(&runtime->partial_frames, 1u,
                                 __ATOMIC_RELAXED);
    } else {
        frame->quality = NOCTI_V3_FRAME_QUALITY_VALID;
        frame->reason = NOCTI_V3_REASON_NONE;
        (void)__atomic_fetch_add(&runtime->frames_valid, 1u,
                                 __ATOMIC_RELAXED);
    }
    (void)__atomic_fetch_add(&runtime->frames_total, 1u, __ATOMIC_RELAXED);
    if (!frame_push(runtime, frame)) {
        runtime->last_reason = NOCTI_V3_REASON_FRAME_RING_OVERFLOW;
        runtime->last_error = ESP_ERR_NO_MEM;
    }
    memset(&runtime->builder, 0, sizeof(runtime->builder));
    runtime->pending_overflow_for_frame = false;
}

static void update_wavelength_pair(nocti_v3_completed_frame_t *frame,
                                   const nocti_v3_slot_result_t *slot)
{
    if (slot->source_id >= NOCTI_V3_SLOT_COUNT / 2u) return;
    nocti_v3_wavelength_pair_t *pair = &frame->pair[slot->source_id];
    pair->source_id = slot->source_id;
    pair->geometry_id = slot->geometry_id;
    const uint16_t pair_bit = (uint16_t)(1u << slot->source_id);
    if (slot->wavelength_nm == 735u) {
        pair->slot_735 = slot->slot_index;
        pair->present_735 = true;
        pair->valid_735 = slot->corrected_valid;
        pair->quality_735 = slot->quality;
        memcpy(pair->channel_i735, slot->i_corrected,
               sizeof(pair->channel_i735));
    } else if (slot->wavelength_nm == 850u) {
        pair->slot_850 = slot->slot_index;
        pair->present_850 = true;
        pair->valid_850 = slot->corrected_valid;
        pair->quality_850 = slot->quality;
        memcpy(pair->channel_i850, slot->i_corrected,
               sizeof(pair->channel_i850));
    }
    if (pair->present_735 && pair->present_850) {
        frame->pair_present_mask |= pair_bit;
        pair->pair_valid = pair->valid_735 && pair->valid_850;
        if (pair->pair_valid) frame->pair_valid_mask |= pair_bit;
        else frame->pair_invalid_mask |= pair_bit;
    }
}

static void assemble_slot_result(optical_runtime_t *runtime,
                                 const nocti_v3_slot_timing_t *timing,
                                 const nocti_v3_slot_result_t *slot)
{
    if (!runtime->builder.active) {
        frame_builder_start(&runtime->builder, timing);
    } else if (runtime->builder.frame.session_epoch != timing->session_epoch ||
               runtime->builder.frame.config_generation !=
                   timing->config_generation ||
               runtime->builder.frame.frame_index != timing->frame_index) {
        finalize_builder(runtime, NOCTI_V3_REASON_MISSING_SLOT);
        frame_builder_start(&runtime->builder, timing);
    }
    nocti_v3_completed_frame_t *frame = &runtime->builder.frame;
    const uint32_t bit = 1u << slot->slot_index;
    if ((frame->present_mask & bit) != 0u) {
        frame->invalid_mask |= bit;
        frame->reason = NOCTI_V3_REASON_INTERNAL;
        return;
    }
    frame->slot[slot->slot_index] = *slot;
    frame->present_mask |= bit;
    update_wavelength_pair(frame, slot);
    if (slot->quality == NOCTI_V3_SLOT_QUALITY_VALID_TARGET) {
        frame->target_mask |= bit;
    } else if (slot->quality == NOCTI_V3_SLOT_QUALITY_VALID_MINIMUM) {
        frame->minimum_mask |= bit;
    } else if (slot->quality == NOCTI_V3_SLOT_QUALITY_PARTIAL) {
        frame->partial_mask |= bit;
    } else {
        frame->invalid_mask |= bit;
    }
    if (slot->n_on_used < s_descriptors[slot->slot_index].k_min_on)
        frame->underk_on_mask |= bit;
    else if (slot->n_on_used < s_descriptors[slot->slot_index].k_target_on)
        frame->subtarget_on_mask |= bit;
    if (slot->n_i_led_off_used <
        s_descriptors[slot->slot_index].k_min_off)
        frame->underk_i_led_off_mask |= bit;
    else if (slot->n_i_led_off_used <
             s_descriptors[slot->slot_index].k_target_off)
        frame->subtarget_i_led_off_mask |= bit;
    if (!timing->valid_timing) frame->timing_invalid_mask |= bit;
    if (slot->crc_bad_seen) frame->crc_mask |= bit;
    if (slot->spi_error_seen) frame->spi_error_mask |= bit;
    if (slot->status_error_seen) frame->status_error_mask |= bit;
    if (frame->present_mask == NOCTI_V3_FRAME_ALL_SLOTS_MASK ||
        slot->slot_index == NOCTI_V3_SLOT_COUNT - 1u) {
        finalize_builder(runtime, NOCTI_V3_REASON_NONE);
    }
}

static bool process_timing_records(optical_runtime_t *runtime, bool force)
{
    bool did_work = false;
    nocti_v3_slot_timing_t timing;
    while (timing_peek(runtime, &timing)) {
        if (!force && runtime->raw_timestamp_watermark <
                      timing.nominal_slot_end_ts) break;
        timing_consume(runtime);
        const nocti_v3_slot_result_t result =
            process_slot_timing(runtime, &timing);
        assemble_slot_result(runtime, &timing, &result);
        did_work = true;
    }
    return did_work;
}

static void processing_task_main(void *argument)
{
    optical_runtime_t *runtime = (optical_runtime_t *)argument;
    runtime->processing_core = xPortGetCoreID();
    for (;;) {
        if (__atomic_exchange_n(&runtime->processing_reset_requested, false,
                                __ATOMIC_ACQ_REL)) {
            processing_clear_state(runtime);
            (void)xSemaphoreGive(runtime->processing_reset_done);
        }

        if (runtime->mode == NOCTI_V3_MODE_SCAN) {
            nocti_v3_raw_sample_t sample;
            while (nocti_v3_adc_pop_raw_sample(runtime->adc, &sample)) {
                (void)pending_push(runtime, &sample);
            }
            (void)process_timing_records(runtime, false);
        }

        if (runtime->mode == NOCTI_V3_MODE_SCAN &&
            nocti_v3_adc_drain_requested(runtime->adc) &&
            nocti_v3_adc_raw_depth(runtime->adc) == 0u) {
            (void)process_timing_records(runtime, true);
            if (runtime->builder.active) {
                finalize_builder(runtime, NOCTI_V3_REASON_STOP_REQUEST);
            }
            runtime->pending_raw_tail = runtime->pending_raw_head;
            (void)nocti_v3_adc_acknowledge_drain(runtime->adc);
        }

        __atomic_store_n(&runtime->processing_stack_watermark,
                         uxTaskGetStackHighWaterMark(NULL), __ATOMIC_RELEASE);
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5u));
    }
}

static void frame_sink_task_main(void *argument)
{
    optical_runtime_t *runtime = (optical_runtime_t *)argument;
    runtime->frame_sink_core = xPortGetCoreID();
    for (;;) {
        if (__atomic_exchange_n(&runtime->frame_sink_reset_requested, false,
                                __ATOMIC_ACQ_REL)) {
            frame_sink_clear_state(runtime);
            (void)xSemaphoreGive(runtime->frame_sink_reset_done);
        }
        if (!__atomic_load_n(&runtime->external_frame_consumer_bound,
                             __ATOMIC_ACQUIRE)) {
            nocti_v3_completed_frame_t *frame = &runtime->frame_sink_scratch;
            while (frame_pop(runtime, frame)) {
                const uint32_t current_session = __atomic_load_n(
                    &runtime->session_epoch, __ATOMIC_ACQUIRE);
                const uint32_t current_config = __atomic_load_n(
                    &runtime->config_generation, __ATOMIC_ACQUIRE);
                if (frame->session_epoch != current_session ||
                    frame->config_generation != current_config) {
                    (void)__atomic_fetch_add(&runtime->raw_stale_epoch, 1u,
                                             __ATOMIC_RELAXED);
                    continue;
                }
                __atomic_add_fetch(&runtime->latest_frame_generation, 1u,
                                   __ATOMIC_ACQ_REL);
                runtime->latest_frame = *frame;
                __atomic_store_n(&runtime->latest_frame_valid, true,
                                 __ATOMIC_RELEASE);
                __atomic_add_fetch(&runtime->latest_frame_generation, 1u,
                                   __ATOMIC_RELEASE);
                (void)__atomic_fetch_add(&runtime->completed_frames_consumed, 1u,
                                         __ATOMIC_RELAXED);
            }
        }
        __atomic_store_n(&runtime->frame_sink_stack_watermark,
                         uxTaskGetStackHighWaterMark(NULL), __ATOMIC_RELEASE);
        (void)ulTaskNotifyTake(pdTRUE,
                              pdMS_TO_TICKS(NOCTI_V3_FRAME_SINK_WAIT_MS));
    }
}

static esp_err_t start_scan(optical_runtime_t *runtime)
{
    if (runtime->lifecycle == NOCTI_V3_STATE_RUNNING_SCAN &&
        runtime->mode == NOCTI_V3_MODE_SCAN) return ESP_OK;
    if (runtime->lifecycle != NOCTI_V3_STATE_STOPPED ||
        !nocti_v3_adc_ready(runtime->adc) ||
        !nocti_v3_led_runtime_ready(runtime->led)) return ESP_ERR_INVALID_STATE;
    runtime->lifecycle = NOCTI_V3_STATE_STARTING_SCAN;
    runtime->mode = NOCTI_V3_MODE_SCAN;
    update_public_lifecycle(runtime);
    runtime->config_generation++;
    esp_err_t err = request_processing_reset(runtime);
    if (err != ESP_OK) goto fail;
    err = request_frame_sink_reset(runtime);
    if (err != ESP_OK) goto fail;
    err = nocti_v3_adc_start_scan(runtime->adc);
    if (err != ESP_OK) goto fail;
    nocti_v3_adc_health_snapshot_t adc_health;
    nocti_v3_adc_health_snapshot(runtime->adc, &adc_health);
    runtime->session_epoch = adc_health.session_epoch;
    runtime->generation++;
    runtime->frame_index = 0u;
    runtime->slot_epoch = 0u;
    runtime->next_command_id = 0u;
    runtime->slot_index = 0u;
    runtime->scan_active = true;
    runtime->stop_pending = false;
    runtime->recovery_fatal = false;
    runtime->lifecycle = NOCTI_V3_STATE_RUNNING_SCAN;
    runtime->last_reason = NOCTI_V3_REASON_NONE;
    runtime->last_error = ESP_OK;
    update_public_lifecycle(runtime);
    uint64_t now = 0u;
    (void)nocti_v3_timebase_now_us(runtime->timebase, &now);
    runtime->frame_start_ts = now + NOCTI_V3_SCAN_START_LEAD_US;
    set_slot_state(runtime, NOCTI_V3_SLOT_IDLE);
    err = arm_event(runtime, runtime->frame_start_ts, SCHED_EVENT_SLOT_START);
    if (err == ESP_OK) return ESP_OK;
    (void)nocti_v3_adc_stop(runtime->adc);
fail:
    runtime->scan_active = false;
    runtime->mode = NOCTI_V3_MODE_STOPPED;
    runtime->lifecycle = NOCTI_V3_STATE_STOPPED;
    update_public_lifecycle(runtime);
    return err;
}

static esp_err_t start_dark(optical_runtime_t *runtime)
{
    if (runtime->lifecycle == NOCTI_V3_STATE_RUNNING_DARK &&
        runtime->mode == NOCTI_V3_MODE_DARK) return ESP_OK;
    if (runtime->lifecycle != NOCTI_V3_STATE_STOPPED ||
        !nocti_v3_adc_ready(runtime->adc) ||
        !nocti_v3_led_runtime_ready(runtime->led)) return ESP_ERR_INVALID_STATE;
    runtime->lifecycle = NOCTI_V3_STATE_STARTING_DARK;
    runtime->mode = NOCTI_V3_MODE_DARK;
    update_public_lifecycle(runtime);
    runtime->config_generation++;
    esp_err_t err = request_processing_reset(runtime);
    if (err == ESP_OK) err = request_frame_sink_reset(runtime);
    if (err == ESP_OK) err = nocti_v3_adc_start_dark(runtime->adc);
    if (err == ESP_OK) {
        nocti_v3_adc_health_snapshot_t h;
        nocti_v3_adc_health_snapshot(runtime->adc, &h);
        runtime->session_epoch = h.session_epoch;
        runtime->lifecycle = NOCTI_V3_STATE_RUNNING_DARK;
    } else {
        runtime->mode = NOCTI_V3_MODE_STOPPED;
        runtime->lifecycle = NOCTI_V3_STATE_STOPPED;
    }
    update_public_lifecycle(runtime);
    return err;
}

static void process_control(optical_runtime_t *runtime)
{
    control_command_t command;
    while (xQueueReceive(runtime->control_queue, &command, 0u) == pdTRUE) {
        if (command.type == CONTROL_START) {
            runtime->control_result = command.mode == NOCTI_V3_MODE_SCAN ?
                start_scan(runtime) : start_dark(runtime);
            (void)xSemaphoreGive(runtime->control_done);
        } else if (command.type == CONTROL_STOP) {
            if (runtime->lifecycle == NOCTI_V3_STATE_STOPPED) {
                runtime->control_result = ESP_OK;
                (void)xSemaphoreGive(runtime->control_done);
            } else if (runtime->lifecycle == NOCTI_V3_STATE_RECOVERING) {
                runtime->stop_pending = true;
            } else if (runtime->mode == NOCTI_V3_MODE_DARK) {
                complete_stop(runtime, ESP_OK);
            } else {
                runtime->scan_active = false;
                runtime->stop_pending = true;
                runtime->lifecycle = NOCTI_V3_STATE_STOPPING;
                runtime->last_reason = NOCTI_V3_REASON_STOP_REQUEST;
                update_public_lifecycle(runtime);
                runtime->generation++;
                (void)nocti_v3_timebase_disarm_deadline(runtime->timebase);
                set_slot_state(runtime, NOCTI_V3_SLOT_INVALID);
                if (!runtime->current.finalized) {
                    (void)finalize_current_slot(runtime,
                        NOCTI_V3_REASON_STOP_REQUEST,
                        ESP_ERR_INVALID_STATE);
                }
                uint64_t now = 0u;
                (void)nocti_v3_timebase_now_us(runtime->timebase, &now);
                nocti_v3_led_request_t off = {
                    .type = NOCTI_V3_LED_COMMAND_FORCE_OFF,
                    .slot_index = runtime->slot_index,
                    .command_id = ++runtime->next_command_id,
                    .session_epoch = runtime->session_epoch,
                    .frame_index = runtime->frame_index,
                    .slot_epoch = runtime->slot_epoch,
                    .request_timestamp_us = now,
                };
                runtime->current.off_command_id = off.command_id;
                if (nocti_v3_led_submit(runtime->led, &off) != ESP_OK ||
                    arm_event(runtime,
                              now + NOCTI_V3_LED_COMMAND_TIMEOUT_US,
                              SCHED_EVENT_COMMAND_TIMEOUT) != ESP_OK) {
                    (void)nocti_v3_led_force_safe_off(runtime->led);
                    complete_stop(runtime, ESP_FAIL);
                }
            }
        }
    }
}

static void scheduler_task_main(void *argument)
{
    optical_runtime_t *runtime = (optical_runtime_t *)argument;
    runtime->scheduler_core = xPortGetCoreID();
    (void)nocti_v3_timebase_bind_deadline_consumer(runtime->timebase,
                                                    runtime->scheduler_task);
    (void)nocti_v3_led_bind_completion_consumer(runtime->led,
                                                runtime->scheduler_task);
    for (;;) {
        process_control(runtime);
        process_led_completions(runtime);
        process_deadline(runtime);
        process_recovery_state(runtime);
        if (runtime->scan_active && !nocti_v3_adc_running(runtime->adc)) {
            begin_recovery(runtime, NOCTI_V3_FAULT_ADC_INTEGRITY,
                           NOCTI_V3_REASON_ADC_FAULT,
                           ESP_ERR_INVALID_STATE, true);
        }
        const uint32_t tb_overflow =
            nocti_v3_timebase_deadline_overflow(runtime->timebase);
        __atomic_store_n(&runtime->scheduler_event_overflow, tb_overflow,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->scheduler_stack_watermark,
                         uxTaskGetStackHighWaterMark(NULL), __ATOMIC_RELEASE);
        uint32_t notify = 0u;
        (void)xTaskNotifyWait(0u, UINT32_MAX, &notify,
                              pdMS_TO_TICKS(10u));
        (void)notify;
    }
}

static esp_err_t execute_control(nocti_v3_optical_t *optical,
                                 control_type_t type,
                                 nocti_v3_mode_t mode)
{
    if (optical == NULL || optical->runtime == NULL)
        return ESP_ERR_INVALID_STATE;
    optical_runtime_t *runtime = (optical_runtime_t *)optical->runtime;
    if (xSemaphoreTake(runtime->control_lock,
        pdMS_TO_TICKS(NOCTI_V3_SCHEDULER_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    (void)xSemaphoreTake(runtime->control_done, 0u);
    const control_command_t command = {.type = type, .mode = mode};
    esp_err_t result = ESP_OK;
    if (xQueueSend(runtime->control_queue, &command, 0u) != pdTRUE) {
        result = ESP_ERR_NO_MEM;
    } else {
        xTaskNotify(runtime->scheduler_task, NOTIFY_CONTROL, eSetBits);
        if (xSemaphoreTake(runtime->control_done,
            pdMS_TO_TICKS(NOCTI_V3_SCHEDULER_CONTROL_TIMEOUT_MS)) != pdTRUE)
            result = ESP_ERR_TIMEOUT;
        else
            result = runtime->control_result;
    }
    (void)xSemaphoreGive(runtime->control_lock);
    return result;
}

esp_err_t nocti_v3_optical_init(nocti_v3_optical_t *optical,
                                nocti_v3_adc_t *adc,
                                nocti_v3_led_t *led,
                                nocti_v3_timebase_t *timebase,
                                nocti_v3_health_t *health)
{
    if (optical == NULL || adc == NULL || led == NULL || timebase == NULL ||
        health == NULL || !nocti_v3_timebase_ready(timebase))
        return ESP_ERR_INVALID_ARG;
    memset(optical, 0, sizeof(*optical));
    memset(&s_runtime, 0, sizeof(s_runtime));
    optical->state = NOCTI_V3_STATE_STOPPED;
    optical->mode = NOCTI_V3_MODE_STOPPED;
    optical->adc = adc;
    optical->led = led;
    optical->timebase = timebase;
    optical->health = health;
    optical->runtime = &s_runtime;
    s_runtime.public_optical = optical;
    s_runtime.adc = adc;
    s_runtime.led = led;
    s_runtime.timebase = timebase;
    s_runtime.health = health;
    s_runtime.lifecycle = NOCTI_V3_STATE_STOPPED;
    s_runtime.mode = NOCTI_V3_MODE_STOPPED;
    s_runtime.slot_state = NOCTI_V3_SLOT_IDLE;
    s_runtime.min_n_on = UINT32_MAX;
    s_runtime.min_n_off = UINT32_MAX;
    s_runtime.external_frame_consumer_core = -1;
    s_runtime.control_lock = xSemaphoreCreateMutexStatic(
        &s_runtime.control_lock_storage);
    s_runtime.control_done = xSemaphoreCreateBinaryStatic(
        &s_runtime.control_done_storage);
    s_runtime.processing_reset_done = xSemaphoreCreateBinaryStatic(
        &s_runtime.processing_reset_done_storage);
    s_runtime.frame_sink_reset_done = xSemaphoreCreateBinaryStatic(
        &s_runtime.frame_sink_reset_done_storage);
    s_runtime.control_queue = xQueueCreateStatic(
        4u, sizeof(control_command_t), s_runtime.control_queue_buffer,
        &s_runtime.control_queue_storage);
    if (s_runtime.control_lock == NULL || s_runtime.control_done == NULL ||
        s_runtime.processing_reset_done == NULL ||
        s_runtime.frame_sink_reset_done == NULL ||
        s_runtime.control_queue == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(processing_task_main,
          "nocti_v3_optical", NOCTI_V3_STACK_OPTICAL_BYTES, &s_runtime,
          NOCTI_V3_PRIO_OPTICAL_PROCESS, &s_runtime.processing_task,
          NOCTI_V3_CORE_NETWORK) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(frame_sink_task_main,
          "nocti_v3_frame_sink", NOCTI_V3_STACK_FRAME_SINK_BYTES, &s_runtime,
          NOCTI_V3_PRIO_FRAME_SINK, &s_runtime.frame_sink_task,
          NOCTI_V3_CORE_NETWORK) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(scheduler_task_main,
          "nocti_v3_sched", NOCTI_V3_STACK_SCHEDULER_BYTES, &s_runtime,
          NOCTI_V3_PRIO_SCHEDULER, &s_runtime.scheduler_task,
          NOCTI_V3_CORE_REALTIME) != pdPASS) return ESP_ERR_NO_MEM;
    const esp_err_t err = nocti_v3_adc_bind_scan_consumer(
        adc, s_runtime.processing_task);
    if (err != ESP_OK) return err;
    nocti_v3_health_set_lifecycle(health, optical->state, optical->mode);
    return ESP_OK;
}

esp_err_t nocti_v3_optical_request_start(nocti_v3_optical_t *optical,
                                         nocti_v3_mode_t mode)
{
    if (mode != NOCTI_V3_MODE_DARK && mode != NOCTI_V3_MODE_SCAN)
        return ESP_ERR_NOT_SUPPORTED;
    return execute_control(optical, CONTROL_START, mode);
}

esp_err_t nocti_v3_optical_stop(nocti_v3_optical_t *optical)
{
    return execute_control(optical, CONTROL_STOP, NOCTI_V3_MODE_STOPPED);
}

void nocti_v3_optical_health_snapshot(nocti_v3_optical_t *optical,
                                      nocti_v3_optical_health_snapshot_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (optical == NULL || optical->runtime == NULL) return;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
#define CPY(name, src) out->name = __atomic_load_n(&r->src, __ATOMIC_ACQUIRE)
    CPY(led_errors, led_errors);
    CPY(stale_i2c_completion, stale_i2c_completion);
    CPY(sched_missed, sched_missed);
    CPY(sched_abort, sched_abort);
    CPY(scheduler_event_overflow, scheduler_event_overflow);
    CPY(frames_total, frames_total);
    CPY(frames_valid, frames_valid);
    CPY(partial_frames, partial_frames);
    CPY(invalid_frames, invalid_frames);
    CPY(slots_total, slots_total);
    CPY(underk_on, underk_on);
    CPY(underk_off, underk_off);
    CPY(subtarget_on, subtarget_on);
    CPY(subtarget_off, subtarget_off);
    CPY(raw_stable_on, raw_stable_on);
    CPY(raw_stable_i_led_off, raw_stable_off);
    CPY(raw_sample_rejected_transition, raw_transition);
    CPY(raw_sample_rejected_settle, raw_settle);
    CPY(raw_sample_rejected_guard, raw_guard);
    CPY(raw_sample_rejected_stale_epoch, raw_stale_epoch);
    CPY(raw_sample_rejected_ambiguous, raw_ambiguous);
    CPY(slot_timing_ring_overflow, timing_ring_overflow);
    CPY(sample_scratch_overflow, sample_scratch_overflow);
    CPY(completed_frame_ring_overflow, frame_ring_overflow);
    CPY(completed_frame_ring_depth_max, frame_ring_depth_max);
    CPY(completed_frames_consumed, completed_frames_consumed);
    CPY(network_drop, network_drop);
    CPY(stream_errors, stream_errors);
    CPY(network_frames_sent, network_frames_sent);
    CPY(network_records_sent, network_records_sent);
    CPY(scheduler_lateness_max_us, scheduler_lateness_max_us);
    CPY(led_on_stable_min_us, led_on_stable_min_us);
    CPY(led_off_stable_min_us, led_off_stable_min_us);
    CPY(scheduler_stack_watermark_bytes, scheduler_stack_watermark);
    CPY(processing_stack_watermark_bytes, processing_stack_watermark);
    CPY(frame_sink_stack_watermark_bytes, frame_sink_stack_watermark);
#undef CPY
    const uint32_t min_on = __atomic_load_n(&r->min_n_on, __ATOMIC_ACQUIRE);
    const uint32_t min_off = __atomic_load_n(&r->min_n_off, __ATOMIC_ACQUIRE);
    out->min_n_on = min_on == UINT32_MAX ? 0u : min_on;
    out->min_n_off = min_off == UINT32_MAX ? 0u : min_off;
    out->completed_frame_ring_depth = frame_ring_depth(r);
    out->session_epoch = r->session_epoch;
    out->config_generation = r->config_generation;
    out->frame_index = r->frame_index;
    out->slot_epoch = r->slot_epoch;
    out->slot_index = r->slot_index;
    out->scheduler_task_core = r->scheduler_core;
    out->processing_task_core = r->processing_core;
    out->frame_sink_task_core = r->frame_sink_core;
    out->frame_consumer_task_core = __atomic_load_n(&r->external_frame_consumer_core, __ATOMIC_ACQUIRE);
    out->scan_active = r->scan_active;
    out->timing_history_valid = r->history_valid;
    out->completed_frame_available = __atomic_load_n(
        &r->latest_frame_valid, __ATOMIC_ACQUIRE) &&
        r->latest_frame.session_epoch == r->session_epoch &&
        r->latest_frame.config_generation == r->config_generation;
    out->slot_state = r->slot_state;
    out->lifecycle = r->lifecycle;
    out->last_reason = r->last_reason;
    out->last_error = r->last_error;
}

esp_err_t nocti_v3_optical_reset_counters(nocti_v3_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL)
        return ESP_ERR_INVALID_STATE;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (r->lifecycle != NOCTI_V3_STATE_STOPPED ||
        nocti_v3_adc_running(r->adc)) return ESP_ERR_INVALID_STATE;
#define ZERO(name) __atomic_store_n(&r->name, 0u, __ATOMIC_RELEASE)
    ZERO(led_errors); ZERO(stale_i2c_completion);
    ZERO(consecutive_stale_completion); ZERO(consecutive_scheduler_faults);
    ZERO(sched_missed); ZERO(sched_abort); ZERO(scheduler_event_overflow);
    ZERO(scheduler_lateness_max_us); ZERO(led_on_stable_min_us);
    ZERO(led_off_stable_min_us); ZERO(frames_total); ZERO(frames_valid);
    ZERO(partial_frames); ZERO(invalid_frames); ZERO(slots_total);
    ZERO(underk_on); ZERO(underk_off); ZERO(subtarget_on); ZERO(subtarget_off);
    ZERO(raw_stable_on); ZERO(raw_stable_off); ZERO(raw_transition);
    ZERO(raw_settle); ZERO(raw_guard); ZERO(raw_stale_epoch);
    ZERO(raw_ambiguous); ZERO(timing_ring_overflow);
    ZERO(sample_scratch_overflow); ZERO(frame_ring_overflow);
    ZERO(frame_ring_depth_max); ZERO(completed_frames_consumed);
    ZERO(network_drop); ZERO(stream_errors); ZERO(network_frames_sent); ZERO(network_records_sent);
#undef ZERO
    __atomic_store_n(&r->min_n_on, UINT32_MAX, __ATOMIC_RELEASE);
    __atomic_store_n(&r->min_n_off, UINT32_MAX, __ATOMIC_RELEASE);
    return ESP_OK;
}

esp_err_t nocti_v3_optical_format_slot_timing(nocti_v3_optical_t *optical,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *out_length)
{
    if (optical == NULL || optical->runtime == NULL || buffer == NULL ||
        capacity == 0u || out_length == NULL) return ESP_ERR_INVALID_ARG;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    size_t used = 0u;
    int n = snprintf(buffer, capacity,
        "FNIRS SLOT_TIMING session=%lu config_gen=%lu frame=%lu valid=%u units=us intervals=half_open commit_is_conservative_completion representative=mean_latest_contiguous_K\n",
        (unsigned long)r->session_epoch,
        (unsigned long)r->config_generation,
        (unsigned long)r->frame_index,
        r->history_valid ? 1u : 0u);
    if (n < 0) return ESP_FAIL;
    used = (size_t)n < capacity ? (size_t)n : capacity - 1u;
    for (uint8_t i = 0u; i < NOCTI_V3_SLOT_COUNT &&
         used < capacity - 1u; ++i) {
        nocti_v3_slot_timing_t t;
        uint32_t before, after;
        do {
            before = __atomic_load_n(&r->history_generation,
                                     __ATOMIC_ACQUIRE);
            if (before & 1u) continue;
            t = r->history[i];
            after = __atomic_load_n(&r->history_generation,
                                    __ATOMIC_ACQUIRE);
        } while (before != after || (after & 1u));
        n = snprintf(buffer + used, capacity - used,
            "S%02u src=%u geom=%u wl=%u pca=%u start=%llu off_nom=%llu end=%llu on_req=%llu on_submit=%llu on_done=%llu on_stable=%llu off_req=%llu off_submit=%llu off_done=%llu off_stable=%llu guard=%llu on_win=%lu off_win=%lu valid=%u reason=%s err=%ld\n",
            (unsigned)i, (unsigned)t.descriptor.source_id,
            (unsigned)t.descriptor.geometry_id,
            (unsigned)t.descriptor.wavelength_nm,
            (unsigned)t.descriptor.pca_output,
            (unsigned long long)t.nominal_slot_start_ts,
            (unsigned long long)t.nominal_led_off_ts,
            (unsigned long long)t.nominal_slot_end_ts,
            (unsigned long long)t.led_request_ts,
            (unsigned long long)t.i2c_submit_ts,
            (unsigned long long)t.i2c_complete_ts,
            (unsigned long long)t.led_stable_start_ts,
            (unsigned long long)t.led_off_request_ts,
            (unsigned long long)t.led_off_submit_ts,
            (unsigned long long)t.led_off_complete_ts,
            (unsigned long long)t.led_off_stable_start_ts,
            (unsigned long long)t.slot_guard_start_ts,
            (unsigned long)t.on_stable_us,
            (unsigned long)t.off_stable_us,
            t.valid_timing ? 1u : 0u,
            nocti_v3_optical_reason_name(t.reason), (long)t.error);
        if (n < 0) return ESP_FAIL;
        if ((size_t)n >= capacity - used) {
            used = capacity - 1u;
            break;
        }
        used += (size_t)n;
    }
    buffer[used] = '\0';
    *out_length = used;
    return used == capacity - 1u ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

bool nocti_v3_optical_latest_frame_snapshot(
    nocti_v3_optical_t *optical,
    nocti_v3_completed_frame_t *out_frame)
{
    if (optical == NULL || optical->runtime == NULL || out_frame == NULL)
        return false;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (!__atomic_load_n(&r->latest_frame_valid, __ATOMIC_ACQUIRE))
        return false;
    uint32_t before, after;
    do {
        before = __atomic_load_n(&r->latest_frame_generation,
                                 __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        *out_frame = r->latest_frame;
        after = __atomic_load_n(&r->latest_frame_generation,
                                __ATOMIC_ACQUIRE);
    } while (before != after || (after & 1u));
    return out_frame->session_epoch == __atomic_load_n(
               &r->session_epoch, __ATOMIC_ACQUIRE) &&
           out_frame->config_generation == __atomic_load_n(
               &r->config_generation, __ATOMIC_ACQUIRE);
}

esp_err_t nocti_v3_optical_bind_frame_consumer(nocti_v3_optical_t *optical,
                                                TaskHandle_t task)
{
    if (optical == NULL || optical->runtime == NULL || task == NULL)
        return ESP_ERR_INVALID_ARG;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (r->lifecycle != NOCTI_V3_STATE_STOPPED) return ESP_ERR_INVALID_STATE;
    __atomic_store_n(&r->external_frame_consumer, task, __ATOMIC_RELEASE);
    __atomic_store_n(&r->external_frame_consumer_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&r->external_frame_consumer_bound, true, __ATOMIC_RELEASE);
    return ESP_OK;
}

bool nocti_v3_optical_take_completed_frame(nocti_v3_optical_t *optical,
                                            nocti_v3_completed_frame_t *out_frame)
{
    if (optical == NULL || optical->runtime == NULL || out_frame == NULL)
        return false;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (!__atomic_load_n(&r->external_frame_consumer_bound, __ATOMIC_ACQUIRE))
        return false;
    __atomic_store_n(&r->external_frame_consumer_core, xPortGetCoreID(),
                     __ATOMIC_RELEASE);
    for (;;) {
        if (!frame_pop(r, out_frame)) return false;
        (void)__atomic_fetch_add(&r->completed_frames_consumed, 1u,
                                 __ATOMIC_RELAXED);
        if (out_frame->session_epoch == __atomic_load_n(
                &r->session_epoch, __ATOMIC_ACQUIRE) &&
            out_frame->config_generation == __atomic_load_n(
                &r->config_generation, __ATOMIC_ACQUIRE)) break;
        (void)__atomic_fetch_add(&r->raw_stale_epoch, 1u, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&r->latest_frame_generation, 1u, __ATOMIC_ACQ_REL);
    r->latest_frame = *out_frame;
    __atomic_store_n(&r->latest_frame_valid, true, __ATOMIC_RELEASE);
    __atomic_add_fetch(&r->latest_frame_generation, 1u, __ATOMIC_RELEASE);
    return true;
}

uint32_t nocti_v3_optical_flush_completed_frames(nocti_v3_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return 0u;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (!__atomic_load_n(&r->external_frame_consumer_bound, __ATOMIC_ACQUIRE))
        return 0u;
    const uint32_t head = __atomic_load_n(&r->frame_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&r->frame_tail, __ATOMIC_RELAXED);
    __atomic_store_n(&r->frame_tail, head, __ATOMIC_RELEASE);
    const uint32_t count = head - tail;
    if (count != 0u)
        (void)__atomic_fetch_add(&r->completed_frames_consumed, count,
                                 __ATOMIC_RELAXED);
    return count;
}

void nocti_v3_optical_record_network_result(nocti_v3_optical_t *optical,
                                            bool frame_sent,
                                            uint32_t records_sent,
                                            bool stream_error)
{
    if (optical == NULL || optical->runtime == NULL) return;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (frame_sent) (void)__atomic_fetch_add(&r->network_frames_sent, 1u, __ATOMIC_RELAXED);
    else (void)__atomic_fetch_add(&r->network_drop, 1u, __ATOMIC_RELAXED);
    if (records_sent != 0u) (void)__atomic_fetch_add(&r->network_records_sent, records_sent, __ATOMIC_RELAXED);
    if (stream_error) (void)__atomic_fetch_add(&r->stream_errors, 1u, __ATOMIC_RELAXED);
}

void nocti_v3_optical_record_stream_error(nocti_v3_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    (void)__atomic_fetch_add(&r->stream_errors, 1u, __ATOMIC_RELAXED);
}

esp_err_t nocti_v3_optical_set_current_profile(nocti_v3_optical_t *optical,
                                                uint16_t current_735_ma,
                                                uint16_t current_850_ma)
{
    if (optical == NULL || optical->runtime == NULL) return ESP_ERR_INVALID_ARG;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (r->lifecycle != NOCTI_V3_STATE_STOPPED || nocti_v3_adc_running(r->adc))
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = nocti_v3_led_set_current_profile(r->led, current_735_ma, current_850_ma);
    if (err != ESP_OK) return err;
    for (uint8_t i = 0u; i < NOCTI_V3_SLOT_COUNT; ++i)
        s_descriptors[i].current_ma = (i & 1u) == 0u ? current_735_ma : current_850_ma;
    return ESP_OK;
}

void nocti_v3_optical_get_current_profile(nocti_v3_optical_t *optical,
                                           uint16_t *current_735_ma,
                                           uint16_t *current_850_ma)
{
    if (optical == NULL || optical->runtime == NULL) return;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    nocti_v3_led_get_current_profile(r->led, current_735_ma, current_850_ma);
}

const nocti_v3_slot_descriptor_t *nocti_v3_optical_slot_descriptors(void)
{
    return s_descriptors;
}

const char *nocti_v3_slot_state_name(nocti_v3_slot_state_t state)
{
    switch (state) {
        case NOCTI_V3_SLOT_IDLE: return "SLOT_IDLE";
        case NOCTI_V3_SLOT_LED_ON_PENDING: return "LED_ON_PENDING";
        case NOCTI_V3_SLOT_LED_ON_COMMITTED: return "LED_ON_COMMITTED";
        case NOCTI_V3_SLOT_LED_ON_SETTLING: return "LED_ON_SETTLING";
        case NOCTI_V3_SLOT_LED_ON_COLLECT: return "LED_ON_COLLECT";
        case NOCTI_V3_SLOT_LED_OFF_PENDING: return "LED_OFF_PENDING";
        case NOCTI_V3_SLOT_LED_OFF_COMMITTED: return "LED_OFF_COMMITTED";
        case NOCTI_V3_SLOT_LED_OFF_SETTLING: return "LED_OFF_SETTLING";
        case NOCTI_V3_SLOT_LED_OFF_COLLECT: return "LED_OFF_COLLECT";
        case NOCTI_V3_SLOT_END_GUARD: return "SLOT_END_GUARD";
        case NOCTI_V3_SLOT_FINALIZE: return "SLOT_FINALIZE";
        case NOCTI_V3_SLOT_INVALID: return "SLOT_INVALID";
        case NOCTI_V3_SLOT_LED_RECOVERY: return "LED_RECOVERY";
        default: return "UNKNOWN";
    }
}

const char *nocti_v3_slot_quality_name(nocti_v3_slot_quality_t quality)
{
    switch (quality) {
        case NOCTI_V3_SLOT_QUALITY_INVALID: return "INVALID";
        case NOCTI_V3_SLOT_QUALITY_PARTIAL: return "PARTIAL";
        case NOCTI_V3_SLOT_QUALITY_VALID_MINIMUM: return "VALID_MINIMUM";
        case NOCTI_V3_SLOT_QUALITY_VALID_TARGET: return "VALID_TARGET";
        default: return "UNKNOWN";
    }
}

const char *nocti_v3_frame_quality_name(nocti_v3_frame_quality_t quality)
{
    switch (quality) {
        case NOCTI_V3_FRAME_QUALITY_INVALID: return "INVALID";
        case NOCTI_V3_FRAME_QUALITY_PARTIAL: return "PARTIAL";
        case NOCTI_V3_FRAME_QUALITY_VALID: return "VALID";
        default: return "UNKNOWN";
    }
}

const char *nocti_v3_optical_reason_name(nocti_v3_optical_reason_t reason)
{
    switch (reason) {
        case NOCTI_V3_REASON_NONE: return "NONE";
        case NOCTI_V3_REASON_UNDER_K: return "UNDER_K";
        case NOCTI_V3_REASON_TIMING_INVALID: return "TIMING_INVALID";
        case NOCTI_V3_REASON_SAMPLE_INTEGRITY: return "SAMPLE_INTEGRITY";
        case NOCTI_V3_REASON_SAMPLE_BUFFER_OVERFLOW: return "SAMPLE_BUFFER_OVERFLOW";
        case NOCTI_V3_REASON_MISSING_SLOT: return "MISSING_SLOT";
        case NOCTI_V3_REASON_TIMING_RING_OVERFLOW: return "TIMING_RING_OVERFLOW";
        case NOCTI_V3_REASON_FRAME_RING_OVERFLOW: return "FRAME_RING_OVERFLOW";
        case NOCTI_V3_REASON_STALE_EPOCH: return "STALE_EPOCH";
        case NOCTI_V3_REASON_LED_NACK: return "LED_NACK";
        case NOCTI_V3_REASON_LED_TIMEOUT: return "LED_TIMEOUT";
        case NOCTI_V3_REASON_LED_RECOVERY_FAILED: return "LED_RECOVERY_FAILED";
        case NOCTI_V3_REASON_SCHEDULER_MISS: return "SCHEDULER_MISS";
        case NOCTI_V3_REASON_ADC_FAULT: return "ADC_FAULT";
        case NOCTI_V3_REASON_STOP_REQUEST: return "STOP_REQUEST";
        case NOCTI_V3_REASON_INTERNAL: return "INTERNAL";
        default: return "UNKNOWN";
    }
}
