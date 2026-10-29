#include "nocti_v4_scheduler.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nocti_v4_common.h"

#define SCHEDULER_RING_MASK (NOCTI_V4_SCHEDULER_EVENT_RING_CAPACITY - 1u)
#define FINALIZED_RING_MASK (NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY - 1u)
#define SCHEDULER_COMMAND_QUEUE_CAPACITY 2u
#define SCHEDULER_COMMAND_TIMEOUT_MS 1000u
#define SCHEDULER_STACK_WORDS \
    (NOCTI_V4_SCHEDULER_STACK_BYTES / sizeof(StackType_t))

_Static_assert((NOCTI_V4_SCHEDULER_STACK_BYTES % sizeof(StackType_t)) == 0u,
               "scheduler stack byte count must align to StackType_t");

_Static_assert((NOCTI_V4_SCHEDULER_EVENT_RING_CAPACITY &
                (NOCTI_V4_SCHEDULER_EVENT_RING_CAPACITY - 1u)) == 0u,
               "scheduler deadline ring must be a power of two");
_Static_assert((NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY &
                (NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY - 1u)) == 0u,
               "finalized-slot ring must be a power of two");
/* Compile-time proof for the unchanged 10 Hz baseline. Runtime Fs setter
 * accepts only exact 125 us slot grids at equal or longer periods. */
_Static_assert(NOCTI_V4_SLOT_COUNT * 5000u == 100000u, "V4 baseline geometry");
_Static_assert(2200u < 5000u && 2500u < 5000u, "V4 OFF phases inside slot");
_Static_assert(NOCTI_V4_SLOT_END_GUARD_US < 5000u, "guard inside shortest slot");
static uint32_t s_slot_period_us = 5000u;
uint32_t IRAM_ATTR nocti_v4_scheduler_slot_period_us(void)
{
    return __atomic_load_n(&s_slot_period_us, __ATOMIC_ACQUIRE);
}

typedef enum {
    DEADLINE_SLOT_START = 0,
    DEADLINE_LED_OFF,
} deadline_phase_t;

typedef struct {
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint8_t slot_index;
    deadline_phase_t phase;
    uint64_t frame_epoch_us;
    uint64_t deadline_us;
    uint64_t callback_timestamp_us;
    int32_t callback_core;
} deadline_event_t;

typedef struct {
    bool valid;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint64_t frame_epoch_us;
} frame_descriptor_t;

typedef nocti_v4_slot_timing_t slot_record_t;

typedef enum {
    SCHEDULER_COMMAND_START = 0,
    SCHEDULER_COMMAND_STOP,
} scheduler_command_type_t;

typedef struct {
    scheduler_command_type_t type;
} scheduler_command_t;

typedef struct {
    nocti_v4_scheduler_t *public_scheduler;
    nocti_v4_time_t *timebase;
    nocti_v4_async_led_t *async_led;
    nocti_v4_health_t *health;
    TaskHandle_t task;
    QueueHandle_t command_queue;
    SemaphoreHandle_t command_lock;
    SemaphoreHandle_t command_done;
    StaticQueue_t command_queue_storage;
    StaticSemaphore_t command_lock_storage;
    StaticSemaphore_t command_done_storage;
    StaticTask_t task_storage;
    /* xTaskCreateStaticPinnedToCore() consumes a byte count in ESP-IDF.
     * StackType_t is uint8_t on the ESP32-S3 IDF 5.5.1 port; the conversion
     * keeps that byte-count contract explicit and portable. */
    StackType_t task_stack[SCHEDULER_STACK_WORDS];
    uint8_t command_queue_buffer[SCHEDULER_COMMAND_QUEUE_CAPACITY * sizeof(scheduler_command_t)];

    portMUX_TYPE state_lock;
    portMUX_TYPE timer_lock;
    volatile bool scan_active;
    volatile bool timer_armed;
    volatile bool event_overflow_pending;
    volatile nocti_v4_scan_lifecycle_t lifecycle;
    volatile esp_err_t command_result;
    volatile uint32_t deadline_head;
    volatile uint32_t deadline_tail;
    volatile uint32_t deadline_depth_max;
    volatile uint32_t configured_off_phase_us;
    volatile uint32_t active_off_phase_us;
    deadline_event_t deadline_ring[NOCTI_V4_SCHEDULER_EVENT_RING_CAPACITY];
    deadline_event_t next_deadline;
    TaskHandle_t optical_task;
    volatile uint32_t finalized_head;
    volatile uint32_t finalized_tail;
    nocti_v4_slot_timing_t finalized_ring[NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY];
    gptimer_alarm_config_t alarm_config;

    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    int32_t active_history_index;
    int32_t active_slot_index;
    int32_t last_history_index;
    int32_t last_slot_index;
    int32_t task_core;
    int32_t gptimer_callback_core;
    esp_err_t last_error;
    uint64_t lateness_sum_us;
    uint32_t lateness_count;
    uint64_t on_latency_sum_us;
    uint32_t on_latency_count;
    uint64_t off_latency_sum_us;
    uint32_t off_latency_count;
    uint64_t deferred_work_sum_us;
    uint32_t deferred_work_count;
    uint8_t source_to_channel[NOCTI_V4_SLOT_COUNT];
    frame_descriptor_t frames[NOCTI_V4_SCHEDULER_HISTORY_FRAMES];
    slot_record_t slots[NOCTI_V4_SCHEDULER_HISTORY_FRAMES][NOCTI_V4_SLOT_COUNT];
} scheduler_runtime_t;

static scheduler_runtime_t s_runtime;
static bool s_runtime_claimed;

static inline void IRAM_ATTR counter_add(uint32_t *counter, uint32_t amount)
{
    (void)__atomic_fetch_add(counter, amount, __ATOMIC_RELAXED);
}

static inline void counter_store(uint32_t *counter, uint32_t value)
{
    __atomic_store_n(counter, value, __ATOMIC_RELEASE);
}

static void update_min(uint32_t *counter, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while ((current == 0u || candidate < current) &&
           !__atomic_compare_exchange_n(counter, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void IRAM_ATTR update_max(uint32_t *counter, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(counter, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static uint32_t deadline_depth(const scheduler_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->deadline_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&runtime->deadline_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

static bool deadline_pop(scheduler_runtime_t *runtime, deadline_event_t *out_event)
{
    const uint32_t tail = __atomic_load_n(&runtime->deadline_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->deadline_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out_event = runtime->deadline_ring[tail & SCHEDULER_RING_MASK];
    __atomic_store_n(&runtime->deadline_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

/* Scheduler task producer -> V4-6 optical task consumer. This ring carries a
 * complete immutable timing copy, so optical processing never observes a
 * history entry after the scheduler has reused it for a later frame. */
static bool finalized_push(scheduler_runtime_t *runtime,
                           const nocti_v4_slot_timing_t *timing)
{
    const uint32_t head = __atomic_load_n(&runtime->finalized_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->finalized_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY) {
        counter_add(&runtime->health->optical_timing_ring_overflow, 1u);
        return false;
    }
    runtime->finalized_ring[head & FINALIZED_RING_MASK] = *timing;
    __atomic_store_n(&runtime->finalized_head, head + 1u, __ATOMIC_RELEASE);
    if (runtime->optical_task != NULL) {
        xTaskNotifyGive(runtime->optical_task);
    }
    return true;
}

static bool finalized_pop(scheduler_runtime_t *runtime,
                          nocti_v4_slot_timing_t *out_timing)
{
    const uint32_t tail = __atomic_load_n(&runtime->finalized_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->finalized_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out_timing = runtime->finalized_ring[tail & FINALIZED_RING_MASK];
    __atomic_store_n(&runtime->finalized_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static bool IRAM_ATTR deadline_push_from_isr(scheduler_runtime_t *runtime,
                                              const deadline_event_t *event)
{
    const uint32_t head = __atomic_load_n(&runtime->deadline_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->deadline_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V4_SCHEDULER_EVENT_RING_CAPACITY) {
        counter_add(&runtime->health->scheduler_event_overflow, 1u);
        __atomic_store_n(&runtime->event_overflow_pending, true, __ATOMIC_RELEASE);
        return false;
    }
    runtime->deadline_ring[head & SCHEDULER_RING_MASK] = *event;
    __atomic_store_n(&runtime->deadline_head, head + 1u, __ATOMIC_RELEASE);
    update_max((uint32_t *)&runtime->deadline_depth_max, head - tail + 1u);
    return true;
}

static deadline_event_t IRAM_ATTR next_absolute_deadline(
    const scheduler_runtime_t *runtime, const deadline_event_t *current)
{
    deadline_event_t next = *current;
    next.callback_timestamp_us = 0u;
    next.callback_core = -1;
    if (current->phase == DEADLINE_SLOT_START) {
        next.phase = DEADLINE_LED_OFF;
        next.deadline_us = current->frame_epoch_us +
                           (uint64_t)current->slot_index * NOCTI_V4_SLOT_PERIOD_US +
                           runtime->active_off_phase_us;
        return next;
    }
    next.phase = DEADLINE_SLOT_START;
    if (current->slot_index + 1u == NOCTI_V4_SLOT_COUNT) {
        next.slot_index = 0u;
        ++next.frame_sequence;
        next.frame_epoch_us = current->frame_epoch_us + NOCTI_V4_FRAME_PERIOD_US;
        ++next.slot_epoch;
    } else {
        ++next.slot_index;
    }
    next.deadline_us = next.frame_epoch_us +
                       (uint64_t)next.slot_index * NOCTI_V4_SLOT_PERIOD_US;
    return next;
}

static bool IRAM_ATTR scheduler_alarm_handler(gptimer_handle_t timer,
                                               const gptimer_alarm_event_data_t *event,
                                               void *user_data)
{
    (void)event;
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)user_data;
    if (runtime == NULL) {
        return false;
    }
    BaseType_t higher_priority_woken = pdFALSE;
    deadline_event_t current = {0};
    bool active = false;
    portENTER_CRITICAL_ISR(&runtime->timer_lock);
    active = runtime->scan_active;
    if (active) {
        current = runtime->next_deadline;
    }
    portEXIT_CRITICAL_ISR(&runtime->timer_lock);
    if (!active) {
        return false;
    }
    (void)nocti_v4_time_now_us_isr(runtime->timebase, &current.callback_timestamp_us);
    current.callback_core = xPortGetCoreID();
    __atomic_store_n(&runtime->gptimer_callback_core, current.callback_core,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->health->scheduler_gptimer_callback_core,
                     current.callback_core, __ATOMIC_RELEASE);
    if (!deadline_push_from_isr(runtime, &current)) {
        portENTER_CRITICAL_ISR(&runtime->timer_lock);
        runtime->scan_active = false;
        runtime->timer_armed = false;
        portEXIT_CRITICAL_ISR(&runtime->timer_lock);
        (void)gptimer_set_alarm_action(timer, NULL);
    } else {
        const deadline_event_t next = next_absolute_deadline(runtime, &current);
        bool rearm = false;
        portENTER_CRITICAL_ISR(&runtime->timer_lock);
        /* SCAN_STOP can race this alarm handler on the other core.  It first
         * clears scan_active under this same lock, so never publish another
         * future deadline after it has taken ownership of the stop. */
        if (runtime->scan_active) {
            runtime->next_deadline = next;
            runtime->alarm_config.alarm_count = next.deadline_us;
            runtime->alarm_config.reload_count = 0u;
            runtime->alarm_config.flags.auto_reload_on_alarm = false;
            rearm = true;
        }
        portEXIT_CRITICAL_ISR(&runtime->timer_lock);
        /* ESP-IDF 5.5.1 explicitly permits this from the alarm ISR. The
         * target is always derived from frame_epoch, never callback time. */
        if (rearm) {
            (void)gptimer_set_alarm_action(timer, &runtime->alarm_config);
            /* Close the small interval where SCAN_STOP clears scan_active
             * after the lock is released but before the ISR arms the timer. */
            if (!__atomic_load_n(&runtime->scan_active, __ATOMIC_ACQUIRE)) {
                (void)gptimer_set_alarm_action(timer, NULL);
            }
        }
    }
    if (runtime->task != NULL) {
        vTaskNotifyGiveFromISR(runtime->task, &higher_priority_woken);
    }
    return higher_priority_woken == pdTRUE;
}

static esp_err_t disarm_deadlines(scheduler_runtime_t *runtime)
{
    portENTER_CRITICAL(&runtime->timer_lock);
    runtime->scan_active = false;
    runtime->timer_armed = false;
    portEXIT_CRITICAL(&runtime->timer_lock);
    return gptimer_set_alarm_action(runtime->timebase->timer, NULL);
}

static esp_err_t arm_first_deadline(scheduler_runtime_t *runtime,
                                    uint32_t session_epoch,
                                    uint32_t frame_sequence,
                                    uint32_t slot_epoch,
                                    uint64_t frame_epoch_us)
{
    const deadline_event_t first = {
        .session_epoch = session_epoch,
        .frame_sequence = frame_sequence,
        .slot_epoch = slot_epoch,
        .slot_index = 0u,
        .phase = DEADLINE_SLOT_START,
        .frame_epoch_us = frame_epoch_us,
        .deadline_us = frame_epoch_us,
        .callback_core = -1,
    };
    portENTER_CRITICAL(&runtime->timer_lock);
    runtime->next_deadline = first;
    runtime->alarm_config.alarm_count = first.deadline_us;
    runtime->alarm_config.reload_count = 0u;
    runtime->alarm_config.flags.auto_reload_on_alarm = false;
    runtime->scan_active = true;
    runtime->timer_armed = true;
    portEXIT_CRITICAL(&runtime->timer_lock);
    const esp_err_t err = gptimer_set_alarm_action(runtime->timebase->timer,
                                                    &runtime->alarm_config);
    if (err != ESP_OK) {
        (void)disarm_deadlines(runtime);
    }
    return err;
}

static slot_record_t *slot_at(scheduler_runtime_t *runtime, int32_t history_index,
                              int32_t slot_index)
{
    if (history_index < 0 || history_index >= (int32_t)NOCTI_V4_SCHEDULER_HISTORY_FRAMES ||
        slot_index < 0 || slot_index >= (int32_t)NOCTI_V4_SLOT_COUNT) {
        return NULL;
    }
    return &runtime->slots[history_index][slot_index];
}

static void begin_frame(scheduler_runtime_t *runtime, uint32_t frame_sequence,
                        uint32_t session_epoch, uint64_t frame_epoch_us)
{
    const uint32_t history_index = frame_sequence % NOCTI_V4_SCHEDULER_HISTORY_FRAMES;
    portENTER_CRITICAL(&runtime->state_lock);
    runtime->frames[history_index] = (frame_descriptor_t){
        .valid = true,
        .session_epoch = session_epoch,
        .frame_sequence = frame_sequence,
        .frame_epoch_us = frame_epoch_us,
    };
    runtime->active_history_index = (int32_t)history_index;
    portEXIT_CRITICAL(&runtime->state_lock);
}

static slot_record_t *init_slot_record(scheduler_runtime_t *runtime,
                                       const deadline_event_t *event,
                                       uint64_t handling_us)
{
    slot_record_t *record = slot_at(runtime, runtime->active_history_index,
                                    event->slot_index);
    if (record == NULL) {
        return NULL;
    }
    portENTER_CRITICAL(&runtime->state_lock);
    *record = (slot_record_t){
        .valid = true,
        .session_epoch = event->session_epoch,
        .frame_sequence = event->frame_sequence,
        .slot_epoch = event->slot_epoch,
        .slot_index = event->slot_index,
        .logical_source = event->slot_index,
        .pca_channel = runtime->source_to_channel[event->slot_index],
        .state = NOCTI_V4_SLOT_LED_ON_PENDING,
        .nominal_slot_start_ts = event->frame_epoch_us +
                                 (uint64_t)event->slot_index * NOCTI_V4_SLOT_PERIOD_US,
        .scheduler_on_deadline_ts = event->deadline_us,
        .scheduler_on_handling_ts = handling_us,
        .nominal_led_off_ts = event->frame_epoch_us +
                              (uint64_t)event->slot_index * NOCTI_V4_SLOT_PERIOD_US +
                              runtime->active_off_phase_us,
        .slot_guard_start_ts = event->frame_epoch_us +
                               (uint64_t)(event->slot_index + 1u) * NOCTI_V4_SLOT_PERIOD_US -
                               NOCTI_V4_SLOT_END_GUARD_US,
        .nominal_slot_end_ts = event->frame_epoch_us +
                               (uint64_t)(event->slot_index + 1u) * NOCTI_V4_SLOT_PERIOD_US,
    };
    runtime->active_slot_index = event->slot_index;
    portEXIT_CRITICAL(&runtime->state_lock);
    counter_add(&runtime->health->slots_started, 1u);
    return record;
}

static void record_latency(nocti_v4_health_t *health, bool on, uint32_t latency_us,
                           uint64_t *sum, uint32_t *count)
{
    *sum += latency_us;
    ++*count;
    if (on) {
        update_min(&health->led_on_latency_min_us, latency_us);
        update_max(&health->led_on_latency_max_us, latency_us);
        counter_store(&health->led_on_latency_avg_us, (uint32_t)(*sum / *count));
    } else {
        update_min(&health->led_off_latency_min_us, latency_us);
        update_max(&health->led_off_latency_max_us, latency_us);
        counter_store(&health->led_off_latency_avg_us, (uint32_t)(*sum / *count));
    }
}

static esp_err_t submit_on(scheduler_runtime_t *runtime, slot_record_t *record,
                           uint64_t deadline_us, uint64_t request_us)
{
    counter_add(&runtime->health->led_on_submitted, 1u);
    return nocti_v4_async_led_on_burst_scheduled(
        runtime->async_led, record->logical_source, record->session_epoch,
        record->frame_sequence, record->slot_epoch, record->slot_index,
        deadline_us, request_us);
}

static esp_err_t submit_off(scheduler_runtime_t *runtime, slot_record_t *record,
                            uint64_t deadline_us, uint64_t handling_us,
                            uint64_t request_us)
{
    portENTER_CRITICAL(&runtime->state_lock);
    record->scheduler_off_handling_ts = handling_us;
    record->led_off_request_ts = request_us;
    record->led_off_transition_boundary_ts = request_us;
    record->state = NOCTI_V4_SLOT_LED_OFF_PENDING;
    portEXIT_CRITICAL(&runtime->state_lock);
    counter_add(&runtime->health->led_off_submitted, 1u);
    return nocti_v4_async_led_off_single_scheduled(
        runtime->async_led, record->logical_source, record->session_epoch,
        record->frame_sequence, record->slot_epoch, record->slot_index,
        deadline_us, request_us);
}

static void record_candidate_histogram(nocti_v4_health_t *health, bool on,
                                       uint32_t candidates)
{
    if (on) {
        if (candidates < NOCTI_V4_K_ON_MINIMUM) counter_add(&health->candidate_on_0_to_7, 1u);
        else if (candidates < NOCTI_V4_K_ON_TARGET) counter_add(&health->candidate_on_8_to_9, 1u);
        else if (candidates <= 12u) counter_add(&health->candidate_on_10_to_12, 1u);
        else counter_add(&health->candidate_on_gt_12, 1u);
    } else {
        if (candidates < NOCTI_V4_K_OFF_MINIMUM) counter_add(&health->candidate_off_0_to_7, 1u);
        else if (candidates < NOCTI_V4_K_OFF_TARGET) counter_add(&health->candidate_off_8_to_9, 1u);
        else if (candidates <= 12u) counter_add(&health->candidate_off_10_to_12, 1u);
        else counter_add(&health->candidate_off_gt_12, 1u);
    }
}

static void finalize_slot(scheduler_runtime_t *runtime, slot_record_t *record,
                          uint64_t finalize_us, bool runtime_invalid,
                          bool aborted_by_stop)
{
    if (record == NULL || !record->valid || record->finalized) {
        return;
    }
    portENTER_CRITICAL(&runtime->state_lock);
    record->actual_finalize_ts = finalize_us;
    record->runtime_invalid = runtime_invalid;
    record->aborted_by_stop = aborted_by_stop;
    record->state = runtime_invalid || aborted_by_stop ? NOCTI_V4_SLOT_INVALID :
                                                       NOCTI_V4_SLOT_FINALIZE;
    record->finalized = true;
    runtime->last_history_index = runtime->active_history_index;
    runtime->last_slot_index = record->slot_index;
    portEXIT_CRITICAL(&runtime->state_lock);
    const nocti_v4_slot_timing_t timing = *record;
    if (!finalized_push(runtime, &timing)) {
        /* Loss of an immutable timing record is a V4-6 data-integrity event.
         * It is counted explicitly; recovery ownership remains out of this
         * normal scheduler path. */
        counter_add(&runtime->health->scan_session_faults, 1u);
    }
    if (runtime_invalid) {
        counter_add(&runtime->health->slots_invalid, 1u);
        return;
    }
    if (aborted_by_stop) {
        counter_add(&runtime->health->slots_aborted_by_stop, 1u);
        return;
    }
    counter_add(&runtime->health->slots_finalized, 1u);
    /* V4-5 used a diagnostic observer to increment these fields. In V4-6 the
     * optical task is the sole classifier/raw consumer, so it owns the same
     * counters after constructing the representative value. */
    if (runtime->optical_task != NULL) {
        if (record->slot_index + 1u == NOCTI_V4_SLOT_COUNT) {
            counter_add(&runtime->health->frames_completed, 1u);
        }
        return;
    }
    const uint32_t candidate_on = __atomic_load_n(&record->candidate_on, __ATOMIC_ACQUIRE);
    const uint32_t candidate_off = __atomic_load_n(&record->candidate_off, __ATOMIC_ACQUIRE);
    update_min(&runtime->health->candidate_min_on, candidate_on);
    update_min(&runtime->health->candidate_min_off, candidate_off);
    if (candidate_on < NOCTI_V4_K_ON_MINIMUM) {
        counter_add(&runtime->health->candidate_under8_on, 1u);
    } else if (candidate_on < NOCTI_V4_K_ON_TARGET) {
        counter_add(&runtime->health->candidate_under10_on, 1u);
    }
    if (candidate_off < NOCTI_V4_K_OFF_MINIMUM) {
        counter_add(&runtime->health->candidate_under8_off, 1u);
    } else if (candidate_off < NOCTI_V4_K_OFF_TARGET) {
        counter_add(&runtime->health->candidate_under10_off, 1u);
    }
    record_candidate_histogram(runtime->health, true, candidate_on);
    record_candidate_histogram(runtime->health, false, candidate_off);
    if (record->slot_index + 1u == NOCTI_V4_SLOT_COUNT) {
        counter_add(&runtime->health->frames_completed, 1u);
    }
}

static void fault_scan(scheduler_runtime_t *runtime, esp_err_t error)
{
    (void)disarm_deadlines(runtime);
    slot_record_t *record = slot_at(runtime, runtime->active_history_index,
                                    runtime->active_slot_index);
    uint64_t now_us = 0u;
    (void)nocti_v4_time_now_us(runtime->timebase, &now_us);
    finalize_slot(runtime, record, now_us, true, false);
    runtime->last_error = error;
    __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_FAULTED, __ATOMIC_RELEASE);
    counter_add(&runtime->health->scan_session_faults, 1u);
    /* This is a bounded safe-off request, not PCA reset/reinitialization in the
     * normal slot path. The async layer asserts RESET# itself on serious I2C failure. */
    (void)nocti_v4_async_led_all_off_burst(runtime->async_led);
}

static void process_led_completion(
    scheduler_runtime_t *runtime,
    const nocti_v4_async_led_completion_t *completion)
{
    const nocti_v4_scan_lifecycle_t lifecycle = __atomic_load_n(
        &runtime->lifecycle, __ATOMIC_ACQUIRE);
    /* A user STOP can overtake an in-flight edge. stop_internal() owns the
     * strong ALL_OFF transaction and marks the active slot aborted; the late
     * normal-edge record is therefore consumed but is not a stale runtime
     * fault. */
    if (lifecycle == NOCTI_V4_SCAN_STOPPING ||
        lifecycle == NOCTI_V4_SCAN_STOPPED) {
        return;
    }
    if (lifecycle != NOCTI_V4_SCAN_RUNNING || completion == NULL) {
        return;
    }
    if (completion->result != ESP_OK) {
        if (completion->result == ESP_ERR_TIMEOUT) {
            counter_add(&runtime->health->led_completion_timeout, 1u);
        }
        fault_scan(runtime, completion->result);
        return;
    }
    nocti_v4_async_led_status_t status;
    nocti_v4_async_led_get_status(runtime->async_led, &status);
    slot_record_t *record = slot_at(
        runtime,
        (int32_t)(completion->frame_sequence %
                  NOCTI_V4_SCHEDULER_HISTORY_FRAMES),
        completion->slot_index);
    const bool common_match = completion->command_id != 0u &&
        completion->generation == status.generation &&
        completion->session_epoch == runtime->session_epoch &&
        record != NULL && record->valid && !record->finalized &&
        record->session_epoch == completion->session_epoch &&
        record->frame_sequence == completion->frame_sequence &&
        record->slot_epoch == completion->slot_epoch &&
        record->slot_index == completion->slot_index &&
        record->logical_source == completion->source &&
        completion->submit_timestamp_us != 0u &&
        completion->complete_timestamp_us >= completion->submit_timestamp_us;
    if (!common_match) {
        counter_add(&runtime->health->stale_i2c_completion, 1u);
        fault_scan(runtime, ESP_ERR_INVALID_STATE);
        return;
    }
    const uint32_t latency_us = (uint32_t)(
        completion->complete_timestamp_us - completion->submit_timestamp_us);
    if (completion->payload_type == NOCTI_V4_ASYNC_PAYLOAD_BURST_ON &&
        completion->payload_length == 7u &&
        record->state == NOCTI_V4_SLOT_LED_ON_PENDING) {
        portENTER_CRITICAL(&runtime->state_lock);
        record->led_on_command_id = completion->command_id;
        record->led_on_queue_ts = completion->queue_timestamp_us;
        record->led_on_dequeue_ts = completion->dequeue_timestamp_us;
        record->led_on_submit_ts = completion->submit_timestamp_us;
        record->led_on_complete_ts = completion->complete_timestamp_us;
        record->led_on_commit_ts = completion->complete_timestamp_us;
        record->state = NOCTI_V4_SLOT_LED_ON_COMMITTED;
        record->state = NOCTI_V4_SLOT_LED_ON_SETTLING;
        record->led_on_stable_start_ts = completion->complete_timestamp_us +
                                         NOCTI_V4_LED_ON_SETTLE_US;
        record->state = NOCTI_V4_SLOT_LED_ON_STABLE;
        portEXIT_CRITICAL(&runtime->state_lock);
        record_latency(runtime->health, true, latency_us,
                       &runtime->on_latency_sum_us,
                       &runtime->on_latency_count);
        counter_add(&runtime->health->led_on_completed, 1u);
        return;
    }
    if (completion->payload_type == NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF &&
        completion->payload_length == 2u &&
        record->state == NOCTI_V4_SLOT_LED_OFF_PENDING) {
        portENTER_CRITICAL(&runtime->state_lock);
        record->led_off_command_id = completion->command_id;
        record->led_off_queue_ts = completion->queue_timestamp_us;
        record->led_off_dequeue_ts = completion->dequeue_timestamp_us;
        record->led_off_submit_ts = completion->submit_timestamp_us;
        record->led_off_complete_ts = completion->complete_timestamp_us;
        record->led_off_commit_ts = completion->complete_timestamp_us;
        record->state = NOCTI_V4_SLOT_LED_OFF_COMMITTED;
        record->state = NOCTI_V4_SLOT_LED_OFF_SETTLING;
        record->led_off_stable_start_ts = completion->complete_timestamp_us +
                                          NOCTI_V4_LED_OFF_SETTLE_US;
        record->state = NOCTI_V4_SLOT_LED_OFF_STABLE;
        portEXIT_CRITICAL(&runtime->state_lock);
        record_latency(runtime->health, false, latency_us,
                       &runtime->off_latency_sum_us,
                       &runtime->off_latency_count);
        counter_add(&runtime->health->led_off_completed, 1u);
        return;
    }
    counter_add(&runtime->health->stale_i2c_completion, 1u);
    fault_scan(runtime, ESP_ERR_INVALID_STATE);
}

static void drain_led_completions(scheduler_runtime_t *runtime)
{
    if (nocti_v4_async_led_take_scheduled_overflow(runtime->async_led)) {
        fault_scan(runtime, ESP_ERR_NO_MEM);
        return;
    }
    nocti_v4_async_led_completion_t completion;
    while (nocti_v4_async_led_pop_scheduled_completion(runtime->async_led,
                                                        &completion)) {
        process_led_completion(runtime, &completion);
        if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) ==
            NOCTI_V4_SCAN_FAULTED) {
            break;
        }
    }
}

static void record_scheduler_lateness(scheduler_runtime_t *runtime,
                                      const deadline_event_t *event,
                                      uint64_t handling_us)
{
    const uint32_t lateness_us = handling_us >= event->deadline_us ?
        (uint32_t)(handling_us - event->deadline_us) : 0u;
    runtime->lateness_sum_us += lateness_us;
    ++runtime->lateness_count;
    update_min(&runtime->health->scheduler_lateness_min_us, lateness_us);
    update_max(&runtime->health->scheduler_lateness_max_us, lateness_us);
    counter_store(&runtime->health->scheduler_lateness_avg_us,
                  (uint32_t)(runtime->lateness_sum_us / runtime->lateness_count));
    if (lateness_us > NOCTI_V4_SCHEDULER_MISSED_DEADLINE_US) {
        counter_add(&runtime->health->scheduler_missed_deadline, 1u);
    }
}

static bool deadline_matches_active_session(const scheduler_runtime_t *runtime,
                                            const deadline_event_t *event)
{
    return runtime->scan_active && event->session_epoch == runtime->session_epoch &&
           event->slot_epoch >= runtime->slot_epoch;
}

static void process_deadline(scheduler_runtime_t *runtime, const deadline_event_t *event)
{
    if (!deadline_matches_active_session(runtime, event)) {
        counter_add(&runtime->health->stale_i2c_completion, 1u);
        return;
    }
    uint64_t handling_us = 0u;
    if (nocti_v4_time_now_us(runtime->timebase, &handling_us) != ESP_OK) {
        fault_scan(runtime, ESP_FAIL);
        return;
    }
    if (event->phase == DEADLINE_SLOT_START) {
        slot_record_t *previous = slot_at(runtime, runtime->active_history_index,
                                          runtime->active_slot_index);
        if (previous != NULL && previous->valid && !previous->finalized &&
            (previous->session_epoch != event->session_epoch ||
             previous->state != NOCTI_V4_SLOT_LED_OFF_STABLE)) {
            fault_scan(runtime, ESP_ERR_INVALID_STATE);
            return;
        }
        if (event->slot_index == 0u) {
            begin_frame(runtime, event->frame_sequence, event->session_epoch,
                        event->frame_epoch_us);
            runtime->frame_sequence = event->frame_sequence;
            runtime->slot_epoch = event->slot_epoch;
        }
        slot_record_t *record = init_slot_record(runtime, event, handling_us);
        nocti_v4_async_led_status_t before;
        nocti_v4_async_led_get_status(runtime->async_led, &before);
        if (record == NULL || before.mode != PCA_IO_MODE_ASYNC ||
            !before.submissions_allowed || before.transaction_pending ||
            before.selected_source >= 0) {
            fault_scan(runtime, ESP_FAIL);
            return;
        }

        /* Slot-start critical path: after only immutable identity and safe-state
         * validation, commit to ON and hand the prebuilt payload identity to the
         * priority-22 LED owner. Previous-slot publication and frame accounting
         * deliberately occur after this zero-wait queue handoff; I2C completion
         * arrives later through the static scheduler-completion ring. */
        uint64_t request_us = 0u;
        if (nocti_v4_time_now_us(runtime->timebase, &request_us) != ESP_OK) {
            fault_scan(runtime, ESP_FAIL);
            return;
        }
        portENTER_CRITICAL(&runtime->state_lock);
        record->led_on_request_ts = request_us;
        portEXIT_CRITICAL(&runtime->state_lock);
        const esp_err_t on_result = submit_on(runtime, record,
                                               event->deadline_us, request_us);

        uint64_t post_handoff_us = request_us;
        (void)nocti_v4_time_now_us(runtime->timebase, &post_handoff_us);
        if (previous != NULL && previous->valid && !previous->finalized) {
            finalize_slot(runtime, previous, post_handoff_us, false, false);
        }
        counter_add(&runtime->health->scheduler_events, 1u);
        record_scheduler_lateness(runtime, event, handling_us);
        if (event->slot_index == 0u) {
            counter_add(&runtime->health->frames_started, 1u);
        }
        uint64_t deferred_done_us = post_handoff_us;
        if (nocti_v4_time_now_us(runtime->timebase, &deferred_done_us) == ESP_OK) {
            const uint32_t deferred_us = (uint32_t)(deferred_done_us - post_handoff_us);
            runtime->deferred_work_sum_us += deferred_us;
            ++runtime->deferred_work_count;
            update_max(&runtime->health->scheduler_deferred_work_max_us,
                       deferred_us);
            counter_store(&runtime->health->scheduler_deferred_work_avg_us,
                          (uint32_t)(runtime->deferred_work_sum_us /
                                     runtime->deferred_work_count));
            if (deferred_done_us >= record->nominal_led_off_ts) {
                counter_add(&runtime->health->scheduler_deferred_work_crossed_off_deadline,
                            1u);
            }
        }
        if (on_result != ESP_OK) {
            fault_scan(runtime, on_result);
        }
        return;
    }

    slot_record_t *record = slot_at(runtime, runtime->active_history_index,
                                    event->slot_index);
    if (record == NULL || !record->valid || record->slot_epoch != event->slot_epoch ||
        record->state != NOCTI_V4_SLOT_LED_ON_STABLE) {
        fault_scan(runtime, ESP_ERR_INVALID_STATE);
        return;
    }
    portENTER_CRITICAL(&runtime->state_lock);
    record->scheduler_off_deadline_ts = event->deadline_us;
    portEXIT_CRITICAL(&runtime->state_lock);
    nocti_v4_async_led_status_t before;
    nocti_v4_async_led_get_status(runtime->async_led, &before);
    if (before.mode != PCA_IO_MODE_ASYNC || !before.submissions_allowed ||
        before.transaction_pending ||
        before.selected_source != (int32_t)record->logical_source) {
        fault_scan(runtime, ESP_ERR_INVALID_STATE);
        return;
    }

    /* OFF is a hard edge, exactly like ON. Capture the transition request only
     * after the minimum immutable identity/safe-state checks, then hand the
     * prebuilt source-specific two-byte OFF to the priority-22 LED owner before
     * scheduler counters or other bookkeeping. Any invariant or I/O failure
     * reaches fault_scan(), which deliberately retains strong seven-byte
     * ALL_OFF plus the async driver's RESET# fail-safe. */
    uint64_t request_us = 0u;
    if (nocti_v4_time_now_us(runtime->timebase, &request_us) != ESP_OK) {
        fault_scan(runtime, ESP_FAIL);
        return;
    }
    const esp_err_t off_result = submit_off(runtime, record,
                                            event->deadline_us, handling_us,
                                            request_us);
    counter_add(&runtime->health->scheduler_events, 1u);
    record_scheduler_lateness(runtime, event, handling_us);
    if (off_result != ESP_OK) {
        fault_scan(runtime, off_result);
    }
}

static uint64_t aligned_frame_epoch(uint64_t now_us)
{
    const uint64_t minimum = now_us + NOCTI_V4_SCHEDULER_ARM_LEAD_US;
    const uint64_t remainder = minimum % NOCTI_V4_SLOT_PERIOD_US;
    return remainder == 0u ? minimum : minimum + NOCTI_V4_SLOT_PERIOD_US - remainder;
}

static void reset_scheduler_counters(scheduler_runtime_t *runtime)
{
#define RESET_SCAN(field) counter_store(&runtime->health->field, 0u)
    RESET_SCAN(frames_started); RESET_SCAN(frames_completed); RESET_SCAN(slots_started);
    RESET_SCAN(slots_finalized); RESET_SCAN(slots_invalid); RESET_SCAN(slots_aborted_by_stop); RESET_SCAN(scheduler_events);
    RESET_SCAN(scheduler_event_overflow); RESET_SCAN(scheduler_missed_deadline);
    RESET_SCAN(scheduler_lateness_min_us); RESET_SCAN(scheduler_lateness_max_us);
    RESET_SCAN(scheduler_lateness_avg_us); RESET_SCAN(scheduler_deferred_work_max_us);
    RESET_SCAN(scheduler_deferred_work_avg_us);
    RESET_SCAN(scheduler_deferred_work_crossed_off_deadline);
    RESET_SCAN(led_on_submitted);
    RESET_SCAN(led_on_completed); RESET_SCAN(led_off_submitted); RESET_SCAN(led_off_completed);
    RESET_SCAN(led_on_latency_min_us); RESET_SCAN(led_on_latency_max_us);
    RESET_SCAN(led_on_latency_avg_us); RESET_SCAN(led_off_latency_min_us);
    RESET_SCAN(led_off_latency_max_us); RESET_SCAN(led_off_latency_avg_us);
    RESET_SCAN(led_completion_timeout); RESET_SCAN(stale_i2c_completion);
    RESET_SCAN(candidate_under8_on); RESET_SCAN(candidate_under8_off);
    RESET_SCAN(candidate_under10_on); RESET_SCAN(candidate_under10_off);
    RESET_SCAN(candidate_min_on); RESET_SCAN(candidate_min_off);
    RESET_SCAN(candidate_on_0_to_7); RESET_SCAN(candidate_on_8_to_9);
    RESET_SCAN(candidate_on_10_to_12); RESET_SCAN(candidate_on_gt_12);
    RESET_SCAN(candidate_off_0_to_7); RESET_SCAN(candidate_off_8_to_9);
    RESET_SCAN(candidate_off_10_to_12); RESET_SCAN(candidate_off_gt_12);
    RESET_SCAN(raw_rejected_precommit); RESET_SCAN(raw_candidate_on);
    RESET_SCAN(raw_rejected_on_settle); RESET_SCAN(raw_rejected_transition);
    RESET_SCAN(raw_rejected_off_settle); RESET_SCAN(raw_candidate_off);
    RESET_SCAN(raw_rejected_guard); RESET_SCAN(scan_session_faults);
#undef RESET_SCAN
    runtime->lateness_sum_us = 0u;
    runtime->lateness_count = 0u;
    runtime->on_latency_sum_us = 0u;
    runtime->on_latency_count = 0u;
    runtime->off_latency_sum_us = 0u;
    runtime->off_latency_count = 0u;
    runtime->deferred_work_sum_us = 0u;
    runtime->deferred_work_count = 0u;
}

static esp_err_t start_internal(scheduler_runtime_t *runtime)
{
    if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) != NOCTI_V4_SCAN_STOPPED ||
        nocti_v4_async_led_mode(runtime->async_led) != PCA_IO_MODE_ASYNC) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t now_us = 0u;
    const esp_err_t now_result = nocti_v4_time_now_us(runtime->timebase, &now_us);
    if (now_result != ESP_OK) {
        return now_result;
    }
    __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_STARTING, __ATOMIC_RELEASE);
    reset_scheduler_counters(runtime);
    ++runtime->session_epoch;
    runtime->frame_sequence = 1u;
    ++runtime->slot_epoch;
    runtime->active_history_index = -1;
    runtime->active_slot_index = -1;
    runtime->last_history_index = -1;
    runtime->last_slot_index = -1;
    /* History clearing is bounded startup work while no deadline is armed.
     * A frame rollover now selects a precleared history bank and overwrites
     * only the current 256-byte slot record; it never clears 5,120 bytes on
     * the slot-start critical path. */
    memset(runtime->frames, 0, sizeof(runtime->frames));
    memset(runtime->slots, 0, sizeof(runtime->slots));
    __atomic_store_n(&runtime->deadline_head, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->deadline_tail, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->deadline_depth_max, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->finalized_head, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->finalized_tail, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->event_overflow_pending, false, __ATOMIC_RELEASE);
    const uint32_t configured_off_phase_us = __atomic_load_n(
        &runtime->configured_off_phase_us, __ATOMIC_ACQUIRE);
    if (!NOCTI_V4_OFF_PHASE_IS_SUPPORTED(configured_off_phase_us)) {
        runtime->last_error = ESP_ERR_INVALID_STATE;
        __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_FAULTED,
                         __ATOMIC_RELEASE);
        counter_add(&runtime->health->scan_session_faults, 1u);
        return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&runtime->active_off_phase_us, configured_off_phase_us,
                     __ATOMIC_RELEASE);
    const uint64_t frame_epoch_us = aligned_frame_epoch(now_us);
    const esp_err_t arm_result = arm_first_deadline(runtime, runtime->session_epoch,
                                                     runtime->frame_sequence,
                                                     runtime->slot_epoch,
                                                     frame_epoch_us);
    if (arm_result != ESP_OK) {
        runtime->last_error = arm_result;
        __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_FAULTED, __ATOMIC_RELEASE);
        counter_add(&runtime->health->scan_session_faults, 1u);
        return arm_result;
    }
    runtime->last_error = ESP_OK;
    __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_RUNNING, __ATOMIC_RELEASE);
    counter_add(&runtime->health->scan_sessions_started, 1u);
    return ESP_OK;
}

static esp_err_t stop_internal(scheduler_runtime_t *runtime)
{
    const nocti_v4_scan_lifecycle_t prior = __atomic_load_n(&runtime->lifecycle,
                                                             __ATOMIC_ACQUIRE);
    if (prior == NOCTI_V4_SCAN_STOPPED) {
        return ESP_OK;
    }
    __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_STOPPING, __ATOMIC_RELEASE);
    const esp_err_t disarm_result = disarm_deadlines(runtime);
    uint64_t now_us = 0u;
    (void)nocti_v4_time_now_us(runtime->timebase, &now_us);
    slot_record_t *record = slot_at(runtime, runtime->active_history_index,
                                    runtime->active_slot_index);
    if (record != NULL && record->valid && !record->finalized) {
        finalize_slot(runtime, record, now_us, false, true);
    }
    esp_err_t off_result = ESP_OK;
    if (nocti_v4_async_led_mode(runtime->async_led) == PCA_IO_MODE_ASYNC) {
        counter_add(&runtime->health->led_off_submitted, 1u);
        off_result = nocti_v4_async_led_all_off_burst(runtime->async_led);
        if (off_result == ESP_OK) {
            counter_add(&runtime->health->led_off_completed, 1u);
        } else if (off_result == ESP_ERR_TIMEOUT) {
            counter_add(&runtime->health->led_completion_timeout, 1u);
        }
    }
    if (disarm_result != ESP_OK || off_result != ESP_OK) {
        runtime->last_error = disarm_result != ESP_OK ? disarm_result : off_result;
        __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_FAULTED, __ATOMIC_RELEASE);
        counter_add(&runtime->health->scan_session_faults, 1u);
        return runtime->last_error;
    }
    __atomic_store_n(&runtime->lifecycle, NOCTI_V4_SCAN_STOPPED, __ATOMIC_RELEASE);
    counter_add(&runtime->health->scan_sessions_stopped, 1u);
    return ESP_OK;
}

static void scheduler_task(void *argument)
{
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)argument;
    runtime->task_core = xPortGetCoreID();
    __atomic_store_n(&runtime->health->scheduler_task_core, runtime->task_core,
                     __ATOMIC_RELEASE);
    nocti_v4_task_wdt_subscribe();
    for (;;) {
        nocti_v4_task_wdt_kick();
        nocti_v4_task_wdt_log_pending();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NOCTI_V4_RTOS_WDT_IDLE_WAIT_MS));
        /* A completion may share a wake with the next absolute deadline. It
         * must advance the slot state before that deadline is validated. */
        drain_led_completions(runtime);
        scheduler_command_t command;
        while (xQueueReceive(runtime->command_queue, &command, 0u) == pdTRUE) {
            const esp_err_t result = command.type == SCHEDULER_COMMAND_START ?
                start_internal(runtime) : stop_internal(runtime);
            runtime->command_result = result;
            if (result != ESP_OK) {
                runtime->last_error = result;
            }
            (void)xSemaphoreGive(runtime->command_done);
        }
        /* SCAN_STOP can wait behind an edge in the LED queue; consume the
         * resulting terminal record after the strong stop path returns. */
        drain_led_completions(runtime);
        if (__atomic_exchange_n(&runtime->event_overflow_pending, false,
                                __ATOMIC_ACQ_REL)) {
            fault_scan(runtime, ESP_ERR_NO_MEM);
            continue;
        }
        deadline_event_t event;
        while (deadline_pop(runtime, &event)) {
            process_deadline(runtime, &event);
            if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) ==
                NOCTI_V4_SCAN_FAULTED) {
                break;
            }
        }
    }
}

static esp_err_t submit_command(nocti_v4_scheduler_t *scheduler,
                                scheduler_command_type_t type)
{
    if (scheduler == NULL || !scheduler->initialized || scheduler->runtime == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)scheduler->runtime;
    if (xSemaphoreTake(runtime->command_lock,
                       pdMS_TO_TICKS(SCHEDULER_COMMAND_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    (void)xSemaphoreTake(runtime->command_done, 0u);
    const scheduler_command_t command = {.type = type};
    esp_err_t result = ESP_OK;
    if (xQueueSend(runtime->command_queue, &command,
                   pdMS_TO_TICKS(SCHEDULER_COMMAND_TIMEOUT_MS)) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    } else {
        xTaskNotifyGive(runtime->task);
        if (xSemaphoreTake(runtime->command_done,
                           pdMS_TO_TICKS(SCHEDULER_COMMAND_TIMEOUT_MS)) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
        } else {
            result = runtime->command_result;
        }
    }
    (void)xSemaphoreGive(runtime->command_lock);
    return result;
}

esp_err_t nocti_v4_scheduler_init(nocti_v4_scheduler_t *scheduler,
                                   nocti_v4_time_t *timebase,
                                   nocti_v4_async_led_t *async_led,
                                   nocti_v4_health_t *health)
{
    if (scheduler == NULL || timebase == NULL || async_led == NULL || health == NULL ||
        !nocti_v4_time_ready(timebase) || !async_led->initialized || s_runtime_claimed) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(scheduler, 0, sizeof(*scheduler));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime_claimed = true;
    scheduler_runtime_t *runtime = &s_runtime;
    runtime->public_scheduler = scheduler;
    runtime->timebase = timebase;
    runtime->async_led = async_led;
    runtime->health = health;
    runtime->state_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    runtime->timer_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    runtime->active_history_index = -1;
    runtime->active_slot_index = -1;
    runtime->last_history_index = -1;
    runtime->last_slot_index = -1;
    runtime->task_core = -1;
    runtime->gptimer_callback_core = -1;
    runtime->lifecycle = NOCTI_V4_SCAN_STOPPED;
    runtime->configured_off_phase_us = NOCTI_V4_OFF_PHASE_DEFAULT_US;
    runtime->active_off_phase_us = NOCTI_V4_OFF_PHASE_DEFAULT_US;
    for (uint8_t source = 0u; source < NOCTI_V4_SLOT_COUNT; ++source) {
        if (nocti_v4_led_resolve_source(source,
                                        &runtime->source_to_channel[source]) != ESP_OK) {
            s_runtime_claimed = false;
            return ESP_ERR_INVALID_STATE;
        }
    }
    runtime->command_queue = xQueueCreateStatic(SCHEDULER_COMMAND_QUEUE_CAPACITY,
                                                 sizeof(scheduler_command_t),
                                                 runtime->command_queue_buffer,
                                                 &runtime->command_queue_storage);
    runtime->command_lock = xSemaphoreCreateMutexStatic(&runtime->command_lock_storage);
    runtime->command_done = xSemaphoreCreateBinaryStatic(&runtime->command_done_storage);
    if (runtime->command_queue == NULL || runtime->command_lock == NULL ||
        runtime->command_done == NULL) {
        s_runtime_claimed = false;
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t handler_result = nocti_v4_time_set_alarm_handler(timebase,
                                                                       scheduler_alarm_handler,
                                                                       runtime);
    if (handler_result != ESP_OK) {
        s_runtime_claimed = false;
        return handler_result;
    }
    runtime->task = xTaskCreateStaticPinnedToCore(scheduler_task, "nocti_v4_sched",
                                                  NOCTI_V4_SCHEDULER_STACK_BYTES, runtime,
                                                  NOCTI_V4_SCHEDULER_PRIORITY,
                                                  runtime->task_stack,
                                                  &runtime->task_storage,
                                                  NOCTI_V4_SCHEDULER_CORE);
    if (runtime->task == NULL) {
        (void)nocti_v4_time_set_alarm_handler(timebase, NULL, NULL);
        s_runtime_claimed = false;
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t bind_result =
        nocti_v4_async_led_bind_scheduled_consumer(async_led, runtime->task);
    if (bind_result != ESP_OK) {
        vTaskDelete(runtime->task);
        runtime->task = NULL;
        (void)nocti_v4_time_set_alarm_handler(timebase, NULL, NULL);
        s_runtime_claimed = false;
        return bind_result;
    }
    __atomic_store_n(&health->scheduler_task_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&health->scheduler_gptimer_callback_core, -1, __ATOMIC_RELEASE);
    scheduler->runtime = runtime;
    scheduler->initialized = true;
    return ESP_OK;
}

esp_err_t nocti_v4_scheduler_start(nocti_v4_scheduler_t *scheduler)
{
    return submit_command(scheduler, SCHEDULER_COMMAND_START);
}

esp_err_t nocti_v4_scheduler_stop(nocti_v4_scheduler_t *scheduler)
{
    return submit_command(scheduler, SCHEDULER_COMMAND_STOP);
}

esp_err_t nocti_v4_scheduler_set_fs(nocti_v4_scheduler_t *scheduler, uint32_t fs)
{
    if (scheduler == NULL || scheduler->runtime == NULL ||
        !(fs == 2u || fs == 4u || fs == 5u || fs == 8u || fs == 10u))
        return ESP_ERR_INVALID_ARG;
    scheduler_runtime_t *runtime = scheduler->runtime;
    if (xSemaphoreTake(runtime->command_lock, pdMS_TO_TICKS(1000u)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) == NOCTI_V4_SCAN_STOPPED &&
        !__atomic_load_n(&runtime->scan_active, __ATOMIC_ACQUIRE)) {
        uint32_t slot = 1000000u / (NOCTI_V4_SLOT_COUNT * fs);
        __atomic_store_n(&s_slot_period_us, slot, __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->configured_off_phase_us, slot * 44u / 100u, __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->active_off_phase_us, slot * 44u / 100u, __ATOMIC_RELEASE);
        result = ESP_OK;
    }
    (void)xSemaphoreGive(runtime->command_lock);
    return result;
}

esp_err_t nocti_v4_scheduler_set_off_phase_us(nocti_v4_scheduler_t *scheduler,
                                               uint32_t off_phase_us)
{
    if (scheduler == NULL || scheduler->runtime == NULL ||
        !NOCTI_V4_OFF_PHASE_IS_SUPPORTED(off_phase_us)) {
        return ESP_ERR_INVALID_ARG;
    }
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)scheduler->runtime;
    if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) !=
            NOCTI_V4_SCAN_STOPPED ||
        __atomic_load_n(&runtime->scan_active, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&runtime->configured_off_phase_us, off_phase_us,
                     __ATOMIC_RELEASE);
    return ESP_OK;
}

bool nocti_v4_scheduler_is_active(const nocti_v4_scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->runtime == NULL) {
        return false;
    }
    const scheduler_runtime_t *runtime = (const scheduler_runtime_t *)scheduler->runtime;
    const nocti_v4_scan_lifecycle_t state = __atomic_load_n(&runtime->lifecycle,
                                                             __ATOMIC_ACQUIRE);
    return state == NOCTI_V4_SCAN_STARTING || state == NOCTI_V4_SCAN_RUNNING ||
           state == NOCTI_V4_SCAN_STOPPING || state == NOCTI_V4_SCAN_RECOVERING ||
           state == NOCTI_V4_SCAN_FAULTED;
}

esp_err_t nocti_v4_scheduler_bind_optical_task(nocti_v4_scheduler_t *scheduler,
                                               TaskHandle_t task)
{
    if (scheduler == NULL || scheduler->runtime == NULL || task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)scheduler->runtime;
    if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) !=
        NOCTI_V4_SCAN_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->optical_task = task;
    return ESP_OK;
}

bool nocti_v4_scheduler_pop_finalized_slot(nocti_v4_scheduler_t *scheduler,
                                           nocti_v4_slot_timing_t *out_timing)
{
    if (scheduler == NULL || scheduler->runtime == NULL || out_timing == NULL) {
        return false;
    }
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)scheduler->runtime;
    if (runtime->optical_task != xTaskGetCurrentTaskHandle()) {
        return false;
    }
    return finalized_pop(runtime, out_timing);
}

void nocti_v4_scheduler_get_status(const nocti_v4_scheduler_t *scheduler,
                                   nocti_v4_scheduler_status_t *out_status)
{
    if (out_status == NULL) {
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    if (scheduler == NULL || scheduler->runtime == NULL) {
        return;
    }
    const scheduler_runtime_t *runtime = (const scheduler_runtime_t *)scheduler->runtime;
    out_status->initialized = scheduler->initialized;
    out_status->timer_armed = __atomic_load_n(&runtime->timer_armed, __ATOMIC_ACQUIRE);
    out_status->scan_active = __atomic_load_n(&runtime->scan_active, __ATOMIC_ACQUIRE);
    out_status->lifecycle = __atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE);
    out_status->session_epoch = runtime->session_epoch;
    out_status->frame_sequence = runtime->frame_sequence;
    out_status->slot_epoch = runtime->slot_epoch;
    out_status->active_slot_index = runtime->active_slot_index >= 0 ?
        (uint8_t)runtime->active_slot_index : UINT8_MAX;
    out_status->frame_epoch_us = runtime->next_deadline.frame_epoch_us;
    out_status->next_deadline_us = runtime->next_deadline.deadline_us;
    out_status->configured_off_phase_us = __atomic_load_n(
        &runtime->configured_off_phase_us, __ATOMIC_ACQUIRE);
    out_status->active_off_phase_us = __atomic_load_n(
        &runtime->active_off_phase_us, __ATOMIC_ACQUIRE);
    out_status->deadline_ring_depth = deadline_depth(runtime);
    out_status->deadline_ring_depth_max = __atomic_load_n(&runtime->deadline_depth_max,
                                                           __ATOMIC_ACQUIRE);
    out_status->scheduler_task_core = runtime->task_core;
    out_status->gptimer_callback_core = __atomic_load_n(&runtime->gptimer_callback_core,
                                                         __ATOMIC_ACQUIRE);
    out_status->last_error = runtime->last_error;
    portENTER_CRITICAL((portMUX_TYPE *)&runtime->state_lock);
    const slot_record_t *record = slot_at((scheduler_runtime_t *)runtime,
                                          runtime->active_history_index,
                                          runtime->active_slot_index);
    if (record != NULL && record->valid) {
        out_status->active_slot_state = record->state;
        out_status->active_source = record->logical_source;
    }
    portEXIT_CRITICAL((portMUX_TYPE *)&runtime->state_lock);
}

void nocti_v4_scheduler_print_status(const nocti_v4_scheduler_t *scheduler)
{
    nocti_v4_scheduler_status_t status;
    nocti_v4_scheduler_get_status(scheduler, &status);
    printf("SCAN_STATUS lifecycle=%s initialized=%u active=%u timer_armed=%u session=%" PRIu32
           " frame=%" PRIu32 " slot_epoch=%" PRIu32 " active_slot=%u source=%u state=%s"
           " frame_epoch_us=%" PRIu64 " next_deadline_us=%" PRIu64
           " configured_off_phase_us=%" PRIu32 " active_off_phase_us=%" PRIu32
           " deadline_depth=%" PRIu32 " deadline_depth_max=%" PRIu32
           " sched_core=%" PRId32 " gptimer_core=%" PRId32
           " last_error=%s\n",
           nocti_v4_scan_lifecycle_name(status.lifecycle), status.initialized ? 1u : 0u,
           status.scan_active ? 1u : 0u, status.timer_armed ? 1u : 0u,
           status.session_epoch, status.frame_sequence, status.slot_epoch,
           (unsigned)status.active_slot_index, (unsigned)status.active_source,
           nocti_v4_slot_state_name(status.active_slot_state), status.frame_epoch_us,
           status.next_deadline_us, status.configured_off_phase_us,
           status.active_off_phase_us, status.deadline_ring_depth,
           status.deadline_ring_depth_max,
           status.scheduler_task_core, status.gptimer_callback_core,
           nocti_v4_err_name(status.last_error));
}

void nocti_v4_scheduler_print_timing(const nocti_v4_scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->runtime == NULL) return;
    const scheduler_runtime_t *runtime = (const scheduler_runtime_t *)scheduler->runtime;
    const nocti_v4_health_t *health = runtime->health;
    printf("SCHED_TIMING slot_count=%u slot_period_us=%" PRIu32 " frame_period_us=%" PRIu32
           " configured_off_phase_us=%" PRIu32 " active_off_phase_us=%" PRIu32
           " on_settle_us=%u off_settle_us=%u guard_us=%u K_on=%u/%u K_off=%u/%u"
           " lateness_min_us=%" PRIu32 " lateness_max_us=%" PRIu32
           " lateness_avg_us=%" PRIu32 " missed_deadline=%" PRIu32
           " deferred_work_max_us=%" PRIu32 " deferred_work_avg_us=%" PRIu32
           " deferred_crossed_off_deadline=%" PRIu32
           " on_latency_min_us=%" PRIu32 " on_latency_max_us=%" PRIu32
           " on_latency_avg_us=%" PRIu32 " off_latency_min_us=%" PRIu32
           " off_latency_max_us=%" PRIu32 " off_latency_avg_us=%" PRIu32 "\n",
           NOCTI_V4_SLOT_COUNT, NOCTI_V4_SLOT_PERIOD_US, NOCTI_V4_FRAME_PERIOD_US,
           __atomic_load_n(&runtime->configured_off_phase_us, __ATOMIC_ACQUIRE),
           __atomic_load_n(&runtime->active_off_phase_us, __ATOMIC_ACQUIRE),
           NOCTI_V4_LED_ON_SETTLE_US,
           NOCTI_V4_LED_OFF_SETTLE_US, NOCTI_V4_SLOT_END_GUARD_US,
           NOCTI_V4_K_ON_MINIMUM, NOCTI_V4_K_ON_TARGET,
           NOCTI_V4_K_OFF_MINIMUM, NOCTI_V4_K_OFF_TARGET,
           health->scheduler_lateness_min_us, health->scheduler_lateness_max_us,
           health->scheduler_lateness_avg_us, health->scheduler_missed_deadline,
           health->scheduler_deferred_work_max_us,
           health->scheduler_deferred_work_avg_us,
           health->scheduler_deferred_work_crossed_off_deadline,
           health->led_on_latency_min_us, health->led_on_latency_max_us,
           health->led_on_latency_avg_us, health->led_off_latency_min_us,
           health->led_off_latency_max_us, health->led_off_latency_avg_us);
}

void nocti_v4_scheduler_print_health(const nocti_v4_scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->runtime == NULL) return;
    const nocti_v4_health_t *h = ((const scheduler_runtime_t *)scheduler->runtime)->health;
    printf("SCAN_HEALTH frames_started=%" PRIu32 " frames_completed=%" PRIu32
           " slots_started=%" PRIu32 " slots_finalized=%" PRIu32 " slots_invalid=%" PRIu32
           " scheduler_events=%" PRIu32 " scheduler_event_overflow=%" PRIu32
           " led_on_submitted=%" PRIu32 " led_on_completed=%" PRIu32
           " led_off_submitted=%" PRIu32 " led_off_completed=%" PRIu32
           " completion_timeout=%" PRIu32 " stale_i2c_completion=%" PRIu32
           " scan_started=%" PRIu32 " scan_stopped=%" PRIu32 " scan_faults=%" PRIu32 "\n",
           h->frames_started, h->frames_completed, h->slots_started, h->slots_finalized,
           h->slots_invalid, h->scheduler_events, h->scheduler_event_overflow,
           h->led_on_submitted, h->led_on_completed, h->led_off_submitted,
           h->led_off_completed, h->led_completion_timeout, h->stale_i2c_completion,
           h->scan_sessions_started, h->scan_sessions_stopped, h->scan_session_faults);
    printf("SCAN_K under8_on=%" PRIu32 " under8_off=%" PRIu32
           " under10_on=%" PRIu32 " under10_off=%" PRIu32
           " min_on=%" PRIu32 " min_off=%" PRIu32
           " on_hist=0_7:%" PRIu32 ",8_9:%" PRIu32 ",10_12:%" PRIu32 ",gt12:%" PRIu32
           " off_hist=0_7:%" PRIu32 ",8_9:%" PRIu32 ",10_12:%" PRIu32 ",gt12:%" PRIu32
           " semantics=exclusive under8_is_k_0_7 under10_is_k_8_9\n",
           h->candidate_under8_on, h->candidate_under8_off,
           h->candidate_under10_on, h->candidate_under10_off,
           h->candidate_min_on, h->candidate_min_off,
           h->candidate_on_0_to_7, h->candidate_on_8_to_9,
           h->candidate_on_10_to_12, h->candidate_on_gt_12,
           h->candidate_off_0_to_7, h->candidate_off_8_to_9,
           h->candidate_off_10_to_12, h->candidate_off_gt_12);
    printf("SCAN_RAW precommit=%" PRIu32 " candidate_on=%" PRIu32
           " on_settle=%" PRIu32 " transition=%" PRIu32 " off_settle=%" PRIu32
           " candidate_off=%" PRIu32 " guard=%" PRIu32 "\n",
           h->raw_rejected_precommit, h->raw_candidate_on, h->raw_rejected_on_settle,
           h->raw_rejected_transition, h->raw_rejected_off_settle,
           h->raw_candidate_off, h->raw_rejected_guard);
}

void nocti_v4_scheduler_dump_slot_timing(const nocti_v4_scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->runtime == NULL) return;
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)scheduler->runtime;
    slot_record_t copy = {0};
    portENTER_CRITICAL(&runtime->state_lock);
    slot_record_t *record = slot_at(runtime, runtime->last_history_index,
                                    runtime->last_slot_index);
    if (record == NULL || !record->valid) {
        record = slot_at(runtime, runtime->active_history_index, runtime->active_slot_index);
    }
    if (record != NULL) copy = *record;
    portEXIT_CRITICAL(&runtime->state_lock);
    if (!copy.valid) {
        printf("DUMP_SLOT_TIMING state=NO_SLOT\n");
        return;
    }
    printf("DUMP_SLOT_TIMING session=%" PRIu32 " frame=%" PRIu32 " slot=%u source=%u channel=%u"
           " state=%s finalized=%u nominal_start=%" PRIu64 " on_deadline=%" PRIu64
           " on_handling=%" PRIu64 " on_request=%" PRIu64
           " on_queue=%" PRIu64 " on_dequeue=%" PRIu64 " on_submit=%" PRIu64
           " on_complete=%" PRIu64 " on_commit=%" PRIu64 " on_stable=%" PRIu64
           " nominal_off=%" PRIu64 " off_deadline=%" PRIu64 " off_handling=%" PRIu64
           " off_request=%" PRIu64 " off_queue=%" PRIu64
           " off_dequeue=%" PRIu64 " off_submit=%" PRIu64 " off_complete=%" PRIu64
           " off_commit=%" PRIu64 " off_stable=%" PRIu64 " guard=%" PRIu64
           " nominal_end=%" PRIu64 " finalized_at=%" PRIu64 "\n",
           copy.session_epoch, copy.frame_sequence, copy.slot_index, copy.logical_source,
           copy.pca_channel, nocti_v4_slot_state_name(copy.state), copy.finalized ? 1u : 0u,
           copy.nominal_slot_start_ts, copy.scheduler_on_deadline_ts,
           copy.scheduler_on_handling_ts, copy.led_on_request_ts,
           copy.led_on_queue_ts, copy.led_on_dequeue_ts, copy.led_on_submit_ts,
           copy.led_on_complete_ts, copy.led_on_commit_ts, copy.led_on_stable_start_ts,
           copy.nominal_led_off_ts, copy.scheduler_off_deadline_ts,
           copy.scheduler_off_handling_ts, copy.led_off_request_ts,
           copy.led_off_queue_ts, copy.led_off_dequeue_ts,
           copy.led_off_submit_ts, copy.led_off_complete_ts, copy.led_off_commit_ts,
           copy.led_off_stable_start_ts, copy.slot_guard_start_ts,
           copy.nominal_slot_end_ts, copy.actual_finalize_ts);
    printf("DUMP_SLOT_COUNTS candidate_on=%" PRIu32 " candidate_off=%" PRIu32
           " precommit=%" PRIu32 " on_settle=%" PRIu32 " transition=%" PRIu32
           " off_settle=%" PRIu32 " guard=%" PRIu32 " on_command=%" PRIu32
           " off_command=%" PRIu32 "\n",
           copy.candidate_on, copy.candidate_off, copy.rejected_precommit,
           copy.rejected_on_settle, copy.rejected_transition, copy.rejected_off_settle,
           copy.rejected_guard, copy.led_on_command_id, copy.led_off_command_id);
}

void nocti_v4_scheduler_sample_stack_watermark(nocti_v4_scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->runtime == NULL) return;
    scheduler_runtime_t *runtime = (scheduler_runtime_t *)scheduler->runtime;
    if (runtime->task != NULL) {
        counter_store(&runtime->health->scheduler_task_stack_watermark_bytes,
                      uxTaskGetStackHighWaterMark(runtime->task));
    }
}

const char *nocti_v4_scan_lifecycle_name(nocti_v4_scan_lifecycle_t state)
{
    switch (state) {
        case NOCTI_V4_SCAN_STOPPED: return "STOPPED";
        case NOCTI_V4_SCAN_STARTING: return "STARTING_SCAN";
        case NOCTI_V4_SCAN_RUNNING: return "RUNNING_SCAN";
        case NOCTI_V4_SCAN_STOPPING: return "STOPPING_SCAN";
        case NOCTI_V4_SCAN_RECOVERING: return "RECOVERING";
        case NOCTI_V4_SCAN_FAULTED: return "FAULTED";
        default: return "UNKNOWN";
    }
}

const char *nocti_v4_slot_state_name(nocti_v4_slot_state_t state)
{
    switch (state) {
        case NOCTI_V4_SLOT_IDLE: return "SLOT_IDLE";
        case NOCTI_V4_SLOT_LED_ON_PENDING: return "LED_ON_PENDING";
        case NOCTI_V4_SLOT_LED_ON_COMMITTED: return "LED_ON_COMMITTED";
        case NOCTI_V4_SLOT_LED_ON_SETTLING: return "LED_ON_SETTLING";
        case NOCTI_V4_SLOT_LED_ON_STABLE: return "LED_ON_STABLE";
        case NOCTI_V4_SLOT_LED_OFF_PENDING: return "LED_OFF_PENDING";
        case NOCTI_V4_SLOT_LED_OFF_COMMITTED: return "LED_OFF_COMMITTED";
        case NOCTI_V4_SLOT_LED_OFF_SETTLING: return "LED_OFF_SETTLING";
        case NOCTI_V4_SLOT_LED_OFF_STABLE: return "LED_OFF_STABLE";
        case NOCTI_V4_SLOT_FINALIZE: return "SLOT_FINALIZE";
        case NOCTI_V4_SLOT_INVALID: return "SLOT_INVALID";
        case NOCTI_V4_SLOT_LED_RECOVERY: return "LED_RECOVERY";
        default: return "UNKNOWN";
    }
}
