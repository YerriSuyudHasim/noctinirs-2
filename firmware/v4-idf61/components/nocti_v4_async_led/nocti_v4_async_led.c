#include "nocti_v4_async_led.h"

#include <inttypes.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nocti_v4_common.h"
#include "nocti_v4_rtos.h"

#define PCA_LEDOUT0                         0x02u
#define PCA_LEDOUT_ON                        0x01u
#define PCA_LEDOUT_AUTO_INCREMENT_CONTROL    0x82u
#define ASYNC_COMMAND_QUEUE_CAPACITY         2u
#define ASYNC_COMPLETION_RING_CAPACITY       4u
#define ASYNC_COMPLETION_RING_MASK           (ASYNC_COMPLETION_RING_CAPACITY - 1u)
#define SCHEDULED_COMPLETION_RING_CAPACITY   4u
#define SCHEDULED_COMPLETION_RING_MASK       (SCHEDULED_COMPLETION_RING_CAPACITY - 1u)
#define ASYNC_COMMAND_TIMEOUT_MS             1000u
#define ASYNC_TRANSACTION_TIMEOUT_MS         50u
#define ASYNC_QUIESCE_TIMEOUT_MS             100u
#define ASYNC_TASK_STACK_BYTES               NOCTI_V4_RTOS_STACK_LED_BYTES
/* PCA I2C stays on core 1, away from the Wi-Fi core. ADS is priority 23 and
 * may preempt this task for one SPI frame (budget 80 us). The LED task only
 * queues a prebuilt payload, blocks on the I2C completion notification, and
 * publishes one static completion record. It must not poll, log, or hold a
 * critical section across the transfer. */
#define ASYNC_TASK_PRIORITY                  NOCTI_V4_RTOS_PRIO_LED
#define ASYNC_TASK_CORE                      NOCTI_V4_RTOS_CORE_ACQ
#define ASYNC_TASK_STACK_WORDS \
    (ASYNC_TASK_STACK_BYTES / sizeof(StackType_t))

_Static_assert((ASYNC_TASK_STACK_BYTES % sizeof(StackType_t)) == 0u,
               "async LED stack byte count must align to StackType_t");
_Static_assert(ASYNC_TASK_PRIORITY < configMAX_PRIORITIES,
               "async LED priority must fit the FreeRTOS priority range");
#define ASYNC_TIMING_HISTOGRAM_BINS          5u

_Static_assert((ASYNC_COMPLETION_RING_CAPACITY &
                (ASYNC_COMPLETION_RING_CAPACITY - 1u)) == 0u,
               "async completion ring must be a power of two");
_Static_assert((SCHEDULED_COMPLETION_RING_CAPACITY &
                (SCHEDULED_COMPLETION_RING_CAPACITY - 1u)) == 0u,
               "scheduled completion ring must be a power of two");
_Static_assert(NOCTI_V4_I2C_SCAN_RUNTIME_HZ == 800000u,
               "V4-6.6 runtime qualification is locked to 800 kbit/s");

typedef enum {
    ASYNC_COMMAND_ENTER = 0,
    ASYNC_COMMAND_EXIT,
    ASYNC_COMMAND_ON_SINGLE,
    ASYNC_COMMAND_OFF_SINGLE,
    ASYNC_COMMAND_OFF_SINGLE_SCHEDULED,
    ASYNC_COMMAND_ON_BURST,
    ASYNC_COMMAND_OFF_BURST,
} async_command_type_t;

typedef enum {
    ASYNC_COMPLETION_TO_CALLER = 0,
    ASYNC_COMPLETION_TO_SCHEDULER,
} async_completion_route_t;

typedef struct {
    async_command_type_t type;
    async_completion_route_t completion_route;
    uint8_t source;
    uint8_t slot_index;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint64_t deadline_timestamp_us;
    uint64_t request_timestamp_us;
    uint64_t queue_timestamp_us;
    uint64_t dequeue_timestamp_us;
} async_command_t;

_Static_assert(sizeof(async_command_t) == 56u,
               "async command queue item size must remain audited");

typedef struct {
    uint32_t command_id;
    uint32_t generation;
    uint8_t source;
    uint8_t channel;
    uint8_t slot_index;
    nocti_v4_async_payload_type_t payload_type;
    uint8_t payload_length;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint64_t deadline_timestamp_us;
    uint64_t request_timestamp_us;
    uint64_t queue_timestamp_us;
    uint64_t dequeue_timestamp_us;
    uint64_t submit_timestamp_us;
} async_identity_t;

typedef enum {
    ASYNC_TIMING_DEADLINE_TO_REQUEST = 0,
    ASYNC_TIMING_REQUEST_TO_QUEUE,
    ASYNC_TIMING_DEADLINE_TO_QUEUE,
    ASYNC_TIMING_QUEUE_SEND_CALL,
    ASYNC_TIMING_QUEUE_TO_DEQUEUE,
    ASYNC_TIMING_DEQUEUE_TO_SUBMIT,
    ASYNC_TIMING_I2C_SUBMIT_CALL,
    ASYNC_TIMING_SUBMIT_TO_COMPLETE,
    ASYNC_TIMING_REQUEST_TO_COMPLETE,
    ASYNC_TIMING_DEADLINE_TO_COMPLETE,
    ASYNC_TIMING_STAGE_COUNT,
} async_timing_stage_t;

typedef enum {
    ASYNC_TIMING_CLASS_ON_7B = 0,
    ASYNC_TIMING_CLASS_NORMAL_OFF_2B,
    ASYNC_TIMING_CLASS_SAFETY_OFF_7B,
    ASYNC_TIMING_CLASS_DIAGNOSTIC_ON_2B,
    ASYNC_TIMING_CLASS_COUNT,
} async_timing_class_t;

typedef struct {
    uint32_t count;
    uint32_t minimum_us;
    uint32_t maximum_us;
    uint32_t average_us;
    uint64_t sum_us;
    uint32_t histogram[ASYNC_TIMING_HISTOGRAM_BINS];
} async_timing_stat_t;

typedef struct {
    async_identity_t identity;
    uint64_t complete_timestamp_us;
    i2c_master_event_t event;
    int32_t callback_core;
} async_completion_t;

typedef struct {
    nocti_v4_async_led_t *public_async;
    nocti_v4_board_t *board;
    nocti_v4_i2c_t *i2c;
    nocti_v4_led_t *led;
    nocti_v4_time_t *timebase;
    nocti_v4_health_t *health;
    TaskHandle_t task;
    TaskHandle_t scheduled_consumer_task;
    QueueHandle_t command_queue;
    SemaphoreHandle_t command_lock;
    SemaphoreHandle_t command_done;
    StaticQueue_t command_queue_storage;
    StaticSemaphore_t command_lock_storage;
    StaticSemaphore_t command_done_storage;
    StaticTask_t task_storage;
    /* ESP-IDF's static task API takes stack depth in bytes.  Express the
     * backing-array length through sizeof(StackType_t) so that contract stays
     * explicit; StackType_t is uint8_t on the ESP32-S3 IDF 5.5.1 port. */
    StackType_t task_stack[ASYNC_TASK_STACK_WORDS];
    uint8_t command_queue_buffer[ASYNC_COMMAND_QUEUE_CAPACITY * sizeof(async_command_t)];

    volatile bool callbacks_registered;
    volatile bool transaction_pending;
    volatile bool submissions_allowed;
    volatile bool completion_overflow_pending;
    volatile bool scheduled_completion_overflow_pending;
    volatile nocti_v4_pca_io_mode_t mode;
    volatile int32_t callback_core;
    int32_t task_core;
    int32_t init_core;
    volatile uint32_t completion_head;
    volatile uint32_t completion_tail;
    async_completion_t completion_ring[ASYNC_COMPLETION_RING_CAPACITY];
    volatile uint32_t scheduled_completion_head;
    volatile uint32_t scheduled_completion_tail;
    nocti_v4_async_led_completion_t
        scheduled_completion_ring[SCHEDULED_COMPLETION_RING_CAPACITY];

    uint32_t generation;
    uint32_t next_command_id;
    int32_t selected_source;
    async_identity_t inflight;
    async_identity_t last_identity;
    uint8_t single_on_payload[NOCTI_V4_OPTICAL_SOURCE_COUNT][2];
    uint8_t single_off_payload[NOCTI_V4_OPTICAL_SOURCE_COUNT][2];
    uint8_t burst_on_payload[NOCTI_V4_OPTICAL_SOURCE_COUNT][7];
    uint8_t burst_off_payload[7];
    bool payloads_valid;
    uint64_t latency_sum_us;
    uint32_t latency_count;
    async_timing_stat_t timing[ASYNC_TIMING_CLASS_COUNT][ASYNC_TIMING_STAGE_COUNT];
    uint64_t last_queue_exit_timestamp_us;
    uint64_t last_i2c_submit_return_timestamp_us;
    esp_err_t last_result;
    uint64_t last_complete_timestamp_us;
    int32_t last_i2c_event;
    int32_t last_callback_core;
} async_led_runtime_t;

static async_led_runtime_t s_runtime;
static bool s_runtime_claimed;

static void IRAM_ATTR counter_add(uint32_t *value, uint32_t amount)
{
    (void)__atomic_fetch_add(value, amount, __ATOMIC_RELAXED);
}

static void counter_store(uint32_t *value, uint32_t candidate)
{
    __atomic_store_n(value, candidate, __ATOMIC_RELEASE);
}

static void update_max(uint32_t *value, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void update_min(uint32_t *value, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while ((current == 0u || candidate < current) &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static uint32_t elapsed_us(uint64_t start, uint64_t end)
{
    if (end < start) {
        return 0u;
    }
    const uint64_t elapsed = end - start;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static uint8_t timing_histogram_bin(uint32_t duration_us)
{
    if (duration_us < 50u) return 0u;
    if (duration_us < 100u) return 1u;
    if (duration_us < 250u) return 2u;
    if (duration_us < 500u) return 3u;
    return 4u;
}

static bool payload_is_on(nocti_v4_async_payload_type_t payload_type)
{
    return payload_type == NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON ||
           payload_type == NOCTI_V4_ASYNC_PAYLOAD_BURST_ON;
}

static async_timing_class_t timing_class_for_payload(
    nocti_v4_async_payload_type_t payload_type)
{
    switch (payload_type) {
        case NOCTI_V4_ASYNC_PAYLOAD_BURST_ON:
            return ASYNC_TIMING_CLASS_ON_7B;
        case NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF:
            return ASYNC_TIMING_CLASS_NORMAL_OFF_2B;
        case NOCTI_V4_ASYNC_PAYLOAD_BURST_OFF:
            return ASYNC_TIMING_CLASS_SAFETY_OFF_7B;
        case NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON:
        default:
            return ASYNC_TIMING_CLASS_DIAGNOSTIC_ON_2B;
    }
}

static async_timing_class_t timing_class_for_command(async_command_type_t type)
{
    switch (type) {
        case ASYNC_COMMAND_ON_BURST:
            return ASYNC_TIMING_CLASS_ON_7B;
        case ASYNC_COMMAND_OFF_SINGLE:
        case ASYNC_COMMAND_OFF_SINGLE_SCHEDULED:
            return ASYNC_TIMING_CLASS_NORMAL_OFF_2B;
        case ASYNC_COMMAND_OFF_BURST:
            return ASYNC_TIMING_CLASS_SAFETY_OFF_7B;
        case ASYNC_COMMAND_ON_SINGLE:
        default:
            return ASYNC_TIMING_CLASS_DIAGNOSTIC_ON_2B;
    }
}

static void record_timing_stat(async_timing_stat_t *stat, uint32_t duration_us)
{
    const uint32_t count = stat->count + 1u;
    stat->sum_us += duration_us;
    if (count == 1u || duration_us < stat->minimum_us) {
        stat->minimum_us = duration_us;
    }
    if (duration_us > stat->maximum_us) {
        stat->maximum_us = duration_us;
    }
    stat->average_us = (uint32_t)(stat->sum_us / count);
    ++stat->histogram[timing_histogram_bin(duration_us)];
    __atomic_store_n(&stat->count, count, __ATOMIC_RELEASE);
}

static void record_command_timing(async_led_runtime_t *runtime,
                                  const async_identity_t *identity,
                                  uint64_t complete_timestamp_us)
{
    async_timing_stat_t *stats = runtime->timing[
        timing_class_for_payload(identity->payload_type)];
    record_timing_stat(&stats[ASYNC_TIMING_DEADLINE_TO_REQUEST],
                       elapsed_us(identity->deadline_timestamp_us,
                                  identity->request_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_REQUEST_TO_QUEUE],
                       elapsed_us(identity->request_timestamp_us,
                                  identity->queue_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_DEADLINE_TO_QUEUE],
                       elapsed_us(identity->deadline_timestamp_us,
                                  identity->queue_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_QUEUE_TO_DEQUEUE],
                       elapsed_us(identity->queue_timestamp_us,
                                  identity->dequeue_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_DEQUEUE_TO_SUBMIT],
                       elapsed_us(identity->dequeue_timestamp_us,
                                  identity->submit_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_SUBMIT_TO_COMPLETE],
                       elapsed_us(identity->submit_timestamp_us,
                                  complete_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_REQUEST_TO_COMPLETE],
                       elapsed_us(identity->request_timestamp_us,
                                  complete_timestamp_us));
    record_timing_stat(&stats[ASYNC_TIMING_DEADLINE_TO_COMPLETE],
                       elapsed_us(identity->deadline_timestamp_us,
                                  complete_timestamp_us));
}

static bool identity_matches(const async_identity_t *left, const async_identity_t *right)
{
    return left->command_id == right->command_id &&
           left->generation == right->generation &&
           left->source == right->source && left->channel == right->channel &&
           left->payload_type == right->payload_type &&
           left->submit_timestamp_us == right->submit_timestamp_us;
}

static const char *event_name(int32_t event)
{
    switch ((i2c_master_event_t)event) {
        case I2C_EVENT_DONE: return "I2C_EVENT_DONE";
        case I2C_EVENT_NACK: return "I2C_EVENT_NACK";
        case I2C_EVENT_TIMEOUT: return "I2C_EVENT_TIMEOUT";
        default: return "I2C_EVENT_UNKNOWN";
    }
}

const char *nocti_v4_pca_io_mode_name(nocti_v4_pca_io_mode_t mode)
{
    switch (mode) {
        case PCA_IO_MODE_SYNC: return "PCA_IO_MODE_SYNC";
        case PCA_IO_MODE_ENTERING: return "PCA_IO_MODE_ENTERING";
        case PCA_IO_MODE_ASYNC: return "PCA_IO_MODE_ASYNC";
        case PCA_IO_MODE_ASYNC_FAULTED: return "PCA_IO_MODE_ASYNC_FAULTED";
        case PCA_IO_MODE_EXITING: return "PCA_IO_MODE_EXITING";
        default: return "PCA_IO_MODE_UNKNOWN";
    }
}

const char *nocti_v4_async_payload_type_name(nocti_v4_async_payload_type_t type)
{
    switch (type) {
        case NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON: return "SINGLE_ON_2B";
        case NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF: return "SINGLE_OFF_2B";
        case NOCTI_V4_ASYNC_PAYLOAD_BURST_ON: return "BURST_ON_7B";
        case NOCTI_V4_ASYNC_PAYLOAD_BURST_OFF: return "BURST_OFF_7B";
        default: return "NONE";
    }
}

static void build_persistent_payloads(async_led_runtime_t *runtime)
{
    memset(runtime->single_on_payload, 0, sizeof(runtime->single_on_payload));
    memset(runtime->single_off_payload, 0, sizeof(runtime->single_off_payload));
    memset(runtime->burst_on_payload, 0, sizeof(runtime->burst_on_payload));
    memset(runtime->burst_off_payload, 0, sizeof(runtime->burst_off_payload));
    runtime->burst_off_payload[0] = PCA_LEDOUT_AUTO_INCREMENT_CONTROL;
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
        uint8_t channel = 0u;
        if (nocti_v4_led_resolve_source(source, &channel) != ESP_OK) {
            continue;
        }
        const uint8_t ledout_register = (uint8_t)(PCA_LEDOUT0 + channel / 4u);
        const uint8_t field = (uint8_t)(PCA_LEDOUT_ON << ((channel % 4u) * 2u));
        runtime->single_on_payload[source][0] = ledout_register;
        runtime->single_on_payload[source][1] = field;
        runtime->single_off_payload[source][0] = ledout_register;
        runtime->single_off_payload[source][1] = 0u;
        runtime->burst_on_payload[source][0] = PCA_LEDOUT_AUTO_INCREMENT_CONTROL;
        runtime->burst_on_payload[source][1u + channel / 4u] = field;
    }
}

static bool persistent_payloads_valid(const async_led_runtime_t *runtime)
{
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
        uint8_t expected_channel = 0u;
        if (nocti_v4_led_resolve_source(source, &expected_channel) != ESP_OK ||
            runtime->burst_on_payload[source][0] != PCA_LEDOUT_AUTO_INCREMENT_CONTROL) {
            return false;
        }
        uint32_t on_fields = 0u;
        for (uint8_t channel = 0u; channel < 24u; ++channel) {
            const uint8_t byte = runtime->burst_on_payload[source][1u + channel / 4u];
            const uint8_t field = (uint8_t)((byte >> ((channel % 4u) * 2u)) & 0x03u);
            if (field != 0u) {
                ++on_fields;
                if (channel != expected_channel || field != PCA_LEDOUT_ON) {
                    return false;
                }
            }
        }
        if (on_fields != 1u || runtime->single_on_payload[source][0] !=
                                  (uint8_t)(PCA_LEDOUT0 + expected_channel / 4u) ||
            runtime->single_on_payload[source][1] !=
                (uint8_t)(PCA_LEDOUT_ON << ((expected_channel % 4u) * 2u)) ||
            runtime->single_off_payload[source][0] !=
                (uint8_t)(PCA_LEDOUT0 + expected_channel / 4u) ||
            runtime->single_off_payload[source][1] != 0u) {
            return false;
        }
    }
    for (uint8_t index = 1u; index < sizeof(runtime->burst_off_payload); ++index) {
        if (runtime->burst_off_payload[index] != 0u) {
            return false;
        }
    }
    return true;
}

static void reset_async_counters(async_led_runtime_t *runtime)
{
#define RESET_ASYNC(field) counter_store(&runtime->health->field, 0u)
    RESET_ASYNC(async_commands_submitted);
    RESET_ASYNC(async_commands_completed);
    RESET_ASYNC(async_submit_errors);
    RESET_ASYNC(async_command_queue_send_errors);
    RESET_ASYNC(async_command_queue_depth_max);
    RESET_ASYNC(async_on_pending_at_submit);
    RESET_ASYNC(async_off_pending_at_submit);
    RESET_ASYNC(async_completion_errors);
    RESET_ASYNC(async_completion_ring_overflow);
    RESET_ASYNC(async_scheduler_completion_ring_overflow);
    RESET_ASYNC(async_stale_completion);
    RESET_ASYNC(async_single_on_count);
    RESET_ASYNC(async_single_off_count);
    RESET_ASYNC(async_burst_on_count);
    RESET_ASYNC(async_burst_off_count);
    RESET_ASYNC(async_latency_min_us);
    RESET_ASYNC(async_latency_max_us);
    RESET_ASYNC(async_latency_avg_us);
    RESET_ASYNC(async_force_off_failures);
    RESET_ASYNC(async_exit_failures);
    RESET_ASYNC(async_normal_off_invariant_failures);
#undef RESET_ASYNC
    runtime->latency_sum_us = 0u;
    runtime->latency_count = 0u;
    runtime->last_queue_exit_timestamp_us = 0u;
    runtime->last_i2c_submit_return_timestamp_us = 0u;
    memset(runtime->timing, 0, sizeof(runtime->timing));
}

static void clear_completion_ring(async_led_runtime_t *runtime)
{
    __atomic_store_n(&runtime->completion_tail,
                     __atomic_load_n(&runtime->completion_head, __ATOMIC_ACQUIRE),
                     __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->completion_overflow_pending, false, __ATOMIC_RELEASE);
}

static bool completion_pop(async_led_runtime_t *runtime, async_completion_t *out_completion)
{
    const uint32_t tail = __atomic_load_n(&runtime->completion_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->completion_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out_completion = runtime->completion_ring[tail & ASYNC_COMPLETION_RING_MASK];
    __atomic_store_n(&runtime->completion_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static bool IRAM_ATTR completion_push_from_isr(async_led_runtime_t *runtime,
                                               const async_completion_t *completion)
{
    const uint32_t head = __atomic_load_n(&runtime->completion_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->completion_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= ASYNC_COMPLETION_RING_CAPACITY) {
        counter_add(&runtime->health->async_completion_ring_overflow, 1u);
        __atomic_store_n(&runtime->completion_overflow_pending, true, __ATOMIC_RELEASE);
        return false;
    }
    runtime->completion_ring[head & ASYNC_COMPLETION_RING_MASK] = *completion;
    __atomic_store_n(&runtime->completion_head, head + 1u, __ATOMIC_RELEASE);
    return true;
}

static void clear_scheduled_completion_ring(async_led_runtime_t *runtime)
{
    __atomic_store_n(&runtime->scheduled_completion_tail,
                     __atomic_load_n(&runtime->scheduled_completion_head,
                                     __ATOMIC_ACQUIRE),
                     __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->scheduled_completion_overflow_pending, false,
                     __ATOMIC_RELEASE);
}

/* LED-task producer -> scheduler-task consumer. This is intentionally a
 * second ring: the ISR ring is consumed only by the LED owner so callback
 * validation and recovery never execute in interrupt context. */
static bool scheduled_completion_push(
    async_led_runtime_t *runtime,
    const nocti_v4_async_led_completion_t *completion)
{
    const uint32_t head = __atomic_load_n(&runtime->scheduled_completion_head,
                                           __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->scheduled_completion_tail,
                                           __ATOMIC_ACQUIRE);
    if (head - tail >= SCHEDULED_COMPLETION_RING_CAPACITY) {
        counter_add(&runtime->health->async_scheduler_completion_ring_overflow,
                    1u);
        __atomic_store_n(&runtime->scheduled_completion_overflow_pending, true,
                         __ATOMIC_RELEASE);
        return false;
    }
    runtime->scheduled_completion_ring[head & SCHEDULED_COMPLETION_RING_MASK] =
        *completion;
    __atomic_store_n(&runtime->scheduled_completion_head, head + 1u,
                     __ATOMIC_RELEASE);
    return true;
}

static bool IRAM_ATTR i2c_done_callback(i2c_master_dev_handle_t device,
                                        const i2c_master_event_data_t *event,
                                        void *user_data)
{
    (void)device;
    async_led_runtime_t *runtime = (async_led_runtime_t *)user_data;
    BaseType_t higher_priority_woken = pdFALSE;
    if (runtime == NULL || !__atomic_load_n(&runtime->transaction_pending,
                                            __ATOMIC_ACQUIRE)) {
        return false;
    }
    async_completion_t completion = {0};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    completion.identity = runtime->inflight;
    completion.event = event != NULL ? event->event : I2C_EVENT_TIMEOUT;
    completion.callback_core = xPortGetCoreID();
    (void)nocti_v4_time_now_us_isr(runtime->timebase, &completion.complete_timestamp_us);
    __atomic_store_n(&runtime->callback_core, completion.callback_core, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->health->async_callback_core, completion.callback_core,
                     __ATOMIC_RELEASE);
    (void)completion_push_from_isr(runtime, &completion);
    __atomic_store_n(&runtime->transaction_pending, false, __ATOMIC_RELEASE);
    if (runtime->task != NULL) {
        vTaskNotifyGiveFromISR(runtime->task, &higher_priority_woken);
    }
    /* IDF6.1 i2c_master.c discards the callback return value. Preserve V4's
     * immediate wake semantics rather than waiting for the next RTOS tick. */
    if (higher_priority_woken==pdTRUE) portYIELD_FROM_ISR();
    return higher_priority_woken == pdTRUE;
}

static esp_err_t force_reset(async_led_runtime_t *runtime, esp_err_t cause)
{
    const esp_err_t reset_result = nocti_v4_led_reset_hold(runtime->led);
    if (reset_result != ESP_OK) {
        counter_add(&runtime->health->async_force_off_failures, 1u);
    }
    __atomic_store_n(&runtime->submissions_allowed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->mode, PCA_IO_MODE_ASYNC_FAULTED, __ATOMIC_RELEASE);
    runtime->selected_source = -1;
    runtime->last_result = cause;
    return cause;
}

static esp_err_t unregister_callback(async_led_runtime_t *runtime)
{
    if (!__atomic_load_n(&runtime->callbacks_registered, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }
    const i2c_master_event_callbacks_t callbacks = {
        .on_trans_done = NULL,
    };
    const esp_err_t err = i2c_master_register_event_callbacks(runtime->i2c->pca,
                                                               &callbacks, NULL);
    if (err == ESP_OK) {
        __atomic_store_n(&runtime->callbacks_registered, false, __ATOMIC_RELEASE);
    }
    return err;
}

static esp_err_t quiesce_async_bus(async_led_runtime_t *runtime)
{
    if (runtime->i2c == NULL || !runtime->i2c->initialized || runtime->i2c->bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t wait_result = i2c_master_bus_wait_all_done(
        runtime->i2c->bus, ASYNC_QUIESCE_TIMEOUT_MS);
    if (wait_result != ESP_OK) {
        return wait_result;
    }
    /* Queue drain alone does not prove completion of IDF6.1's direct async
     * transaction. Retain callback, handles and persistent payloads if unknown. */
    if (__atomic_load_n(&runtime->transaction_pending,__ATOMIC_ACQUIRE))
        return ESP_ERR_INVALID_STATE;
    return unregister_callback(runtime);
}

static esp_err_t recreate_sync_and_verify(async_led_runtime_t *runtime)
{
    esp_err_t err = quiesce_async_bus(runtime);
    if (err != ESP_OK) {
        return err;
    }
    err = nocti_v4_i2c_deinit(runtime->i2c);
    if (err != ESP_OK) {
        return err;
    }
    err = nocti_v4_i2c_init_sync(runtime->i2c, runtime->health);
    if (err != ESP_OK) {
        return err;
    }
    return nocti_v4_led_reinit(runtime->led);
}

static void set_last_submission_error(async_led_runtime_t *runtime,
                                      const async_identity_t *identity,
                                      esp_err_t result)
{
    runtime->last_identity = *identity;
    runtime->last_complete_timestamp_us = identity->submit_timestamp_us;
    runtime->last_i2c_event = -1;
    runtime->last_callback_core = -1;
    runtime->last_result = result;
}

static esp_err_t wait_for_completion(async_led_runtime_t *runtime,
                                     const async_identity_t *identity)
{
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(ASYNC_TRANSACTION_TIMEOUT_MS);
    for (;;) {
        async_completion_t completion;
        while (completion_pop(runtime, &completion)) {
            if (!identity_matches(&completion.identity, identity)) {
                counter_add(&runtime->health->async_stale_completion, 1u);
                continue;
            }
            runtime->last_identity = completion.identity;
            runtime->last_complete_timestamp_us = completion.complete_timestamp_us;
            runtime->last_i2c_event = completion.event;
            runtime->last_callback_core = completion.callback_core;
            const uint32_t latency_us = completion.complete_timestamp_us >=
                                        identity->submit_timestamp_us ?
                (uint32_t)(completion.complete_timestamp_us - identity->submit_timestamp_us) : 0u;
            runtime->latency_sum_us += latency_us;
            ++runtime->latency_count;
            update_min(&runtime->health->async_latency_min_us, latency_us);
            update_max(&runtime->health->async_latency_max_us, latency_us);
            counter_store(&runtime->health->async_latency_avg_us,
                          (uint32_t)(runtime->latency_sum_us / runtime->latency_count));
            record_command_timing(runtime, identity,
                                  completion.complete_timestamp_us);
            counter_add(&runtime->health->async_commands_completed, 1u);
            if (completion.event != I2C_EVENT_DONE) {
                counter_add(&runtime->health->async_completion_errors, 1u);
                return force_reset(runtime, ESP_FAIL);
            }
            runtime->last_result = ESP_OK;
            return ESP_OK;
        }
        if (__atomic_exchange_n(&runtime->completion_overflow_pending, false,
                                __ATOMIC_ACQ_REL)) {
            return force_reset(runtime, ESP_ERR_NO_MEM);
        }
        const TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) {
            return force_reset(runtime, ESP_ERR_TIMEOUT);
        }
        (void)ulTaskNotifyTake(pdTRUE, timeout - elapsed);
    }
}

static esp_err_t submit_payload(async_led_runtime_t *runtime,
                                const async_command_t *command,
                                nocti_v4_async_payload_type_t payload_type,
                                const uint8_t *payload, uint8_t payload_length)
{
    const bool on_payload = payload_is_on(payload_type);
    const bool pending_at_submit = __atomic_load_n(&runtime->transaction_pending,
                                                    __ATOMIC_ACQUIRE);
    if (pending_at_submit) {
        counter_add(on_payload ? &runtime->health->async_on_pending_at_submit :
                                 &runtime->health->async_off_pending_at_submit,
                    1u);
    }
    if (__atomic_load_n(&runtime->mode, __ATOMIC_ACQUIRE) != PCA_IO_MODE_ASYNC ||
        !__atomic_load_n(&runtime->submissions_allowed, __ATOMIC_ACQUIRE) ||
        pending_at_submit ||
        command == NULL || payload == NULL ||
        (payload_length != 2u && payload_length != 7u)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t channel = 0u;
    if (nocti_v4_led_resolve_source(command->source, &channel) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t submit_timestamp_us = 0u;
    const esp_err_t time_result = nocti_v4_time_now_us(runtime->timebase,
                                                         &submit_timestamp_us);
    if (time_result != ESP_OK) {
        return time_result;
    }
    async_identity_t identity = {
        .command_id = ++runtime->next_command_id,
        .generation = runtime->generation,
        .source = command->source,
        .channel = channel,
        .slot_index = command->slot_index,
        .payload_type = payload_type,
        .payload_length = payload_length,
        .session_epoch = command->session_epoch,
        .frame_sequence = command->frame_sequence,
        .slot_epoch = command->slot_epoch,
        .deadline_timestamp_us = command->deadline_timestamp_us,
        .request_timestamp_us = command->request_timestamp_us,
        .queue_timestamp_us = command->queue_timestamp_us,
        .dequeue_timestamp_us = command->dequeue_timestamp_us,
        .submit_timestamp_us = submit_timestamp_us,
    };
    runtime->inflight = identity;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&runtime->transaction_pending, true, __ATOMIC_RELEASE);
    counter_add(&runtime->health->async_commands_submitted, 1u);
    const esp_err_t submit_result = i2c_master_transmit(runtime->i2c->pca, payload,
                                                         payload_length,
                                                         ASYNC_TRANSACTION_TIMEOUT_MS);
    uint64_t submit_return_timestamp_us = submit_timestamp_us;
    (void)nocti_v4_time_now_us(runtime->timebase, &submit_return_timestamp_us);
    runtime->last_i2c_submit_return_timestamp_us = submit_return_timestamp_us;
    record_timing_stat(&runtime->timing[timing_class_for_payload(payload_type)]
                                       [ASYNC_TIMING_I2C_SUBMIT_CALL],
                       elapsed_us(submit_timestamp_us,
                                  submit_return_timestamp_us));
    if (submit_result != ESP_OK) {
        if (__atomic_exchange_n(&runtime->transaction_pending, false,
                                __ATOMIC_ACQ_REL)) {
            counter_add(&runtime->health->async_submit_errors, 1u);
            set_last_submission_error(runtime, &identity, submit_result);
            return force_reset(runtime, submit_result);
        }
    }
    return wait_for_completion(runtime, &identity);
}

static esp_err_t enter_async(async_led_runtime_t *runtime)
{
    nocti_v4_i2c_fast_qual_status_t fast_qualification;
    nocti_v4_led_get_fast_i2c_qualification(&fast_qualification);
    if (__atomic_load_n(&runtime->mode, __ATOMIC_ACQUIRE) != PCA_IO_MODE_SYNC ||
        !runtime->led->initialized || !runtime->i2c->initialized ||
        runtime->i2c->queue_depth != NOCTI_V4_I2C_TRANS_QUEUE_DEPTH ||
        runtime->i2c->clock_hz != NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ ||
        fast_qualification.state != NOCTI_V4_I2C_FAST_QUAL_PASS ||
        !fast_qualification.restored_sync_configured ||
        !nocti_v4_time_ready(runtime->timebase)) {
        return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&runtime->mode, PCA_IO_MODE_ENTERING, __ATOMIC_RELEASE);
    esp_err_t err = nocti_v4_led_all_off(runtime->led);
    if (err != ESP_OK) {
        __atomic_store_n(&runtime->mode, PCA_IO_MODE_SYNC, __ATOMIC_RELEASE);
        return err;
    }
    runtime->selected_source = -1;
    build_persistent_payloads(runtime);
    runtime->payloads_valid = persistent_payloads_valid(runtime);
    if (!runtime->payloads_valid) {
        __atomic_store_n(&runtime->mode, PCA_IO_MODE_SYNC, __ATOMIC_RELEASE);
        return ESP_ERR_INVALID_STATE;
    }
    clear_completion_ring(runtime);
    clear_scheduled_completion_ring(runtime);
    err = nocti_v4_i2c_deinit(runtime->i2c);
    if (err == ESP_OK) {
        err = nocti_v4_i2c_init_at_speed(
            runtime->i2c, runtime->health,
            NOCTI_V4_I2C_ASYNC_TRANS_QUEUE_DEPTH,
            NOCTI_V4_I2C_SCAN_RUNTIME_HZ);
    }
    if (err == ESP_OK) {
        const i2c_master_event_callbacks_t callbacks = {
            .on_trans_done = i2c_done_callback,
        };
        err = i2c_master_register_event_callbacks(runtime->i2c->pca, &callbacks,
                                                   runtime);
        if (err == ESP_OK) {
            __atomic_store_n(&runtime->callbacks_registered, true, __ATOMIC_RELEASE);
        }
    }
    if (err != ESP_OK) {
        (void)nocti_v4_led_reset_hold(runtime->led);
        if (runtime->i2c->initialized && runtime->i2c->queue_depth ==
                                              NOCTI_V4_I2C_ASYNC_TRANS_QUEUE_DEPTH &&
            recreate_sync_and_verify(runtime) == ESP_OK) {
            __atomic_store_n(&runtime->mode, PCA_IO_MODE_SYNC, __ATOMIC_RELEASE);
        } else {
            counter_add(&runtime->health->async_exit_failures, 1u);
            __atomic_store_n(&runtime->mode, PCA_IO_MODE_ASYNC_FAULTED,
                             __ATOMIC_RELEASE);
        }
        return err;
    }
    reset_async_counters(runtime);
    ++runtime->generation;
    runtime->init_core = xPortGetCoreID();
    __atomic_store_n(&runtime->health->async_init_core, runtime->init_core,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->health->async_led_task_core, runtime->task_core,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->callback_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->submissions_allowed, true, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->mode, PCA_IO_MODE_ASYNC, __ATOMIC_RELEASE);
    runtime->last_result = ESP_OK;
    return ESP_OK;
}

static esp_err_t exit_async(async_led_runtime_t *runtime)
{
    const nocti_v4_pca_io_mode_t prior = __atomic_load_n(&runtime->mode,
                                                          __ATOMIC_ACQUIRE);
    if (prior != PCA_IO_MODE_ASYNC && prior != PCA_IO_MODE_ASYNC_FAULTED) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (prior == PCA_IO_MODE_ASYNC) {
        uint64_t now_us = 0u;
        (void)nocti_v4_time_now_us(runtime->timebase, &now_us);
        const async_command_t exit_off = {
            .type = ASYNC_COMMAND_OFF_BURST,
            .source = 0u,
            .deadline_timestamp_us = now_us,
            .request_timestamp_us = now_us,
            .queue_timestamp_us = now_us,
            .dequeue_timestamp_us = now_us,
        };
        err = submit_payload(runtime, &exit_off,
                             NOCTI_V4_ASYNC_PAYLOAD_BURST_OFF,
                             runtime->burst_off_payload,
                             (uint8_t)sizeof(runtime->burst_off_payload));
        if (err != ESP_OK) {
            (void)nocti_v4_led_reset_hold(runtime->led);
        }
    }
    __atomic_store_n(&runtime->submissions_allowed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->mode, PCA_IO_MODE_EXITING, __ATOMIC_RELEASE);
    const esp_err_t restore_result = recreate_sync_and_verify(runtime);
    if (restore_result != ESP_OK) {
        counter_add(&runtime->health->async_exit_failures, 1u);
        (void)nocti_v4_led_reset_hold(runtime->led);
        __atomic_store_n(&runtime->mode, PCA_IO_MODE_ASYNC_FAULTED, __ATOMIC_RELEASE);
        return restore_result;
    }
    runtime->selected_source = -1;
    __atomic_store_n(&runtime->mode, PCA_IO_MODE_SYNC, __ATOMIC_RELEASE);
    return err;
}

static esp_err_t execute_async_command(async_led_runtime_t *runtime,
                                       const async_command_t *command)
{
    switch (command->type) {
        case ASYNC_COMMAND_ENTER:
            return enter_async(runtime);
        case ASYNC_COMMAND_EXIT:
            return exit_async(runtime);
        case ASYNC_COMMAND_ON_SINGLE:
            if (runtime->selected_source >= 0) return ESP_ERR_INVALID_STATE;
            if (command->source >= NOCTI_V4_OPTICAL_SOURCE_COUNT) return ESP_ERR_INVALID_ARG;
            {
                const esp_err_t err = submit_payload(runtime, command,
                                                     NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON,
                                                     runtime->single_on_payload[command->source], 2u);
                if (err == ESP_OK) {
                    runtime->selected_source = command->source;
                    counter_add(&runtime->health->async_single_on_count, 1u);
                }
                return err;
            }
        case ASYNC_COMMAND_OFF_SINGLE:
            if (runtime->selected_source < 0) return ESP_ERR_INVALID_STATE;
            {
                const uint8_t source = (uint8_t)runtime->selected_source;
                async_command_t selected_command = *command;
                selected_command.source = source;
                const esp_err_t err = submit_payload(runtime, &selected_command,
                                                     NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF,
                                                     runtime->single_off_payload[source], 2u);
                if (err == ESP_OK) {
                    runtime->selected_source = -1;
                    counter_add(&runtime->health->async_single_off_count, 1u);
                }
                return err;
            }
        case ASYNC_COMMAND_OFF_SINGLE_SCHEDULED:
            /* V4-6.5 normal-slot short OFF is valid only after the matching,
             * completed seven-byte one-hot ON. A failed invariant is handed
             * back to the scheduler, whose fault path retains strong 7-byte
             * ALL_OFF plus RESET# fail-safe policy. */
            if (!runtime->payloads_valid ||
                command->source >= NOCTI_V4_OPTICAL_SOURCE_COUNT ||
                runtime->selected_source != (int32_t)command->source ||
                runtime->last_result != ESP_OK ||
                runtime->last_identity.command_id == 0u ||
                runtime->last_identity.command_id != runtime->next_command_id ||
                runtime->last_identity.generation != runtime->generation ||
                runtime->last_identity.source != command->source ||
                runtime->last_identity.payload_type !=
                    NOCTI_V4_ASYNC_PAYLOAD_BURST_ON ||
                runtime->last_identity.payload_length != 7u) {
                counter_add(&runtime->health->async_normal_off_invariant_failures,
                            1u);
                return ESP_ERR_INVALID_STATE;
            }
            {
                const esp_err_t err = submit_payload(
                    runtime, command, NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF,
                    runtime->single_off_payload[command->source], 2u);
                if (err == ESP_OK) {
                    runtime->selected_source = -1;
                    counter_add(&runtime->health->async_single_off_count, 1u);
                }
                return err;
            }
        case ASYNC_COMMAND_ON_BURST:
            if (runtime->selected_source >= 0) return ESP_ERR_INVALID_STATE;
            if (command->source >= NOCTI_V4_OPTICAL_SOURCE_COUNT) return ESP_ERR_INVALID_ARG;
            {
                const esp_err_t err = submit_payload(runtime, command,
                                                     NOCTI_V4_ASYNC_PAYLOAD_BURST_ON,
                                                     runtime->burst_on_payload[command->source], 7u);
                if (err == ESP_OK) {
                    runtime->selected_source = command->source;
                    counter_add(&runtime->health->async_burst_on_count, 1u);
                }
                return err;
            }
        case ASYNC_COMMAND_OFF_BURST:
            {
                const esp_err_t err = submit_payload(runtime, command,
                                                     NOCTI_V4_ASYNC_PAYLOAD_BURST_OFF,
                                                     runtime->burst_off_payload, 7u);
                if (err == ESP_OK) {
                    runtime->selected_source = -1;
                    counter_add(&runtime->health->async_burst_off_count, 1u);
                }
                return err;
            }
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static void publish_scheduled_completion(async_led_runtime_t *runtime,
                                         const async_command_t *command,
                                         esp_err_t result)
{
    nocti_v4_async_led_completion_t completion = {
        .result = result,
        .session_epoch = command->session_epoch,
        .frame_sequence = command->frame_sequence,
        .slot_epoch = command->slot_epoch,
        .slot_index = command->slot_index,
        .source = command->source,
        .deadline_timestamp_us = command->deadline_timestamp_us,
        .request_timestamp_us = command->request_timestamp_us,
        .queue_timestamp_us = command->queue_timestamp_us,
        .dequeue_timestamp_us = command->dequeue_timestamp_us,
        .i2c_event = runtime->last_i2c_event,
        .callback_core = runtime->last_callback_core,
    };
    const async_identity_t *identity = &runtime->last_identity;
    if (identity->generation == runtime->generation &&
        identity->source == command->source &&
        identity->session_epoch == command->session_epoch &&
        identity->frame_sequence == command->frame_sequence &&
        identity->slot_epoch == command->slot_epoch &&
        identity->slot_index == command->slot_index &&
        identity->deadline_timestamp_us == command->deadline_timestamp_us &&
        identity->request_timestamp_us == command->request_timestamp_us) {
        completion.command_id = identity->command_id;
        completion.generation = identity->generation;
        completion.channel = identity->channel;
        completion.payload_type = identity->payload_type;
        completion.payload_length = identity->payload_length;
        completion.submit_timestamp_us = identity->submit_timestamp_us;
        completion.complete_timestamp_us = runtime->last_complete_timestamp_us;
    } else {
        completion.generation = runtime->generation;
        completion.payload_type = command->type ==
                ASYNC_COMMAND_OFF_SINGLE_SCHEDULED ?
            NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF :
            NOCTI_V4_ASYNC_PAYLOAD_BURST_ON;
        completion.payload_length = command->type ==
                ASYNC_COMMAND_OFF_SINGLE_SCHEDULED ? 2u : 7u;
        (void)nocti_v4_led_resolve_source(command->source,
                                           &completion.channel);
        (void)nocti_v4_time_now_us(runtime->timebase,
                                    &completion.complete_timestamp_us);
    }
    if (!scheduled_completion_push(runtime, &completion)) {
        (void)force_reset(runtime, ESP_ERR_NO_MEM);
    }
    TaskHandle_t consumer = __atomic_load_n(&runtime->scheduled_consumer_task,
                                             __ATOMIC_ACQUIRE);
    if (consumer != NULL) {
        xTaskNotifyGive(consumer);
    }
}

static void async_led_task(void *argument)
{
    async_led_runtime_t *runtime = (async_led_runtime_t *)argument;
    runtime->task_core = xPortGetCoreID();
    __atomic_store_n(&runtime->health->async_led_task_core, runtime->task_core,
                     __ATOMIC_RELEASE);
    nocti_v4_task_wdt_subscribe();
    for (;;) {
        async_command_t command;
        nocti_v4_task_wdt_kick();
        if (xQueueReceive(runtime->command_queue, &command,
                          pdMS_TO_TICKS(NOCTI_V4_RTOS_WDT_IDLE_WAIT_MS)) == pdTRUE) {
            (void)nocti_v4_time_now_us(runtime->timebase,
                                       &command.dequeue_timestamp_us);
            const esp_err_t result = execute_async_command(runtime, &command);
            runtime->last_result = result;
            if (command.completion_route == ASYNC_COMPLETION_TO_SCHEDULER) {
                publish_scheduled_completion(runtime, &command, result);
            } else {
                (void)xSemaphoreGive(runtime->command_done);
            }
        }
    }
}

static esp_err_t submit_command_timed(nocti_v4_async_led_t *async_led,
                                      async_command_type_t type, uint8_t source,
                                      uint64_t deadline_timestamp_us,
                                      uint64_t request_timestamp_us,
                                      bool nonblocking_handoff)
{
    if (async_led == NULL || !async_led->initialized || async_led->runtime == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    async_led_runtime_t *runtime = (async_led_runtime_t *)async_led->runtime;
    if (request_timestamp_us == 0u) {
        const esp_err_t time_result = nocti_v4_time_now_us(
            runtime->timebase, &request_timestamp_us);
        if (time_result != ESP_OK) {
            return time_result;
        }
    }
    if (deadline_timestamp_us == 0u) {
        deadline_timestamp_us = request_timestamp_us;
    }
    const TickType_t handoff_wait = nonblocking_handoff ? 0u :
                                    pdMS_TO_TICKS(ASYNC_COMMAND_TIMEOUT_MS);
    if (xSemaphoreTake(runtime->command_lock, handoff_wait) != pdTRUE) {
        counter_add(&runtime->health->async_command_queue_send_errors, 1u);
        return ESP_ERR_TIMEOUT;
    }
    (void)xSemaphoreTake(runtime->command_done, 0u);
    async_command_t command = {
        .type = type,
        .source = source,
        .deadline_timestamp_us = deadline_timestamp_us,
        .request_timestamp_us = request_timestamp_us,
    };
    esp_err_t result = ESP_OK;
    const esp_err_t queue_time_result = nocti_v4_time_now_us(
        runtime->timebase, &command.queue_timestamp_us);
    if (queue_time_result != ESP_OK) {
        result = queue_time_result;
    }
    if (result == ESP_OK) {
        const uint32_t queue_depth_before =
            (uint32_t)uxQueueMessagesWaiting(runtime->command_queue);
        const BaseType_t sent = xQueueSend(runtime->command_queue, &command,
                                           handoff_wait);
        uint64_t queue_exit_timestamp_us = command.queue_timestamp_us;
        (void)nocti_v4_time_now_us(runtime->timebase, &queue_exit_timestamp_us);
        if (sent != pdTRUE) {
            counter_add(&runtime->health->async_command_queue_send_errors, 1u);
            result = ESP_ERR_TIMEOUT;
        } else if (xSemaphoreTake(runtime->command_done,
                                  pdMS_TO_TICKS(ASYNC_COMMAND_TIMEOUT_MS)) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
        } else {
            result = runtime->last_result;
        }
        if (sent == pdTRUE) {
            runtime->last_queue_exit_timestamp_us = queue_exit_timestamp_us;
            if (type == ASYNC_COMMAND_ON_SINGLE || type == ASYNC_COMMAND_ON_BURST ||
                type == ASYNC_COMMAND_OFF_SINGLE ||
                type == ASYNC_COMMAND_OFF_SINGLE_SCHEDULED ||
                type == ASYNC_COMMAND_OFF_BURST) {
                async_timing_stat_t *stats = runtime->timing[
                    timing_class_for_command(type)];
                record_timing_stat(&stats[ASYNC_TIMING_QUEUE_SEND_CALL],
                                   elapsed_us(command.queue_timestamp_us,
                                              queue_exit_timestamp_us));
            }
            update_max(&runtime->health->async_command_queue_depth_max,
                       queue_depth_before + 1u);
        }
    }
    (void)xSemaphoreGive(runtime->command_lock);
    return result;
}

static esp_err_t submit_scheduled_command(
    nocti_v4_async_led_t *async_led, async_command_type_t type, uint8_t source,
    uint32_t session_epoch, uint32_t frame_sequence, uint32_t slot_epoch,
    uint8_t slot_index, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us)
{
    if (async_led == NULL || !async_led->initialized ||
        async_led->runtime == NULL ||
        (type != ASYNC_COMMAND_ON_BURST &&
         type != ASYNC_COMMAND_OFF_SINGLE_SCHEDULED)) {
        return ESP_ERR_INVALID_ARG;
    }
    async_led_runtime_t *runtime = (async_led_runtime_t *)async_led->runtime;
    if (__atomic_load_n(&runtime->scheduled_consumer_task, __ATOMIC_ACQUIRE) ==
            NULL ||
        __atomic_load_n(&runtime->mode, __ATOMIC_ACQUIRE) != PCA_IO_MODE_ASYNC ||
        !__atomic_load_n(&runtime->submissions_allowed, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&runtime->transaction_pending, __ATOMIC_ACQUIRE) ||
        source >= NOCTI_V4_OPTICAL_SOURCE_COUNT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(runtime->command_lock, 0u) != pdTRUE) {
        counter_add(&runtime->health->async_command_queue_send_errors, 1u);
        return ESP_ERR_TIMEOUT;
    }
    async_command_t command = {
        .type = type,
        .completion_route = ASYNC_COMPLETION_TO_SCHEDULER,
        .source = source,
        .slot_index = slot_index,
        .session_epoch = session_epoch,
        .frame_sequence = frame_sequence,
        .slot_epoch = slot_epoch,
        .deadline_timestamp_us = deadline_timestamp_us,
        .request_timestamp_us = request_timestamp_us,
    };
    esp_err_t result = nocti_v4_time_now_us(runtime->timebase,
                                             &command.queue_timestamp_us);
    if (result == ESP_OK) {
        const uint32_t queue_depth_before =
            (uint32_t)uxQueueMessagesWaiting(runtime->command_queue);
        if (queue_depth_before != 0u ||
            xQueueSend(runtime->command_queue, &command, 0u) != pdTRUE) {
            counter_add(&runtime->health->async_command_queue_send_errors, 1u);
            result = ESP_ERR_TIMEOUT;
        } else {
            uint64_t queue_exit_timestamp_us = command.queue_timestamp_us;
            (void)nocti_v4_time_now_us(runtime->timebase,
                                       &queue_exit_timestamp_us);
            runtime->last_queue_exit_timestamp_us = queue_exit_timestamp_us;
            record_timing_stat(
                &runtime->timing[timing_class_for_command(type)]
                                [ASYNC_TIMING_QUEUE_SEND_CALL],
                elapsed_us(command.queue_timestamp_us,
                           queue_exit_timestamp_us));
            update_max(&runtime->health->async_command_queue_depth_max, 1u);
        }
    }
    (void)xSemaphoreGive(runtime->command_lock);
    return result;
}

static esp_err_t submit_command(nocti_v4_async_led_t *async_led,
                                async_command_type_t type, uint8_t source)
{
    return submit_command_timed(async_led, type, source, 0u, 0u, false);
}

esp_err_t nocti_v4_async_led_init(nocti_v4_async_led_t *async_led,
                                  nocti_v4_board_t *board,
                                  nocti_v4_i2c_t *i2c,
                                  nocti_v4_led_t *led,
                                  nocti_v4_time_t *timebase,
                                  nocti_v4_health_t *health)
{
    if (async_led == NULL || board == NULL || i2c == NULL || led == NULL ||
        timebase == NULL || health == NULL || s_runtime_claimed) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(async_led, 0, sizeof(*async_led));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime_claimed = true;
    async_led_runtime_t *runtime = &s_runtime;
    runtime->public_async = async_led;
    runtime->board = board;
    runtime->i2c = i2c;
    runtime->led = led;
    runtime->timebase = timebase;
    runtime->health = health;
    runtime->selected_source = -1;
    runtime->callback_core = -1;
    runtime->task_core = -1;
    runtime->init_core = -1;
    runtime->mode = PCA_IO_MODE_SYNC;
    runtime->last_result = ESP_OK;
    __atomic_store_n(&health->async_callback_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&health->async_init_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&health->async_led_task_core, -1, __ATOMIC_RELEASE);
    build_persistent_payloads(runtime);
    runtime->payloads_valid = persistent_payloads_valid(runtime);
    if (!runtime->payloads_valid) {
        s_runtime_claimed = false;
        return ESP_FAIL;
    }
    runtime->command_queue = xQueueCreateStatic(ASYNC_COMMAND_QUEUE_CAPACITY,
                                                 sizeof(async_command_t),
                                                 runtime->command_queue_buffer,
                                                 &runtime->command_queue_storage);
    runtime->command_lock = xSemaphoreCreateMutexStatic(&runtime->command_lock_storage);
    runtime->command_done = xSemaphoreCreateBinaryStatic(&runtime->command_done_storage);
    if (runtime->command_queue == NULL || runtime->command_lock == NULL ||
        runtime->command_done == NULL) {
        s_runtime_claimed = false;
        return ESP_ERR_NO_MEM;
    }
    runtime->task = xTaskCreateStaticPinnedToCore(async_led_task, "nocti_v4_led",
                                                  ASYNC_TASK_STACK_BYTES, runtime,
                                                  ASYNC_TASK_PRIORITY,
                                                  runtime->task_stack,
                                                  &runtime->task_storage,
                                                  ASYNC_TASK_CORE);
    if (runtime->task == NULL) {
        s_runtime_claimed = false;
        return ESP_ERR_NO_MEM;
    }
    async_led->runtime = runtime;
    async_led->initialized = true;
    return ESP_OK;
}

esp_err_t nocti_v4_async_led_enter(nocti_v4_async_led_t *async_led)
{
    return submit_command(async_led, ASYNC_COMMAND_ENTER, 0u);
}

esp_err_t nocti_v4_async_led_exit(nocti_v4_async_led_t *async_led)
{
    return submit_command(async_led, ASYNC_COMMAND_EXIT, 0u);
}

esp_err_t nocti_v4_async_led_on_single(nocti_v4_async_led_t *async_led,
                                        uint8_t source)
{
    return submit_command(async_led, ASYNC_COMMAND_ON_SINGLE, source);
}

esp_err_t nocti_v4_async_led_off_single(nocti_v4_async_led_t *async_led)
{
    return submit_command(async_led, ASYNC_COMMAND_OFF_SINGLE, 0u);
}

esp_err_t nocti_v4_async_led_on_burst(nocti_v4_async_led_t *async_led,
                                       uint8_t source)
{
    return submit_command(async_led, ASYNC_COMMAND_ON_BURST, source);
}

esp_err_t nocti_v4_async_led_all_off_burst(nocti_v4_async_led_t *async_led)
{
    return submit_command(async_led, ASYNC_COMMAND_OFF_BURST, 0u);
}

esp_err_t nocti_v4_async_led_on_burst_timed(nocti_v4_async_led_t *async_led,
                                             uint8_t source,
                                             uint64_t request_timestamp_us)
{
    return submit_command_timed(async_led, ASYNC_COMMAND_ON_BURST, source,
                                request_timestamp_us, request_timestamp_us, false);
}

esp_err_t nocti_v4_async_led_all_off_burst_timed(nocti_v4_async_led_t *async_led,
                                                  uint64_t request_timestamp_us)
{
    return submit_command_timed(async_led, ASYNC_COMMAND_OFF_BURST, 0u,
                                request_timestamp_us, request_timestamp_us, false);
}

esp_err_t nocti_v4_async_led_on_burst_scheduled(
    nocti_v4_async_led_t *async_led, uint8_t source,
    uint32_t session_epoch, uint32_t frame_sequence, uint32_t slot_epoch,
    uint8_t slot_index, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us)
{
    return submit_scheduled_command(
        async_led, ASYNC_COMMAND_ON_BURST, source, session_epoch,
        frame_sequence, slot_epoch, slot_index, deadline_timestamp_us,
        request_timestamp_us);
}

esp_err_t nocti_v4_async_led_off_single_scheduled(
    nocti_v4_async_led_t *async_led, uint8_t source,
    uint32_t session_epoch, uint32_t frame_sequence, uint32_t slot_epoch,
    uint8_t slot_index, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us)
{
    return submit_scheduled_command(
        async_led, ASYNC_COMMAND_OFF_SINGLE_SCHEDULED, source, session_epoch,
        frame_sequence, slot_epoch, slot_index, deadline_timestamp_us,
        request_timestamp_us);
}

esp_err_t nocti_v4_async_led_all_off_burst_scheduled(
    nocti_v4_async_led_t *async_led, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us)
{
    return submit_command_timed(async_led, ASYNC_COMMAND_OFF_BURST, 0u,
                                deadline_timestamp_us, request_timestamp_us, true);
}

esp_err_t nocti_v4_async_led_bind_scheduled_consumer(
    nocti_v4_async_led_t *async_led, TaskHandle_t scheduler_task)
{
    if (async_led == NULL || async_led->runtime == NULL ||
        scheduler_task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    async_led_runtime_t *runtime = (async_led_runtime_t *)async_led->runtime;
    if (__atomic_load_n(&runtime->mode, __ATOMIC_ACQUIRE) != PCA_IO_MODE_SYNC ||
        __atomic_load_n(&runtime->transaction_pending, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    TaskHandle_t existing = __atomic_load_n(&runtime->scheduled_consumer_task,
                                             __ATOMIC_ACQUIRE);
    if (existing != NULL && existing != scheduler_task) {
        return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&runtime->scheduled_consumer_task, scheduler_task,
                     __ATOMIC_RELEASE);
    clear_scheduled_completion_ring(runtime);
    return ESP_OK;
}

bool nocti_v4_async_led_pop_scheduled_completion(
    nocti_v4_async_led_t *async_led,
    nocti_v4_async_led_completion_t *out_completion)
{
    if (async_led == NULL || async_led->runtime == NULL ||
        out_completion == NULL) {
        return false;
    }
    async_led_runtime_t *runtime = (async_led_runtime_t *)async_led->runtime;
    if (__atomic_load_n(&runtime->scheduled_consumer_task, __ATOMIC_ACQUIRE) !=
        xTaskGetCurrentTaskHandle()) {
        return false;
    }
    const uint32_t tail = __atomic_load_n(&runtime->scheduled_completion_tail,
                                           __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->scheduled_completion_head,
                                           __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out_completion = runtime->scheduled_completion_ring[
        tail & SCHEDULED_COMPLETION_RING_MASK];
    __atomic_store_n(&runtime->scheduled_completion_tail, tail + 1u,
                     __ATOMIC_RELEASE);
    return true;
}

bool nocti_v4_async_led_take_scheduled_overflow(nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        return false;
    }
    async_led_runtime_t *runtime = (async_led_runtime_t *)async_led->runtime;
    return __atomic_exchange_n(
        &runtime->scheduled_completion_overflow_pending, false,
        __ATOMIC_ACQ_REL);
}

bool nocti_v4_async_led_is_active(const nocti_v4_async_led_t *async_led)
{
    return nocti_v4_async_led_mode(async_led) != PCA_IO_MODE_SYNC;
}

nocti_v4_pca_io_mode_t nocti_v4_async_led_mode(const nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        return PCA_IO_MODE_ASYNC_FAULTED;
    }
    const async_led_runtime_t *runtime = (const async_led_runtime_t *)async_led->runtime;
    return __atomic_load_n(&runtime->mode, __ATOMIC_ACQUIRE);
}

void nocti_v4_async_led_get_status(const nocti_v4_async_led_t *async_led,
                                   nocti_v4_async_led_status_t *out_status)
{
    if (out_status == NULL) {
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    if (async_led == NULL || async_led->runtime == NULL) {
        return;
    }
    const async_led_runtime_t *runtime = (const async_led_runtime_t *)async_led->runtime;
    out_status->initialized = async_led->initialized;
    out_status->callbacks_registered = __atomic_load_n(&runtime->callbacks_registered,
                                                        __ATOMIC_ACQUIRE);
    out_status->transaction_pending = __atomic_load_n(&runtime->transaction_pending,
                                                       __ATOMIC_ACQUIRE);
    out_status->submissions_allowed = __atomic_load_n(&runtime->submissions_allowed,
                                                       __ATOMIC_ACQUIRE);
    out_status->mode = __atomic_load_n(&runtime->mode, __ATOMIC_ACQUIRE);
    out_status->i2c_clock_hz = runtime->i2c != NULL ? runtime->i2c->clock_hz : 0u;
    out_status->i2c_queue_depth = runtime->i2c != NULL ? runtime->i2c->queue_depth : 0u;
    out_status->generation = runtime->generation;
    out_status->next_command_id = runtime->next_command_id;
    out_status->selected_source = runtime->selected_source;
    out_status->task_core = runtime->task_core;
    out_status->callback_core = __atomic_load_n(&runtime->callback_core, __ATOMIC_ACQUIRE);
    out_status->init_core = runtime->init_core;
    out_status->last_result = runtime->last_result;
    out_status->last_command_id = runtime->last_identity.command_id;
    out_status->last_source = runtime->last_identity.source;
    out_status->last_channel = runtime->last_identity.channel;
    out_status->last_payload_type = runtime->last_identity.payload_type;
    out_status->last_payload_length = runtime->last_identity.payload_length;
    if (runtime->last_identity.payload_type == NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON ||
        runtime->last_identity.payload_type == NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF) {
        memcpy(out_status->last_payload,
               runtime->last_identity.payload_type == NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON ?
                   runtime->single_on_payload[runtime->last_identity.source] :
                   runtime->single_off_payload[runtime->last_identity.source],
               2u);
    } else if (runtime->last_identity.payload_type == NOCTI_V4_ASYNC_PAYLOAD_BURST_ON) {
        memcpy(out_status->last_payload,
               runtime->burst_on_payload[runtime->last_identity.source], 7u);
    } else if (runtime->last_identity.payload_type == NOCTI_V4_ASYNC_PAYLOAD_BURST_OFF) {
        memcpy(out_status->last_payload, runtime->burst_off_payload, 7u);
    }
    out_status->last_deadline_timestamp_us = runtime->last_identity.deadline_timestamp_us;
    out_status->last_request_timestamp_us = runtime->last_identity.request_timestamp_us;
    out_status->last_queue_timestamp_us = runtime->last_identity.queue_timestamp_us;
    out_status->last_queue_exit_timestamp_us = runtime->last_queue_exit_timestamp_us;
    out_status->last_dequeue_timestamp_us = runtime->last_identity.dequeue_timestamp_us;
    out_status->last_submit_timestamp_us = runtime->last_identity.submit_timestamp_us;
    out_status->last_i2c_submit_return_timestamp_us =
        runtime->last_i2c_submit_return_timestamp_us;
    out_status->last_complete_timestamp_us = runtime->last_complete_timestamp_us;
    out_status->last_deadline_to_request_us = elapsed_us(
        runtime->last_identity.deadline_timestamp_us,
        runtime->last_identity.request_timestamp_us);
    out_status->last_request_to_queue_us = elapsed_us(
        runtime->last_identity.request_timestamp_us,
        runtime->last_identity.queue_timestamp_us);
    out_status->last_deadline_to_queue_us = elapsed_us(
        runtime->last_identity.deadline_timestamp_us,
        runtime->last_identity.queue_timestamp_us);
    out_status->last_queue_send_call_us = elapsed_us(
        runtime->last_identity.queue_timestamp_us,
        runtime->last_queue_exit_timestamp_us);
    out_status->last_queue_to_dequeue_us = elapsed_us(
        runtime->last_identity.queue_timestamp_us,
        runtime->last_identity.dequeue_timestamp_us);
    out_status->last_dequeue_to_submit_us = elapsed_us(
        runtime->last_identity.dequeue_timestamp_us,
        runtime->last_identity.submit_timestamp_us);
    out_status->last_i2c_submit_call_us = elapsed_us(
        runtime->last_identity.submit_timestamp_us,
        runtime->last_i2c_submit_return_timestamp_us);
    out_status->last_request_to_complete_us = elapsed_us(
        runtime->last_identity.request_timestamp_us,
        runtime->last_complete_timestamp_us);
    out_status->last_deadline_to_complete_us = elapsed_us(
        runtime->last_identity.deadline_timestamp_us,
        runtime->last_complete_timestamp_us);
    out_status->last_latency_us = runtime->last_complete_timestamp_us >=
                                  runtime->last_identity.submit_timestamp_us ?
        (uint32_t)(runtime->last_complete_timestamp_us -
                   runtime->last_identity.submit_timestamp_us) : 0u;
    out_status->last_i2c_event = runtime->last_i2c_event;
    out_status->last_callback_core = runtime->last_callback_core;
}

void nocti_v4_async_led_print_status(const nocti_v4_async_led_t *async_led)
{
    nocti_v4_async_led_status_t status;
    nocti_v4_async_led_get_status(async_led, &status);
    printf("ASYNC_STATUS mode=%s initialized=%u ready=%u i2c_hz=%" PRIu32
           " qdepth=%" PRIu32 " callbacks_registered=%u transaction_pending=%u generation=%" PRIu32
           " next_command_id=%" PRIu32 " selected_source=%" PRId32
           " task_core=%" PRId32 " init_core=%" PRId32 " callback_core=%" PRId32 " last_result=%s\n",
           nocti_v4_pca_io_mode_name(status.mode), status.initialized ? 1u : 0u,
           status.submissions_allowed ? 1u : 0u,
           status.i2c_clock_hz,
           status.i2c_queue_depth,
           status.callbacks_registered ? 1u : 0u, status.transaction_pending ? 1u : 0u,
           status.generation, status.next_command_id, status.selected_source, status.task_core,
           status.init_core, status.callback_core, nocti_v4_err_name(status.last_result));
}

void nocti_v4_async_led_print_health(const nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        return;
    }
    const async_led_runtime_t *runtime = (const async_led_runtime_t *)async_led->runtime;
    const nocti_v4_health_t *health = runtime->health;
    printf("ASYNC_HEALTH submitted=%" PRIu32 " completed=%" PRIu32
           " submit_errors=%" PRIu32 " queue_send_errors=%" PRIu32
           " queue_depth_max=%" PRIu32 " completion_errors=%" PRIu32
           " pending_at_submit_on=%" PRIu32 " pending_at_submit_off=%" PRIu32
           " completion_ring_overflow=%" PRIu32
           " scheduler_completion_ring_overflow=%" PRIu32
           " stale_completion=%" PRIu32
           " single_on=%" PRIu32 " single_off=%" PRIu32
           " burst_on=%" PRIu32 " burst_off=%" PRIu32
           " force_off_failures=%" PRIu32 " exit_failures=%" PRIu32
           " normal_off_invariant_failures=%" PRIu32
           " task_core=%" PRId32 " callback_core=%" PRId32 " init_core=%" PRId32 "\n",
           health->async_commands_submitted, health->async_commands_completed,
           health->async_submit_errors, health->async_command_queue_send_errors,
           health->async_command_queue_depth_max, health->async_completion_errors,
           health->async_on_pending_at_submit,
           health->async_off_pending_at_submit,
           health->async_completion_ring_overflow,
           health->async_scheduler_completion_ring_overflow,
           health->async_stale_completion,
           health->async_single_on_count, health->async_single_off_count,
           health->async_burst_on_count, health->async_burst_off_count,
           health->async_force_off_failures, health->async_exit_failures,
           health->async_normal_off_invariant_failures,
           health->async_led_task_core,
           health->async_callback_core, health->async_init_core);
}

void nocti_v4_async_led_print_timing(const nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        return;
    }
    const async_led_runtime_t *runtime = (const async_led_runtime_t *)async_led->runtime;
    const nocti_v4_health_t *health = runtime->health;
    printf("ASYNC_TIMING latency_min_us=%" PRIu32 " latency_max_us=%" PRIu32
           " latency_avg_us=%" PRIu32 " completed=%" PRIu32 "\n",
           health->async_latency_min_us, health->async_latency_max_us,
           health->async_latency_avg_us, health->async_commands_completed);
}

void nocti_v4_async_led_print_timing_detail(const nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        printf("LED_TIMING_DETAIL state=NOT_INITIALIZED\n");
        return;
    }
    const async_led_runtime_t *runtime =
        (const async_led_runtime_t *)async_led->runtime;
    static const char *const stage_name[ASYNC_TIMING_STAGE_COUNT] = {
        "deadline_to_request", "request_to_queue", "deadline_to_queue",
        "queue_send_call", "queue_to_dequeue", "dequeue_to_submit",
        "i2c_submit_call", "submit_to_complete", "request_to_complete",
        "deadline_to_complete",
    };
    static const char *const class_name[ASYNC_TIMING_CLASS_COUNT] = {
        "ON_7B", "NORMAL_OFF_2B", "SAFETY_OFF_7B", "DIAGNOSTIC_ON_2B",
    };
    static const uint8_t payload_length[ASYNC_TIMING_CLASS_COUNT] = {
        7u, 2u, 7u, 2u,
    };
    printf("LED_TIMING_DETAIL task=nocti_v4_led core=%" PRId32
           " priority=%u stack_bytes=%u queue_length=%u queue_item_bytes=%u"
           " i2c_configured_qdepth=%" PRIu32
           " i2c_live_queue_depth_api=UNAVAILABLE app_one_in_flight=1"
           " core0_v4_critical_sections=0 critical_section_max_us=0"
           " idf_driver_critical_duration=NOT_OBSERVABLE"
           " consumer_blocking=xQueueReceive_portMAX_DELAY"
           " scan_producer_wait_ticks=0 scan_waits_for_completion=0"
           " completion_delivery=STATIC_SPSC_TO_SCHEDULER"
           " queue_send_call_semantics=WALL_SPAN_MAY_INCLUDE_LED_PREEMPTION"
           " histogram_us=<50,50_99,100_249,250_499,ge500\n",
           runtime->task_core, ASYNC_TASK_PRIORITY, ASYNC_TASK_STACK_BYTES,
           ASYNC_COMMAND_QUEUE_CAPACITY, (unsigned)sizeof(async_command_t),
           runtime->i2c != NULL ? runtime->i2c->queue_depth : 0u);
    for (uint8_t timing_class = 0u;
         timing_class < ASYNC_TIMING_CLASS_COUNT; ++timing_class) {
        for (uint8_t stage = 0u; stage < ASYNC_TIMING_STAGE_COUNT; ++stage) {
            const async_timing_stat_t *stat =
                &runtime->timing[timing_class][stage];
            printf("LED_TIMING class=%s payload_length=%u stage=%s count=%" PRIu32
                   " min_us=%" PRIu32 " max_us=%" PRIu32 " avg_us=%" PRIu32
                   " hist=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
                   ",%" PRIu32 "\n",
                   class_name[timing_class], payload_length[timing_class],
                   stage_name[stage],
                   __atomic_load_n(&stat->count, __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->minimum_us, __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->maximum_us, __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->average_us, __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->histogram[0], __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->histogram[1], __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->histogram[2], __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->histogram[3], __ATOMIC_ACQUIRE),
                   __atomic_load_n(&stat->histogram[4], __ATOMIC_ACQUIRE));
        }
    }
}

void nocti_v4_async_led_print_last_result(const nocti_v4_async_led_t *async_led)
{
    nocti_v4_async_led_status_t status;
    nocti_v4_async_led_get_status(async_led, &status);
    printf("ASYNC_RESULT command_id=%" PRIu32 " generation=%" PRIu32
           " source=%u channel=%u payload_type=%s payload=",
           status.last_command_id, status.generation, (unsigned)status.last_source,
           (unsigned)status.last_channel,
           nocti_v4_async_payload_type_name(status.last_payload_type));
    for (uint8_t index = 0u; index < status.last_payload_length; ++index) {
        printf("%s%02X", index == 0u ? "" : ",", status.last_payload[index]);
    }
    printf(" deadline_us=%" PRIu64 " request_us=%" PRIu64
           " queue_us=%" PRIu64 " queue_exit_us=%" PRIu64
           " dequeue_us=%" PRIu64 " submit_us=%" PRIu64
           " i2c_submit_return_us=%" PRIu64
           " complete_us=%" PRIu64 " deadline_to_request_us=%" PRIu32
           " request_to_queue_us=%" PRIu32 " deadline_to_queue_us=%" PRIu32
           " queue_send_call_us=%" PRIu32
           " queue_to_dequeue_us=%" PRIu32 " dequeue_to_submit_us=%" PRIu32
           " i2c_submit_call_us=%" PRIu32
           " submit_to_complete_us=%" PRIu32 " request_to_complete_us=%" PRIu32
           " deadline_to_complete_us=%" PRIu32
           " event=%s callback_core=%" PRId32
           " result=%s physical_state=UNVERIFIED\n",
           status.last_deadline_timestamp_us, status.last_request_timestamp_us,
           status.last_queue_timestamp_us, status.last_queue_exit_timestamp_us,
           status.last_dequeue_timestamp_us, status.last_submit_timestamp_us,
           status.last_i2c_submit_return_timestamp_us,
           status.last_complete_timestamp_us, status.last_deadline_to_request_us,
           status.last_request_to_queue_us, status.last_deadline_to_queue_us,
           status.last_queue_send_call_us,
           status.last_queue_to_dequeue_us, status.last_dequeue_to_submit_us,
           status.last_i2c_submit_call_us,
           status.last_latency_us, status.last_request_to_complete_us,
           status.last_deadline_to_complete_us,
           event_name(status.last_i2c_event),
           status.last_callback_core, nocti_v4_err_name(status.last_result));
}

void nocti_v4_async_led_print_payloads(const nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        printf("DUMP_LED_PAYLOADS FAIL result=ESP_ERR_INVALID_STATE\n");
        return;
    }
    const async_led_runtime_t *runtime = (const async_led_runtime_t *)async_led->runtime;
    const bool valid = persistent_payloads_valid(runtime);
    printf("DUMP_LED_PAYLOADS table_valid=%u control=0x%02X sources=%u\n",
           valid ? 1u : 0u, PCA_LEDOUT_AUTO_INCREMENT_CONTROL,
           NOCTI_V4_OPTICAL_SOURCE_COUNT);
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
        uint8_t channel = 0u;
        (void)nocti_v4_led_resolve_source(source, &channel);
        printf("LED_PAYLOAD source=%u channel=%u ledout_reg=0x%02X field_shift=%u burst=",
               source, channel, PCA_LEDOUT0 + channel / 4u, (channel % 4u) * 2u);
        for (uint8_t index = 0u; index < 7u; ++index) {
            printf("%s%02X", index == 0u ? "" : ",", runtime->burst_on_payload[source][index]);
        }
        printf(" one_hot=1 unused_channels_off=1\n");
    }
}

void nocti_v4_async_led_print_off_payloads(
    const nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        printf("DUMP_OFF_PAYLOADS FAIL result=ESP_ERR_INVALID_STATE\n");
        return;
    }
    const async_led_runtime_t *runtime =
        (const async_led_runtime_t *)async_led->runtime;
    const bool valid = persistent_payloads_valid(runtime);
    printf("DUMP_OFF_PAYLOADS table_valid=%u sources=%u normal_off=2B"
           " safety_off=7B_ALL_LEDOUT_RESET_FALLBACK\n",
           valid ? 1u : 0u, NOCTI_V4_OPTICAL_SOURCE_COUNT);
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT;
         ++source) {
        uint8_t channel = 0u;
        (void)nocti_v4_led_resolve_source(source, &channel);
        printf("OFF_PAYLOAD source=%u channel=%u ledout_reg=0x%02X"
               " payload=%02X,%02X invariant=accepted_matching_7B_one_hot_ON\n",
               source, channel,
               runtime->single_off_payload[source][0],
               runtime->single_off_payload[source][0],
               runtime->single_off_payload[source][1]);
    }
}

void nocti_v4_async_led_sample_stack_watermark(nocti_v4_async_led_t *async_led)
{
    if (async_led == NULL || async_led->runtime == NULL) {
        return;
    }
    async_led_runtime_t *runtime = (async_led_runtime_t *)async_led->runtime;
    if (runtime->task != NULL) {
        __atomic_store_n(&runtime->health->async_led_task_stack_watermark_bytes,
                         uxTaskGetStackHighWaterMark(runtime->task), __ATOMIC_RELEASE);
    }
}

void nocti_v4_async_led_cancel_submissions(nocti_v4_async_led_t *async_led)
{
    if (async_led==NULL || async_led->runtime==NULL) return;
    async_led_runtime_t *runtime=async_led->runtime;
    __atomic_store_n(&runtime->submissions_allowed,false,__ATOMIC_RELEASE);
    nocti_v4_pca_io_mode_t mode=__atomic_load_n(&runtime->mode,__ATOMIC_ACQUIRE);
    if (mode==PCA_IO_MODE_ASYNC)
        __atomic_store_n(&runtime->mode,PCA_IO_MODE_ASYNC_FAULTED,__ATOMIC_RELEASE);
}
