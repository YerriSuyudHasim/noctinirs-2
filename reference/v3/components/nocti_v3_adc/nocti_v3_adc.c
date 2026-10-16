#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_adc.h"

#include <limits.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nocti_v3_ads131m04.h"

static const char *TAG = "nocti_v3_adc";

#define EVENT_RING_MASK (NOCTI_V3_DRDY_EVENT_RING_CAPACITY - 1u)
#define RAW_RING_MASK   (NOCTI_V3_RAW_RING_CAPACITY - 1u)

typedef struct {
    uint64_t timestamp_us;
    uint32_t sequence;
    uint32_t optical_epoch_snapshot;
    uint16_t optical_state_snapshot;
} drdy_event_t;

typedef enum {
    ADC_COMMAND_START_DARK = 1,
    ADC_COMMAND_START_SCAN,
    ADC_COMMAND_STOP,
} adc_command_type_t;

typedef struct {
    adc_command_type_t type;
} adc_command_t;

typedef struct {
    /* Single-session counters are 32-bit so every hot-path atomic is native on Xtensa. */
    uint32_t adc_ok;
    uint32_t adc_missed;
    uint32_t adc_sequence_gap;
    uint32_t adc_ring_overflow;
    uint32_t adc_period_gap;
    uint32_t crc_bad;
    uint32_t ads_status_errors;
    uint32_t spi_errors;
    uint32_t adc_period_min_us;
    uint32_t adc_period_max_us;
    uint32_t adc_max_gap_us;
    uint32_t spi_duration_max_us;
    uint32_t raw_ring_depth_max;
} adc_counters_t;

typedef struct {
    nocti_v3_adc_t *public_adc;
    nocti_v3_board_t *board;
    nocti_v3_timebase_t *timebase;
    nocti_v3_health_t *health;
    nocti_v3_ads131m04_t ads;

    TaskHandle_t adc_task;
    TaskHandle_t diagnostic_task;
    TaskHandle_t scan_consumer_task;
    SemaphoreHandle_t init_done;
    SemaphoreHandle_t command_done;
    SemaphoreHandle_t command_lock;
    SemaphoreHandle_t drain_done;
    StaticSemaphore_t init_done_storage;
    StaticSemaphore_t command_done_storage;
    StaticSemaphore_t command_lock_storage;
    StaticSemaphore_t drain_done_storage;
    QueueHandle_t command_queue;
    StaticQueue_t command_queue_storage;
    uint8_t command_queue_buffer[4u * sizeof(adc_command_t)];

    volatile bool capture_enabled;
    volatile bool drain_requested;
    volatile bool initialized;
    volatile bool ready;
    volatile bool running;
    volatile bool rate_valid;
    volatile bool session_integrity_fault;
    volatile nocti_v3_lifecycle_state_t lifecycle;
    volatile esp_err_t init_result;
    volatile esp_err_t command_result;
    volatile esp_err_t last_error;
    volatile uint32_t session_epoch;
    volatile nocti_v3_mode_t current_mode;
    volatile uint32_t optical_epoch_snapshot;
    volatile uint16_t optical_state_snapshot;

    volatile uint32_t drdy_sequence;
    volatile uint32_t drdy_event_overflow_isr;
    volatile uint32_t event_ring_depth_max_isr;
    volatile bool drdy_overflow_pending;
    volatile uint32_t event_head;
    volatile uint32_t event_tail;
    drdy_event_t event_ring[NOCTI_V3_DRDY_EVENT_RING_CAPACITY];

    volatile uint32_t raw_head;
    volatile uint32_t raw_tail;
    nocti_v3_raw_sample_t raw_ring[NOCTI_V3_RAW_RING_CAPACITY];

    adc_counters_t counters;
    uint32_t measured_sps;
    uint32_t measured_period_average_us_x1000;
    uint32_t inferred_clkin_hz;
    uint32_t adc_stack_watermark_bytes;
    uint32_t diagnostic_stack_watermark_bytes;
    int adc_task_core;
    int diagnostic_task_core;
    int gpio_isr_init_core;
    volatile int gpio_isr_callback_core;
    nocti_v3_ads_registers_t registers;
    uint32_t last_runtime_sequence;
    uint64_t last_runtime_timestamp_us;
    uint64_t run_start_timestamp_us;
    uint32_t consecutive_spi_errors;
    uint32_t consecutive_crc_errors;
    uint32_t consecutive_status_errors;
    uint32_t consecutive_period_gaps;
    uint32_t sync_discard_remaining;
    uint32_t expected_resync_remaining;
} adc_runtime_t;

static adc_runtime_t s_runtime;

static inline void counter_add_u32(uint32_t *counter, uint32_t value)
{
    (void)__atomic_fetch_add(counter, value, __ATOMIC_RELAXED);
}

static void update_max_u32(uint32_t *value, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(value,
                                        &current,
                                        candidate,
                                        false,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
}

static void update_min_u32(uint32_t *value, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while ((current == 0u || candidate < current) &&
           !__atomic_compare_exchange_n(value,
                                        &current,
                                        candidate,
                                        false,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
}

static uint32_t event_depth(adc_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&runtime->event_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

static uint32_t raw_depth(adc_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&runtime->raw_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

static void clear_event_ring(adc_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_ACQUIRE);
    __atomic_store_n(&runtime->event_tail, head, __ATOMIC_RELEASE);
}

static void clear_raw_ring(adc_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_ACQUIRE);
    __atomic_store_n(&runtime->raw_tail, head, __ATOMIC_RELEASE);
}

static bool event_pop(adc_runtime_t *runtime, drdy_event_t *out_event)
{
    const uint32_t tail = __atomic_load_n(&runtime->event_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_ACQUIRE);
    if (tail == head) return false;
    *out_event = runtime->event_ring[tail & EVENT_RING_MASK];
    __atomic_store_n(&runtime->event_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static bool raw_push(adc_runtime_t *runtime, const nocti_v3_raw_sample_t *sample)
{
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->raw_tail, __ATOMIC_ACQUIRE);
    const uint32_t depth = head - tail;
    if (depth >= NOCTI_V3_RAW_RING_CAPACITY) return false;
    runtime->raw_ring[head & RAW_RING_MASK] = *sample;
    __atomic_store_n(&runtime->raw_head, head + 1u, __ATOMIC_RELEASE);
    update_max_u32(&runtime->counters.raw_ring_depth_max, depth + 1u);
    return true;
}

static bool raw_pop(adc_runtime_t *runtime, nocti_v3_raw_sample_t *out_sample)
{
    const uint32_t tail = __atomic_load_n(&runtime->raw_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_ACQUIRE);
    if (tail == head) return false;
    *out_sample = runtime->raw_ring[tail & RAW_RING_MASK];
    __atomic_store_n(&runtime->raw_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static void IRAM_ATTR drdy_isr(void *argument)
{
    adc_runtime_t *runtime = (adc_runtime_t *)argument;
    __atomic_store_n(&runtime->gpio_isr_callback_core,
                     xPortGetCoreID(),
                     __ATOMIC_RELAXED);
    if (!__atomic_load_n(&runtime->capture_enabled, __ATOMIC_RELAXED)) return;

    uint64_t timestamp_us = 0u;
    if (nocti_v3_timebase_now_us_isr(runtime->timebase, &timestamp_us) != ESP_OK) {
        (void)__atomic_fetch_add(&runtime->drdy_event_overflow_isr,
                                 1u,
                                 __ATOMIC_RELAXED);
        __atomic_store_n(&runtime->drdy_overflow_pending, true, __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
        BaseType_t high_priority_woken = pdFALSE;
        vTaskNotifyGiveFromISR(runtime->adc_task, &high_priority_woken);
        if (high_priority_woken == pdTRUE) portYIELD_FROM_ISR();
        return;
    }

    const uint32_t sequence =
        __atomic_add_fetch(&runtime->drdy_sequence, 1u, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->event_tail, __ATOMIC_ACQUIRE);
    const uint32_t depth = head - tail;
    if (depth >= NOCTI_V3_DRDY_EVENT_RING_CAPACITY) {
        (void)__atomic_fetch_add(&runtime->drdy_event_overflow_isr,
                                 1u,
                                 __ATOMIC_RELAXED);
        __atomic_store_n(&runtime->drdy_overflow_pending, true, __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
    } else {
        runtime->event_ring[head & EVENT_RING_MASK] = (drdy_event_t) {
            .timestamp_us = timestamp_us,
            .sequence = sequence,
            .optical_epoch_snapshot = __atomic_load_n(
                &runtime->optical_epoch_snapshot, __ATOMIC_RELAXED),
            .optical_state_snapshot = __atomic_load_n(
                &runtime->optical_state_snapshot, __ATOMIC_RELAXED),
        };
        __atomic_store_n(&runtime->event_head, head + 1u, __ATOMIC_RELEASE);
        const uint32_t candidate_depth = depth + 1u;
        const uint32_t current_max = __atomic_load_n(
            &runtime->event_ring_depth_max_isr, __ATOMIC_RELAXED);
        if (candidate_depth > current_max) {
            __atomic_store_n(&runtime->event_ring_depth_max_isr,
                             candidate_depth,
                             __ATOMIC_RELAXED);
        }
    }

    BaseType_t high_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(runtime->adc_task, &high_priority_woken);
    if (high_priority_woken == pdTRUE) portYIELD_FROM_ISR();
}

static esp_err_t install_drdy_interrupt(adc_runtime_t *runtime)
{
    const gpio_num_t drdy = (gpio_num_t)runtime->board->pins.ads131_drdy;
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << drdy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&input);
    if (err != ESP_OK) return err;

    runtime->gpio_isr_init_core = xPortGetCoreID();
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG,
                 "GPIO ISR service was already installed; DRDY affinity cannot be proven");
        return err;
    }
    if (err != ESP_OK) return err;
    runtime->board->isr_service_ready = true;

    err = gpio_isr_handler_add(drdy, drdy_isr, runtime);
    if (err != ESP_OK) return err;
    err = gpio_intr_disable(drdy);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

static esp_err_t wait_for_event(adc_runtime_t *runtime,
                                drdy_event_t *out_event,
                                uint32_t timeout_ms)
{
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms) > 0u ?
        pdMS_TO_TICKS(timeout_ms) : 1u;
    uint64_t start = 0u;
    (void)nocti_v3_timebase_now_us(runtime->timebase, &start);
    const uint64_t deadline = start + (uint64_t)timeout_ms * 1000u;

    for (;;) {
        if (event_pop(runtime, out_event)) return ESP_OK;
        uint64_t now = 0u;
        (void)nocti_v3_timebase_now_us(runtime->timebase, &now);
        if (now >= deadline) return ESP_ERR_TIMEOUT;
        (void)ulTaskNotifyTake(pdTRUE, timeout_ticks);
    }
}

static esp_err_t read_one_event(adc_runtime_t *runtime,
                                const drdy_event_t *event,
                                uint32_t expected_previous_sequence,
                                uint64_t previous_timestamp,
                                bool count_runtime,
                                nocti_v3_raw_sample_t *out_sample)
{
    const uint32_t backlog = event_depth(runtime);
    if (backlog >= NOCTI_V3_ADS_MAX_EVENT_BACKLOG) {
        counter_add_u32(&runtime->counters.adc_missed, backlog - 1u);
        __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
        return ESP_ERR_INVALID_STATE;
    }

    if (expected_previous_sequence != 0u &&
        event->sequence != expected_previous_sequence + 1u) {
        const uint32_t gap = event->sequence - expected_previous_sequence - 1u;
        counter_add_u32(&runtime->counters.adc_sequence_gap, gap);
        counter_add_u32(&runtime->counters.adc_missed, gap);
        __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
    }

    if (previous_timestamp != 0u && event->timestamp_us > previous_timestamp) {
        const uint32_t period = (uint32_t)(event->timestamp_us - previous_timestamp);
        update_min_u32(&runtime->counters.adc_period_min_us, period);
        update_max_u32(&runtime->counters.adc_period_max_us, period);
        update_max_u32(&runtime->counters.adc_max_gap_us, period);
        if (period > NOCTI_V3_ADS_PERIOD_GAP_THRESHOLD_US) {
            counter_add_u32(&runtime->counters.adc_period_gap, 1u);
        }
    }

    nocti_v3_ads_frame_t frame;
    uint32_t spi_duration_us = 0u;
    uint64_t spi_complete_us = event->timestamp_us;
    const esp_err_t spi_result = nocti_v3_ads131m04_read_frame(
        &runtime->ads, &frame, &spi_duration_us, &spi_complete_us);
    update_max_u32(&runtime->counters.spi_duration_max_us, spi_duration_us);
    bool effective_status_valid = spi_result == ESP_OK && frame.status_valid;
    if (effective_status_valid && frame.resync_occurred) {
        if (runtime->sync_discard_remaining > 0u &&
            runtime->expected_resync_remaining > 0u) {
            --runtime->expected_resync_remaining;
        } else {
            effective_status_valid = false;
        }
    }

    if (spi_result != ESP_OK) {
        counter_add_u32(&runtime->counters.spi_errors, 1u);
        runtime->last_error = spi_result;
    } else if (!frame.crc_valid) {
        counter_add_u32(&runtime->counters.crc_bad, 1u);
    } else if (!effective_status_valid) {
        counter_add_u32(&runtime->counters.ads_status_errors, 1u);
    } else if (count_runtime) {
        counter_add_u32(&runtime->counters.adc_ok, 1u);
    }

    if (out_sample != NULL) {
        *out_sample = (nocti_v3_raw_sample_t) {
            .drdy_timestamp_us = event->timestamp_us,
            .spi_complete_timestamp_us = spi_complete_us,
            .drdy_sequence = event->sequence,
            .ads_status = spi_result == ESP_OK ? frame.status : 0u,
            .channel = {
                spi_result == ESP_OK ? frame.channel[0] : 0,
                spi_result == ESP_OK ? frame.channel[1] : 0,
                spi_result == ESP_OK ? frame.channel[2] : 0,
                spi_result == ESP_OK ? frame.channel[3] : 0,
            },
            .crc_received = spi_result == ESP_OK ? frame.crc_received : 0u,
            .crc_valid = spi_result == ESP_OK && frame.crc_valid,
            .status_valid = effective_status_valid,
            .spi_result = spi_result,
            .session_epoch = runtime->session_epoch,
            .optical_epoch_snapshot = event->optical_epoch_snapshot,
            .optical_state_snapshot = event->optical_state_snapshot,
        };
    }
    return spi_result;
}

static esp_err_t validate_rate(adc_runtime_t *runtime)
{
    const gpio_num_t drdy = (gpio_num_t)runtime->board->pins.ads131_drdy;
    (void)gpio_intr_disable(drdy);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    esp_err_t err = nocti_v3_ads131m04_sync(&runtime->ads);
    if (err != ESP_OK) goto done;
    clear_event_ring(runtime);
    __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->capture_enabled, true, __ATOMIC_RELEASE);
    err = gpio_intr_enable(drdy);
    if (err != ESP_OK) goto done;

    uint32_t sync_discarded = 0u;
    uint32_t expected_resync_seen = 0u;
    uint32_t unexpected_sync_status = 0u;
    uint32_t expected_resync_budget = NOCTI_V3_ADS_EXPECTED_RESYNC_BUDGET;

    /* A SYNC pulse clears the ADC FIFO and can legitimately set F_RESYNC on
     * the first NULL response because the GPIO edge is asynchronous to CLKIN.
     * Also discard three conversion periods so rate validation starts only
     * after the sinc3 path has re-settled. These frames are still checked for
     * SPI, CRC, and all non-F_RESYNC STATUS faults.
     */
    for (uint32_t i = 0u; i < NOCTI_V3_ADS_SYNC_DISCARD_FRAMES; ++i) {
        drdy_event_t event;
        err = wait_for_event(runtime,
                             &event,
                             NOCTI_V3_ADS_EVENT_WAIT_TIMEOUT_MS);
        if (err != ESP_OK) break;

        nocti_v3_ads_frame_t frame;
        uint32_t duration = 0u;
        const esp_err_t frame_error = nocti_v3_ads131m04_read_frame(
            &runtime->ads, &frame, &duration, NULL);
        update_max_u32(&runtime->counters.spi_duration_max_us, duration);
        if (frame_error != ESP_OK) {
            err = frame_error;
            break;
        }
        if (!frame.crc_valid || !frame.status_valid) {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if (frame.resync_occurred) {
            if (expected_resync_budget > 0u) {
                --expected_resync_budget;
                ++expected_resync_seen;
            } else {
                ++unexpected_sync_status;
                err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
        }
        ++sync_discarded;
    }
    if (err != ESP_OK) goto log_and_done;

    uint64_t first_timestamp = 0u;
    uint64_t last_timestamp = 0u;
    uint64_t period_sum = 0u;
    uint32_t previous_sequence = 0u;
    uint32_t period_min = UINT32_MAX;
    uint32_t period_max = 0u;
    uint32_t sequence_gaps = 0u;
    uint32_t crc_bad = 0u;
    uint32_t status_errors = 0u;
    uint32_t spi_errors = 0u;

    for (uint32_t i = 0u; i < NOCTI_V3_ADS_RATE_VALIDATION_SAMPLES; ++i) {
        drdy_event_t event;
        err = wait_for_event(runtime,
                             &event,
                             NOCTI_V3_ADS_EVENT_WAIT_TIMEOUT_MS);
        if (err != ESP_OK) break;
        if (i == 0u) first_timestamp = event.timestamp_us;
        if (previous_sequence != 0u && event.sequence != previous_sequence + 1u) {
            sequence_gaps += event.sequence - previous_sequence - 1u;
        }
        if (last_timestamp != 0u) {
            const uint32_t period = (uint32_t)(event.timestamp_us - last_timestamp);
            period_sum += period;
            if (period < period_min) period_min = period;
            if (period > period_max) period_max = period;
        }

        nocti_v3_ads_frame_t frame;
        uint32_t duration = 0u;
        const esp_err_t frame_error = nocti_v3_ads131m04_read_frame(
            &runtime->ads, &frame, &duration, NULL);
        update_max_u32(&runtime->counters.spi_duration_max_us, duration);
        if (frame_error != ESP_OK) {
            ++spi_errors;
            err = frame_error;
            break;
        }
        if (!frame.crc_valid) ++crc_bad;
        /* Any F_RESYNC after the bounded settling window is unexpected. */
        if (!frame.status_valid || frame.resync_occurred) ++status_errors;
        previous_sequence = event.sequence;
        last_timestamp = event.timestamp_us;
    }

    if (err == ESP_OK) {
        const uint32_t intervals = NOCTI_V3_ADS_RATE_VALIDATION_SAMPLES - 1u;
        const uint64_t elapsed = last_timestamp - first_timestamp;
        if (elapsed == 0u || intervals == 0u) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            runtime->measured_sps = (uint32_t)(((uint64_t)intervals * 1000000u + elapsed / 2u) / elapsed);
            runtime->measured_period_average_us_x1000 =
                (uint32_t)((period_sum * 1000u + intervals / 2u) / intervals);
            runtime->inferred_clkin_hz =
                runtime->measured_sps * 2u * NOCTI_V3_ADS_OSR;

            const uint32_t tolerance =
                NOCTI_V3_ADS_SAMPLE_RATE_HZ * NOCTI_V3_ADS_RATE_TOLERANCE_PERCENT / 100u;
            const uint32_t min_sps = NOCTI_V3_ADS_SAMPLE_RATE_HZ - tolerance;
            const uint32_t max_sps = NOCTI_V3_ADS_SAMPLE_RATE_HZ + tolerance;
            runtime->rate_valid = runtime->measured_sps >= min_sps &&
                                  runtime->measured_sps <= max_sps &&
                                  sequence_gaps == 0u && crc_bad == 0u &&
                                  status_errors == 0u && spi_errors == 0u &&
                                  unexpected_sync_status == 0u &&
                                  __atomic_load_n(
                                      &runtime->drdy_event_overflow_isr,
                                      __ATOMIC_ACQUIRE) == 0u;
            if (!runtime->rate_valid) err = ESP_ERR_INVALID_RESPONSE;

            ESP_LOGI(TAG,
                     "ADS startup rate expected=%u SPS measured=%lu SPS CLKIN_assumed=%u Hz OSR=%u inferred_CLKIN=%lu Hz period_min=%lu us period_max=%lu us period_avg_x1000=%lu sync_discard=%lu resync_ack=%lu seq_gap=%lu crc_bad=%lu status_err=%lu spi_err=%lu",
                     (unsigned)NOCTI_V3_ADS_SAMPLE_RATE_HZ,
                     (unsigned long)runtime->measured_sps,
                     (unsigned)NOCTI_V3_ADS_CLKIN_HZ,
                     (unsigned)NOCTI_V3_ADS_OSR,
                     (unsigned long)runtime->inferred_clkin_hz,
                     (unsigned long)(period_min == UINT32_MAX ? 0u : period_min),
                     (unsigned long)period_max,
                     (unsigned long)runtime->measured_period_average_us_x1000,
                     (unsigned long)sync_discarded,
                     (unsigned long)expected_resync_seen,
                     (unsigned long)sequence_gaps,
                     (unsigned long)crc_bad,
                     (unsigned long)status_errors,
                     (unsigned long)spi_errors);
        }
    }
    goto done;

log_and_done:
    ESP_LOGE(TAG,
             "ADS post-SYNC settling failed discard=%lu/%u resync_ack=%lu unexpected_resync=%lu err=%s",
             (unsigned long)sync_discarded,
             (unsigned)NOCTI_V3_ADS_SYNC_DISCARD_FRAMES,
             (unsigned long)expected_resync_seen,
             (unsigned long)unexpected_sync_status,
             esp_err_to_name(err));

done:
    (void)gpio_intr_disable(drdy);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    clear_event_ring(runtime);
    return err;
}

static void set_fault(adc_runtime_t *runtime,
                      nocti_v3_fault_code_t code,
                      esp_err_t error)
{
    runtime->last_error = error;
    __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
    runtime->lifecycle = NOCTI_V3_STATE_FAULTED;
    __atomic_store_n(&runtime->running, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->public_adc->acquisition_running,
                     false,
                     __ATOMIC_RELEASE);
    (void)gpio_intr_disable((gpio_num_t)runtime->board->pins.ads131_drdy);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    nocti_v3_ads131m04_release_bus(&runtime->ads);
    nocti_v3_health_record_fault(runtime->health, code, error);
    nocti_v3_health_set_lifecycle(runtime->health,
                                  NOCTI_V3_STATE_FAULTED,
                                  runtime->current_mode);
}

static esp_err_t start_internal(adc_runtime_t *runtime, nocti_v3_mode_t mode)
{
    if (!runtime->ready || !runtime->rate_valid) return ESP_ERR_INVALID_STATE;
    if (runtime->lifecycle != NOCTI_V3_STATE_STOPPED) return ESP_ERR_INVALID_STATE;
    if (mode != NOCTI_V3_MODE_DARK && mode != NOCTI_V3_MODE_SCAN) return ESP_ERR_INVALID_ARG;
    if (mode == NOCTI_V3_MODE_SCAN && runtime->scan_consumer_task == NULL)
        return ESP_ERR_INVALID_STATE;

    runtime->current_mode = mode;
    runtime->lifecycle = mode == NOCTI_V3_MODE_DARK ?
        NOCTI_V3_STATE_STARTING_DARK : NOCTI_V3_STATE_STARTING_SCAN;
    nocti_v3_health_set_lifecycle(runtime->health, runtime->lifecycle, mode);
    (void)gpio_intr_disable((gpio_num_t)runtime->board->pins.ads131_drdy);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    clear_event_ring(runtime);
    clear_raw_ring(runtime);
    __atomic_store_n(&runtime->session_integrity_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->drdy_overflow_pending, false, __ATOMIC_RELEASE);
    (void)__atomic_add_fetch(&runtime->session_epoch, 1u, __ATOMIC_ACQ_REL);
    runtime->last_runtime_sequence = 0u;
    runtime->last_runtime_timestamp_us = 0u;
    runtime->run_start_timestamp_us = 0u;
    runtime->consecutive_spi_errors = 0u;
    runtime->consecutive_crc_errors = 0u;
    runtime->consecutive_status_errors = 0u;
    runtime->consecutive_period_gaps = 0u;
    runtime->sync_discard_remaining = NOCTI_V3_ADS_SYNC_DISCARD_FRAMES;
    runtime->expected_resync_remaining = NOCTI_V3_ADS_EXPECTED_RESYNC_BUDGET;
    __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);

    esp_err_t err = nocti_v3_ads131m04_acquire_bus(&runtime->ads);
    if (err != ESP_OK) {
        set_fault(runtime, NOCTI_V3_FAULT_SPI, err);
        return err;
    }

    err = nocti_v3_ads131m04_sync(&runtime->ads);
    if (err == ESP_OK) {
        clear_event_ring(runtime);
        __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);
        (void)ulTaskNotifyTake(pdTRUE, 0u);
        __atomic_store_n(&runtime->capture_enabled, true, __ATOMIC_RELEASE);
        err = gpio_intr_enable((gpio_num_t)runtime->board->pins.ads131_drdy);
    }
    if (err != ESP_OK) {
        (void)gpio_intr_disable((gpio_num_t)runtime->board->pins.ads131_drdy);
        __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
        nocti_v3_ads131m04_release_bus(&runtime->ads);
        set_fault(runtime, NOCTI_V3_FAULT_ADC_INTEGRITY, err);
        return err;
    }

    (void)nocti_v3_timebase_now_us(runtime->timebase,
                                    &runtime->run_start_timestamp_us);
    __atomic_store_n(&runtime->running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->public_adc->acquisition_running,
                     true,
                     __ATOMIC_RELEASE);
    runtime->lifecycle = mode == NOCTI_V3_MODE_DARK ?
        NOCTI_V3_STATE_RUNNING_DARK : NOCTI_V3_STATE_RUNNING_SCAN;
    __atomic_store_n(&runtime->optical_epoch_snapshot, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->optical_state_snapshot, (uint16_t)runtime->lifecycle, __ATOMIC_RELEASE);
    nocti_v3_health_set_lifecycle(runtime->health, runtime->lifecycle, mode);
    ESP_LOGI(TAG,
             "%s START session=%lu adc_task_core=%d ISR_init_core=%d sync_discard=%u resync_budget=%u",
             mode == NOCTI_V3_MODE_DARK ? "DARK" : "SCAN",
             (unsigned long)__atomic_load_n(&runtime->session_epoch, __ATOMIC_ACQUIRE),
             runtime->adc_task_core,
             runtime->gpio_isr_init_core,
             (unsigned)NOCTI_V3_ADS_SYNC_DISCARD_FRAMES,
             (unsigned)NOCTI_V3_ADS_EXPECTED_RESYNC_BUDGET);
    return ESP_OK;
}

static esp_err_t stop_internal(adc_runtime_t *runtime)
{
    if (runtime->lifecycle == NOCTI_V3_STATE_STOPPED) return ESP_OK;
    runtime->lifecycle = NOCTI_V3_STATE_STOPPING;
    nocti_v3_health_set_lifecycle(runtime->health,
                                  runtime->lifecycle, runtime->current_mode);

    (void)gpio_intr_disable((gpio_num_t)runtime->board->pins.ads131_drdy);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    clear_event_ring(runtime);
    __atomic_store_n(&runtime->running, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->public_adc->acquisition_running,
                     false,
                     __ATOMIC_RELEASE);

    BaseType_t drained = pdTRUE;
    TaskHandle_t drain_consumer = runtime->current_mode == NOCTI_V3_MODE_SCAN ?
        runtime->scan_consumer_task : runtime->diagnostic_task;
    if (drain_consumer != NULL) {
        (void)xSemaphoreTake(runtime->drain_done, 0u);
        __atomic_store_n(&runtime->drain_requested, true, __ATOMIC_RELEASE);
        xTaskNotifyGive(drain_consumer);
        drained = xSemaphoreTake(runtime->drain_done,
            pdMS_TO_TICKS(NOCTI_V3_ADS_CONTROL_TIMEOUT_MS));
    } else {
        clear_raw_ring(runtime);
    }
    nocti_v3_ads131m04_release_bus(&runtime->ads);
    if (drained != pdTRUE) {
        runtime->last_error = ESP_ERR_TIMEOUT;
        runtime->lifecycle = NOCTI_V3_STATE_FAULTED;
        nocti_v3_health_record_fault(runtime->health,
                                     NOCTI_V3_FAULT_ADC_INTEGRITY,
                                     ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    runtime->lifecycle = NOCTI_V3_STATE_STOPPED;
    nocti_v3_health_set_lifecycle(runtime->health,
                                  runtime->lifecycle,
                                  NOCTI_V3_MODE_STOPPED);
    ESP_LOGI(TAG,
             "%s STOP session=%lu raw_ring_drained=1 callback_core=%d",
             runtime->current_mode == NOCTI_V3_MODE_DARK ? "DARK" : "SCAN",
             (unsigned long)__atomic_load_n(&runtime->session_epoch, __ATOMIC_ACQUIRE),
             __atomic_load_n(&runtime->gpio_isr_callback_core, __ATOMIC_ACQUIRE));
    runtime->current_mode = NOCTI_V3_MODE_STOPPED;
    return ESP_OK;
}

static void process_runtime_events(adc_runtime_t *runtime)
{
    if (__atomic_exchange_n(&runtime->drdy_overflow_pending,
                            false,
                            __ATOMIC_ACQ_REL)) {
        counter_add_u32(&runtime->counters.adc_missed, 1u);
        set_fault(runtime,
                  NOCTI_V3_FAULT_DRDY_EVENT_OVERFLOW,
                  ESP_ERR_INVALID_STATE);
        return;
    }

    drdy_event_t event;
    while (__atomic_load_n(&runtime->running, __ATOMIC_ACQUIRE) &&
           event_pop(runtime, &event)) {
        const uint64_t previous_timestamp = runtime->last_runtime_timestamp_us;
        const bool sync_discard = runtime->sync_discard_remaining > 0u;
        nocti_v3_raw_sample_t sample;
        const esp_err_t err = read_one_event(runtime,
                                             &event,
                                             runtime->last_runtime_sequence,
                                             previous_timestamp,
                                             !sync_discard,
                                             &sample);
        runtime->last_runtime_sequence = event.sequence;
        runtime->last_runtime_timestamp_us = event.timestamp_us;

        if (__atomic_load_n(&runtime->session_integrity_fault,
                            __ATOMIC_ACQUIRE)) {
            set_fault(runtime, NOCTI_V3_FAULT_ADC_INTEGRITY,
                      err == ESP_OK ? ESP_ERR_INVALID_STATE : err);
            break;
        }

        if (sample.spi_result != ESP_OK) {
            ++runtime->consecutive_spi_errors;
        } else {
            runtime->consecutive_spi_errors = 0u;
        }
        if (sample.spi_result == ESP_OK && !sample.crc_valid) {
            ++runtime->consecutive_crc_errors;
        } else {
            runtime->consecutive_crc_errors = 0u;
        }
        if (sample.spi_result == ESP_OK && sample.crc_valid &&
            !sample.status_valid) {
            ++runtime->consecutive_status_errors;
        } else {
            runtime->consecutive_status_errors = 0u;
        }
        if (previous_timestamp != 0u && event.timestamp_us > previous_timestamp &&
            event.timestamp_us - previous_timestamp >
                NOCTI_V3_ADS_PERIOD_GAP_THRESHOLD_US) {
            ++runtime->consecutive_period_gaps;
        } else {
            runtime->consecutive_period_gaps = 0u;
        }

        if (sync_discard) {
            if (sample.spi_result == ESP_OK && sample.crc_valid &&
                sample.status_valid) {
                --runtime->sync_discard_remaining;
                if (runtime->sync_discard_remaining == 0u) {
                    runtime->expected_resync_remaining = 0u;
                }
            }
        } else if (!raw_push(runtime, &sample)) {
            counter_add_u32(&runtime->counters.adc_ring_overflow, 1u);
            set_fault(runtime,
                      NOCTI_V3_FAULT_ADC_RING_OVERFLOW,
                      ESP_ERR_NO_MEM);
            break;
        }
        if (!sync_discard) {
            TaskHandle_t consumer = runtime->current_mode == NOCTI_V3_MODE_SCAN ?
                runtime->scan_consumer_task : runtime->diagnostic_task;
            if (consumer != NULL) xTaskNotifyGive(consumer);
        }

        if (runtime->consecutive_spi_errors >=
            NOCTI_V3_ADC_CONSECUTIVE_SPI_FATAL) {
            set_fault(runtime, NOCTI_V3_FAULT_SPI,
                      sample.spi_result == ESP_OK ? ESP_FAIL : sample.spi_result);
            break;
        }
        if (runtime->consecutive_crc_errors >=
            NOCTI_V3_ADC_CONSECUTIVE_CRC_FATAL) {
            set_fault(runtime, NOCTI_V3_FAULT_ADC_REPEATED_CRC,
                      ESP_ERR_INVALID_RESPONSE);
            break;
        }
        if (runtime->consecutive_status_errors >=
            NOCTI_V3_ADC_CONSECUTIVE_STATUS_FATAL) {
            set_fault(runtime, NOCTI_V3_FAULT_ADC_REPEATED_STATUS,
                      ESP_ERR_INVALID_RESPONSE);
            break;
        }
        if (runtime->consecutive_period_gaps >=
            NOCTI_V3_ADC_CONSECUTIVE_PERIOD_FATAL) {
            set_fault(runtime, NOCTI_V3_FAULT_ADC_RATE,
                      ESP_ERR_INVALID_RESPONSE);
            break;
        }
    }
}

static void check_drdy_stall(adc_runtime_t *runtime)
{
    if (!__atomic_load_n(&runtime->running, __ATOMIC_ACQUIRE)) return;
    uint64_t now_us = 0u;
    if (nocti_v3_timebase_now_us(runtime->timebase, &now_us) != ESP_OK) return;
    const uint64_t reference = runtime->last_runtime_timestamp_us != 0u ?
        runtime->last_runtime_timestamp_us : runtime->run_start_timestamp_us;
    if (reference != 0u && now_us > reference &&
        now_us - reference > NOCTI_V3_ADC_DRDY_STOP_TIMEOUT_US) {
        set_fault(runtime, NOCTI_V3_FAULT_ADC_DRDY_STOPPED,
                  ESP_ERR_TIMEOUT);
    }
}

static void diagnostic_task(void *argument)
{
    adc_runtime_t *runtime = (adc_runtime_t *)argument;
    __atomic_store_n(&runtime->diagnostic_task_core,
                     xPortGetCoreID(),
                     __ATOMIC_RELEASE);
    uint64_t last_watermark_us = 0u;

    for (;;) {
        nocti_v3_raw_sample_t sample;
        if (runtime->current_mode != NOCTI_V3_MODE_SCAN) {
            while (raw_pop(runtime, &sample)) { (void)sample; }
        }

        if (__atomic_load_n(&runtime->drain_requested, __ATOMIC_ACQUIRE) &&
            raw_depth(runtime) == 0u) {
            __atomic_store_n(&runtime->drain_requested, false, __ATOMIC_RELEASE);
            (void)xSemaphoreGive(runtime->drain_done);
        }

        uint64_t now_us = 0u;
        (void)nocti_v3_timebase_now_us(runtime->timebase, &now_us);
        if (last_watermark_us == 0u ||
            now_us - last_watermark_us >=
                (uint64_t)NOCTI_V3_DARK_HEALTH_PERIOD_MS * 1000u) {
            __atomic_store_n(&runtime->adc_stack_watermark_bytes,
                             uxTaskGetStackHighWaterMark(runtime->adc_task),
                             __ATOMIC_RELEASE);
            last_watermark_us = now_us;
        }
        __atomic_store_n(&runtime->diagnostic_stack_watermark_bytes,
                         uxTaskGetStackHighWaterMark(NULL),
                         __ATOMIC_RELEASE);
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10u));
    }
}

static void adc_task(void *argument)
{
    adc_runtime_t *runtime = (adc_runtime_t *)argument;
    runtime->adc_task_core = xPortGetCoreID();
    __atomic_store_n(&runtime->gpio_isr_callback_core,
                     -1,
                     __ATOMIC_RELEASE);
    runtime->lifecycle = NOCTI_V3_STATE_STOPPED;

    esp_err_t err = nocti_v3_timebase_init(runtime->timebase);
    if (err == ESP_OK) err = install_drdy_interrupt(runtime);
    if (err == ESP_OK) {
        err = nocti_v3_ads131m04_init(&runtime->ads,
                                      runtime->board,
                                      runtime->timebase);
    }
    if (err == ESP_OK) {
        err = nocti_v3_ads131m04_acquire_bus(&runtime->ads);
    }
    if (err == ESP_OK) err = nocti_v3_ads131m04_hardware_reset(&runtime->ads);
    if (err == ESP_OK) err = nocti_v3_ads131m04_configure_8ksps(&runtime->ads);
    if (err == ESP_OK) {
        err = nocti_v3_ads131m04_read_registers(&runtime->ads,
                                                &runtime->registers);
    }
    if (err == ESP_OK) err = validate_rate(runtime);
    nocti_v3_ads131m04_release_bus(&runtime->ads);

    runtime->init_result = err;
    runtime->initialized = true;
    runtime->ready = err == ESP_OK && runtime->rate_valid;
    runtime->public_adc->interface_initialized = true;
    runtime->public_adc->hardware_initialized = err == ESP_OK;
    runtime->public_adc->ready = runtime->ready;
    runtime->last_error = err;

    if (err != ESP_OK) {
        const nocti_v3_fault_code_t fault = runtime->measured_sps != 0u ?
            NOCTI_V3_FAULT_ADC_RATE : NOCTI_V3_FAULT_ADC_INIT;
        nocti_v3_health_record_fault(runtime->health, fault, err);
        runtime->lifecycle = NOCTI_V3_STATE_FAULTED;
    }

    const BaseType_t diagnostic_created = xTaskCreatePinnedToCore(
        diagnostic_task,
        "nocti_v3_dark",
        NOCTI_V3_STACK_OPTICAL_BYTES,
        runtime,
        NOCTI_V3_PRIO_OPTICAL_PROCESS,
        &runtime->diagnostic_task,
        NOCTI_V3_CORE_NETWORK);
    if (diagnostic_created != pdPASS) {
        runtime->init_result = ESP_ERR_NO_MEM;
        runtime->last_error = ESP_ERR_NO_MEM;
        runtime->ready = false;
        runtime->public_adc->ready = false;
        runtime->lifecycle = NOCTI_V3_STATE_FAULTED;
        nocti_v3_health_record_fault(runtime->health,
                                     NOCTI_V3_FAULT_ADC_INIT,
                                     ESP_ERR_NO_MEM);
    }
    (void)xSemaphoreGive(runtime->init_done);

    for (;;) {
        adc_command_t command;
        while (xQueueReceive(runtime->command_queue, &command, 0u) == pdTRUE) {
            switch (command.type) {
                case ADC_COMMAND_START_DARK:
                    runtime->command_result = start_internal(runtime, NOCTI_V3_MODE_DARK);
                    break;
                case ADC_COMMAND_START_SCAN:
                    runtime->command_result = start_internal(runtime, NOCTI_V3_MODE_SCAN);
                    break;
                case ADC_COMMAND_STOP:
                    runtime->command_result = stop_internal(runtime);
                    break;
                default:
                    runtime->command_result = ESP_ERR_INVALID_ARG;
                    break;
            }
            (void)xSemaphoreGive(runtime->command_done);
        }

        if (__atomic_load_n(&runtime->running, __ATOMIC_ACQUIRE)) {
            process_runtime_events(runtime);
            check_drdy_stall(runtime);
        }
        __atomic_store_n(&runtime->adc_stack_watermark_bytes,
                         uxTaskGetStackHighWaterMark(NULL),
                         __ATOMIC_RELEASE);
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(__atomic_load_n(&runtime->running, __ATOMIC_ACQUIRE) ? 1u : 100u));
    }
}

static esp_err_t execute_command(nocti_v3_adc_t *adc, adc_command_type_t type)
{
    if (adc == NULL || adc->runtime == NULL) return ESP_ERR_INVALID_STATE;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
    if (xSemaphoreTake(runtime->command_lock,
                       pdMS_TO_TICKS(NOCTI_V3_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    (void)xSemaphoreTake(runtime->command_done, 0u);
    const adc_command_t command = {.type = type};
    esp_err_t result = ESP_OK;
    if (xQueueSend(runtime->command_queue,
                   &command,
                   pdMS_TO_TICKS(NOCTI_V3_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    } else {
        xTaskNotifyGive(runtime->adc_task);
        if (xSemaphoreTake(runtime->command_done,
                           pdMS_TO_TICKS(NOCTI_V3_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
        } else {
            result = runtime->command_result;
        }
    }
    (void)xSemaphoreGive(runtime->command_lock);
    return result;
}

esp_err_t nocti_v3_adc_init(nocti_v3_adc_t *adc,
                            nocti_v3_board_t *board,
                            nocti_v3_timebase_t *timebase,
                            nocti_v3_health_t *health)
{
    if (adc == NULL || board == NULL || timebase == NULL || health == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(adc, 0, sizeof(*adc));
    memset(&s_runtime, 0, sizeof(s_runtime));
    adc->target_sample_rate_hz = NOCTI_V3_ADS_SAMPLE_RATE_HZ;
    adc->spi_clock_hz = NOCTI_V3_ADS_SPI_CLOCK_HZ;
    adc->spi_mode = NOCTI_V3_ADS_SPI_MODE;
    adc->runtime = &s_runtime;

    adc_runtime_t *runtime = &s_runtime;
    runtime->public_adc = adc;
    runtime->board = board;
    runtime->timebase = timebase;
    runtime->health = health;
    __atomic_store_n(&runtime->gpio_isr_callback_core,
                     -1,
                     __ATOMIC_RELEASE);

    runtime->init_done = xSemaphoreCreateBinaryStatic(&runtime->init_done_storage);
    runtime->command_done = xSemaphoreCreateBinaryStatic(&runtime->command_done_storage);
    runtime->command_lock = xSemaphoreCreateMutexStatic(&runtime->command_lock_storage);
    runtime->drain_done = xSemaphoreCreateBinaryStatic(&runtime->drain_done_storage);
    runtime->command_queue = xQueueCreateStatic(
        4u,
        sizeof(adc_command_t),
        runtime->command_queue_buffer,
        &runtime->command_queue_storage);
    if (runtime->init_done == NULL || runtime->command_done == NULL ||
        runtime->command_lock == NULL || runtime->drain_done == NULL ||
        runtime->command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        adc_task,
        "nocti_v3_adc",
        NOCTI_V3_STACK_ADC_BYTES,
        runtime,
        NOCTI_V3_PRIO_ADC,
        &runtime->adc_task,
        NOCTI_V3_CORE_REALTIME);
    if (created != pdPASS) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(runtime->init_done,
                       pdMS_TO_TICKS(NOCTI_V3_ADS_INIT_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return runtime->init_result;
}

esp_err_t nocti_v3_adc_start_dark(nocti_v3_adc_t *adc)
{ return execute_command(adc, ADC_COMMAND_START_DARK); }

esp_err_t nocti_v3_adc_start_scan(nocti_v3_adc_t *adc)
{ return execute_command(adc, ADC_COMMAND_START_SCAN); }

esp_err_t nocti_v3_adc_bind_scan_consumer(nocti_v3_adc_t *adc, TaskHandle_t task)
{
    if (adc == NULL || adc->runtime == NULL || task == NULL) return ESP_ERR_INVALID_ARG;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
    if (__atomic_load_n(&runtime->running, __ATOMIC_ACQUIRE)) return ESP_ERR_INVALID_STATE;
    runtime->scan_consumer_task = task;
    return ESP_OK;
}

bool nocti_v3_adc_pop_raw_sample(nocti_v3_adc_t *adc, nocti_v3_raw_sample_t *out_sample)
{
    return adc != NULL && adc->runtime != NULL && out_sample != NULL &&
           raw_pop((adc_runtime_t *)adc->runtime, out_sample);
}

uint32_t nocti_v3_adc_raw_depth(nocti_v3_adc_t *adc)
{ return adc != NULL && adc->runtime != NULL ? raw_depth((adc_runtime_t *)adc->runtime) : 0u; }

bool nocti_v3_adc_drain_requested(nocti_v3_adc_t *adc)
{ return adc != NULL && adc->runtime != NULL &&
         __atomic_load_n(&((adc_runtime_t *)adc->runtime)->drain_requested, __ATOMIC_ACQUIRE); }

esp_err_t nocti_v3_adc_acknowledge_drain(nocti_v3_adc_t *adc)
{
    if (adc == NULL || adc->runtime == NULL) return ESP_ERR_INVALID_STATE;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
    if (!__atomic_exchange_n(&runtime->drain_requested, false, __ATOMIC_ACQ_REL))
        return ESP_ERR_INVALID_STATE;
    return xSemaphoreGive(runtime->drain_done) == pdTRUE ? ESP_OK : ESP_FAIL;
}

void nocti_v3_adc_set_optical_snapshot(nocti_v3_adc_t *adc,
                                       uint32_t optical_epoch,
                                       uint16_t optical_state)
{
    if (adc == NULL || adc->runtime == NULL) return;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
    __atomic_store_n(&runtime->optical_epoch_snapshot, optical_epoch, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->optical_state_snapshot, optical_state, __ATOMIC_RELEASE);
}

esp_err_t nocti_v3_adc_stop(nocti_v3_adc_t *adc)
{
    return execute_command(adc, ADC_COMMAND_STOP);
}

bool nocti_v3_adc_ready(const nocti_v3_adc_t *adc)
{
    return adc != NULL && adc->ready && adc->hardware_initialized;
}

bool nocti_v3_adc_running(const nocti_v3_adc_t *adc)
{
    return adc != NULL &&
           __atomic_load_n(&adc->acquisition_running, __ATOMIC_ACQUIRE);
}

void nocti_v3_adc_health_snapshot(nocti_v3_adc_t *adc,
                                  nocti_v3_adc_health_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (adc == NULL || adc->runtime == NULL) return;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
#define COPY_COUNTER(name) \
    out_snapshot->name = __atomic_load_n(&runtime->counters.name, __ATOMIC_ACQUIRE)
    COPY_COUNTER(adc_ok);
    COPY_COUNTER(adc_missed);
    COPY_COUNTER(adc_sequence_gap);
    COPY_COUNTER(adc_ring_overflow);
    COPY_COUNTER(adc_period_gap);
    COPY_COUNTER(crc_bad);
    COPY_COUNTER(ads_status_errors);
    COPY_COUNTER(spi_errors);
    out_snapshot->consecutive_spi_errors = runtime->consecutive_spi_errors;
    out_snapshot->consecutive_crc_errors = runtime->consecutive_crc_errors;
    out_snapshot->consecutive_status_errors = runtime->consecutive_status_errors;
    out_snapshot->consecutive_period_gaps = runtime->consecutive_period_gaps;
    COPY_COUNTER(adc_period_min_us);
    COPY_COUNTER(adc_period_max_us);
    COPY_COUNTER(adc_max_gap_us);
    COPY_COUNTER(spi_duration_max_us);
    COPY_COUNTER(raw_ring_depth_max);
    out_snapshot->event_ring_depth_max = __atomic_load_n(
        &runtime->event_ring_depth_max_isr, __ATOMIC_ACQUIRE);
#undef COPY_COUNTER
    out_snapshot->drdy_event_overflow = __atomic_load_n(
        &runtime->drdy_event_overflow_isr, __ATOMIC_ACQUIRE);
    out_snapshot->adc_task_stack_watermark_bytes = __atomic_load_n(
        &runtime->adc_stack_watermark_bytes, __ATOMIC_ACQUIRE);
    out_snapshot->diagnostic_task_stack_watermark_bytes = __atomic_load_n(
        &runtime->diagnostic_stack_watermark_bytes, __ATOMIC_ACQUIRE);
    out_snapshot->raw_ring_depth = raw_depth(runtime);
    out_snapshot->session_epoch = __atomic_load_n(
        &runtime->session_epoch, __ATOMIC_ACQUIRE);
    out_snapshot->measured_sps = runtime->measured_sps;
    out_snapshot->measured_period_average_us_x1000 =
        runtime->measured_period_average_us_x1000;
    out_snapshot->inferred_clkin_hz = runtime->inferred_clkin_hz;
    out_snapshot->adc_task_core = runtime->adc_task_core;
    out_snapshot->diagnostic_task_core = __atomic_load_n(
        &runtime->diagnostic_task_core, __ATOMIC_ACQUIRE);
    out_snapshot->gpio_isr_init_core = runtime->gpio_isr_init_core;
    out_snapshot->gpio_isr_callback_core = __atomic_load_n(
        &runtime->gpio_isr_callback_core, __ATOMIC_ACQUIRE);
    out_snapshot->ready = runtime->ready;
    out_snapshot->acquisition_running = __atomic_load_n(
        &runtime->running, __ATOMIC_ACQUIRE);
    out_snapshot->rate_valid = runtime->rate_valid;
    out_snapshot->session_integrity_fault = __atomic_load_n(
        &runtime->session_integrity_fault, __ATOMIC_ACQUIRE);
    out_snapshot->lifecycle = runtime->lifecycle;
    out_snapshot->last_error = runtime->last_error;
}

void nocti_v3_adc_register_snapshot(nocti_v3_adc_t *adc,
                                    nocti_v3_adc_register_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (adc == NULL || adc->runtime == NULL) return;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
    *out_snapshot = (nocti_v3_adc_register_snapshot_t) {
        .id = runtime->registers.id,
        .status = runtime->registers.status,
        .mode = runtime->registers.mode,
        .clock = runtime->registers.clock,
        .gain = runtime->registers.gain,
        .cfg = runtime->registers.cfg,
    };
}

esp_err_t nocti_v3_adc_reset_counters(nocti_v3_adc_t *adc)
{
    if (adc == NULL || adc->runtime == NULL) return ESP_ERR_INVALID_STATE;
    adc_runtime_t *runtime = (adc_runtime_t *)adc->runtime;
    if (__atomic_load_n(&runtime->running, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
#define RESET_COUNTER(name) __atomic_store_n(&runtime->counters.name, 0, __ATOMIC_RELEASE)
    RESET_COUNTER(adc_ok);
    RESET_COUNTER(adc_missed);
    RESET_COUNTER(adc_sequence_gap);
    RESET_COUNTER(adc_ring_overflow);
    RESET_COUNTER(adc_period_gap);
    RESET_COUNTER(crc_bad);
    RESET_COUNTER(ads_status_errors);
    RESET_COUNTER(spi_errors);
    RESET_COUNTER(adc_period_min_us);
    RESET_COUNTER(adc_period_max_us);
    RESET_COUNTER(adc_max_gap_us);
    RESET_COUNTER(spi_duration_max_us);
    RESET_COUNTER(raw_ring_depth_max);
#undef RESET_COUNTER
    __atomic_store_n(&runtime->event_ring_depth_max_isr, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->drdy_event_overflow_isr, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->drdy_overflow_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->session_integrity_fault, false, __ATOMIC_RELEASE);
    runtime->consecutive_spi_errors = 0u;
    runtime->consecutive_crc_errors = 0u;
    runtime->consecutive_status_errors = 0u;
    runtime->consecutive_period_gaps = 0u;
    return ESP_OK;
}
