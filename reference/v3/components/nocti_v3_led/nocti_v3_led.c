#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_led.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nocti_v3_config.h"

static const char *TAG = "nocti_v3_led";

#define PCA_MODE1       0x00u
#define PCA_MODE2       0x01u
#define PCA_LEDOUT0     0x02u
#define PCA_LEDOUT5     0x07u
#define PCA_GRPPWM      0x08u
#define PCA_GRPFREQ     0x09u
#define PCA_IREF0       0x22u
#define PCA_PWMALL      0x3Fu
#define PCA_MODE1_AI1   0x40u
#define PCA_LED_ON      0x01u
#define COMPLETION_MASK (NOCTI_V3_LED_COMPLETION_RING_CAPACITY - 1u)

static const uint8_t k_slot_to_pca[NOCTI_V3_SLOT_COUNT] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
    14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u,
};

typedef struct {
    nocti_v3_led_t *public_led;
    nocti_v3_board_t *board;
    nocti_v3_timebase_t *timebase;
    nocti_v3_health_t *health;
    TaskHandle_t led_task;
    TaskHandle_t recovery_task;
    TaskHandle_t completion_consumer;
    SemaphoreHandle_t init_done;
    SemaphoreHandle_t recovery_sem;
    StaticSemaphore_t init_done_storage;
    StaticSemaphore_t recovery_sem_storage;
    QueueHandle_t request_queue;
    StaticQueue_t request_queue_storage;
    uint8_t request_queue_buffer[NOCTI_V3_LED_REQUEST_QUEUE_CAPACITY *
                                 sizeof(nocti_v3_led_request_t)];

    volatile bool initialized;
    volatile bool runtime_ready;
    volatile bool callbacks_registered;
    volatile bool transaction_pending;
    volatile bool recovery_pending;
    volatile esp_err_t init_result;
    volatile esp_err_t last_error;
    volatile nocti_v3_led_state_t state;
    volatile int i2c_callback_core;
    int led_task_core;
    int recovery_task_core;
    int i2c_init_core;

    nocti_v3_led_request_t pending_request;
    uint64_t pending_submit_us;
    uint8_t slot_payload[NOCTI_V3_SLOT_COUNT][7];
    uint8_t all_off_payload[7];
    uint8_t iref_values[24];
    uint16_t current_735_ma;
    uint16_t current_850_ma;

    volatile uint32_t completion_head;
    volatile uint32_t completion_tail;
    nocti_v3_led_completion_t completion_ring[NOCTI_V3_LED_COMPLETION_RING_CAPACITY];

    volatile uint32_t led_errors;
    volatile uint32_t i2c_nack;
    volatile uint32_t i2c_timeout;
    volatile uint32_t i2c_queue_full;
    volatile uint32_t completion_overflow;
    volatile uint32_t stale_completion;
    volatile uint32_t recovery_count;
    volatile uint32_t hardware_reset_count;
    volatile uint32_t commit_latency_max_us;
    volatile uint32_t consecutive_errors;
    volatile uint32_t safe_off_count;
    volatile uint32_t led_stack_watermark;
    volatile uint32_t recovery_stack_watermark;
} led_runtime_t;

static led_runtime_t s_runtime;

static void IRAM_ATTR update_max_u32(volatile uint32_t *value, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static uint8_t current_to_iref(uint16_t current_ma)
{
    if (current_ma == 0u) return 0u;
    const uint64_t numerator = (uint64_t)current_ma * 1000u *
                               (uint64_t)(4u * NOCTI_V3_PCA_REXT_OHM);
    uint32_t value = (uint32_t)((numerator + 450000u) / 900000u);
    return (uint8_t)(value > 255u ? 255u : value);
}

static void build_persistent_payloads(led_runtime_t *runtime)
{
    memset(runtime->slot_payload, 0, sizeof(runtime->slot_payload));
    runtime->all_off_payload[0] = (uint8_t)(0x80u | PCA_LEDOUT0);
    memset(&runtime->all_off_payload[1], 0, 6u);
    memset(runtime->iref_values, 0, sizeof(runtime->iref_values));
    for (uint8_t slot = 0u; slot < NOCTI_V3_SLOT_COUNT; ++slot) {
        uint8_t *payload = runtime->slot_payload[slot];
        payload[0] = (uint8_t)(0x80u | PCA_LEDOUT0);
        const uint8_t channel = k_slot_to_pca[slot];
        const uint8_t byte_index = (uint8_t)(1u + channel / 4u);
        const uint8_t shift = (uint8_t)((channel % 4u) * 2u);
        payload[byte_index] = (uint8_t)(PCA_LED_ON << shift);
        runtime->iref_values[channel] = current_to_iref(
            (slot & 1u) == 0u ? runtime->current_735_ma :
                                runtime->current_850_ma);
    }
}

static esp_err_t sync_write_raw(led_runtime_t *runtime,
                                const uint8_t *data, size_t length)
{
    if (runtime->callbacks_registered || runtime->transaction_pending) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(runtime->board->pca_device, data, length,
                               NOCTI_V3_I2C_INIT_TIMEOUT_MS);
}

static esp_err_t sync_write_reg(led_runtime_t *runtime,
                                uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return sync_write_raw(runtime, data, sizeof(data));
}

static esp_err_t sync_read_reg(led_runtime_t *runtime,
                               uint8_t reg, uint8_t *value)
{
    if (value == NULL || runtime->callbacks_registered ||
        runtime->transaction_pending) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(runtime->board->pca_device,
                                       &reg, 1u, value, 1u,
                                       NOCTI_V3_I2C_INIT_TIMEOUT_MS);
}

static esp_err_t reset_and_program_sync(led_runtime_t *runtime)
{
    esp_err_t err = nocti_v3_board_assert_pca_reset(runtime->board);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(NOCTI_V3_PCA_RESET_LOW_US);
    err = nocti_v3_board_release_pca_reset(runtime->board);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(NOCTI_V3_PCA_RESET_RECOVERY_US);
    (void)__atomic_fetch_add(&runtime->hardware_reset_count, 1u, __ATOMIC_RELAXED);

    uint8_t mode1 = 0u;
    err = sync_read_reg(runtime, PCA_MODE1, &mode1);
    if (err != ESP_OK) return err;
    err = sync_write_reg(runtime, PCA_MODE1, PCA_MODE1_AI1);
    if (err != ESP_OK) return err;
    /* OCH=0: all LEDOUT registers commit together on STOP. */
    err = sync_write_reg(runtime, PCA_MODE2, 0x00u);
    if (err != ESP_OK) return err;
    err = sync_write_reg(runtime, PCA_GRPPWM, 0x00u);
    if (err != ESP_OK) return err;
    err = sync_write_reg(runtime, PCA_GRPFREQ, 0x00u);
    if (err != ESP_OK) return err;
    err = sync_write_reg(runtime, PCA_PWMALL, 0x00u);
    if (err != ESP_OK) return err;
    err = sync_write_raw(runtime, runtime->all_off_payload,
                         sizeof(runtime->all_off_payload));
    if (err != ESP_OK) return err;
    uint8_t iref_payload[25];
    iref_payload[0] = (uint8_t)(0x80u | PCA_IREF0);
    memcpy(&iref_payload[1], runtime->iref_values, 24u);
    err = sync_write_raw(runtime, iref_payload, sizeof(iref_payload));
    if (err != ESP_OK) return err;

    uint8_t rb_mode1 = 0u, rb_mode2 = 0u;
    err = sync_read_reg(runtime, PCA_MODE1, &rb_mode1);
    if (err != ESP_OK) return err;
    err = sync_read_reg(runtime, PCA_MODE2, &rb_mode2);
    if (err != ESP_OK) return err;
    if ((rb_mode1 & 0x7Fu) != PCA_MODE1_AI1 || (rb_mode2 & 0x28u) != 0u) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (uint8_t reg = PCA_LEDOUT0; reg <= PCA_LEDOUT5; ++reg) {
        uint8_t value = 0xFFu;
        err = sync_read_reg(runtime, reg, &value);
        if (err != ESP_OK || value != 0u) {
            return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
        }
    }
    runtime->state = NOCTI_V3_LED_CONFIGURED_ALL_OFF;
    runtime->public_led->state = runtime->state;
    ESP_LOGI(TAG,
             "PCA init verified addr=0x%02X MODE1=0x%02X MODE2=0x%02X change_on_STOP=1",
             (unsigned)runtime->board->config.pca9956b_address_7bit,
             (unsigned)rb_mode1, (unsigned)rb_mode2);
    return ESP_OK;
}

static bool IRAM_ATTR i2c_done_callback(i2c_master_dev_handle_t device,
                                        const i2c_master_event_data_t *event,
                                        void *user_data)
{
    (void)device;
    led_runtime_t *runtime = (led_runtime_t *)user_data;
    BaseType_t high_priority_woken = pdFALSE;
    uint64_t complete_us = 0u;
    (void)nocti_v3_timebase_now_us_isr(runtime->timebase, &complete_us);
    __atomic_store_n(&runtime->i2c_callback_core, xPortGetCoreID(),
                     __ATOMIC_RELAXED);

    nocti_v3_led_completion_t completion = {
        .request = runtime->pending_request,
        .result = NOCTI_V3_LED_RESULT_DONE,
        .submit_result = ESP_OK,
        .i2c_submit_timestamp_us = runtime->pending_submit_us,
        .i2c_complete_timestamp_us = complete_us,
        .callback_core = xPortGetCoreID(),
    };
    if (event != NULL) {
        if (event->event == I2C_EVENT_NACK) {
            completion.result = NOCTI_V3_LED_RESULT_NACK;
            (void)__atomic_fetch_add(&runtime->i2c_nack, 1u, __ATOMIC_RELAXED);
        } else if (event->event == I2C_EVENT_TIMEOUT) {
            completion.result = NOCTI_V3_LED_RESULT_TIMEOUT;
            (void)__atomic_fetch_add(&runtime->i2c_timeout, 1u, __ATOMIC_RELAXED);
        }
    }
    if (completion.result == NOCTI_V3_LED_RESULT_DONE) {
        __atomic_store_n(&runtime->consecutive_errors, 0u, __ATOMIC_RELEASE);
    }
    if (complete_us >= runtime->pending_submit_us) {
        update_max_u32(&runtime->commit_latency_max_us,
                       (uint32_t)(complete_us - runtime->pending_submit_us));
    }
    const uint32_t head = __atomic_load_n(&runtime->completion_head,
                                          __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->completion_tail,
                                          __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V3_LED_COMPLETION_RING_CAPACITY) {
        (void)__atomic_fetch_add(&runtime->completion_overflow, 1u,
                                 __ATOMIC_RELAXED);
    } else {
        runtime->completion_ring[head & COMPLETION_MASK] = completion;
        __atomic_store_n(&runtime->completion_head, head + 1u,
                         __ATOMIC_RELEASE);
    }
    __atomic_store_n(&runtime->transaction_pending, false, __ATOMIC_RELEASE);
    if (runtime->led_task != NULL) {
        vTaskNotifyGiveFromISR(runtime->led_task, &high_priority_woken);
    }
    TaskHandle_t consumer = runtime->completion_consumer;
    if (consumer != NULL) {
        (void)xTaskNotifyFromISR(consumer, (1u << 1), eSetBits,
                                 &high_priority_woken);
    }
    return high_priority_woken == pdTRUE;
}

static esp_err_t enter_sync_configuration(led_runtime_t *runtime)
{
    runtime->runtime_ready = false;

    /* V2-validated rule: queue depth, not callback registration, selects the
     * ESP-IDF asynchronous path. Therefore configuration/readback must use a
     * physically recreated qdepth=0 bus. */
    if (runtime->callbacks_registered && runtime->board->pca_device != NULL) {
        const i2c_master_event_callbacks_t callbacks = {
            .on_trans_done = NULL,
        };
        const esp_err_t unregister_err = i2c_master_register_event_callbacks(
            runtime->board->pca_device, &callbacks, NULL);
        if (unregister_err != ESP_OK) return unregister_err;
        runtime->callbacks_registered = false;
    }

    if (!runtime->board->i2c_ready || runtime->board->i2c_queue_depth != 0u) {
        return nocti_v3_board_i2c_reconfigure_queue_depth(runtime->board, 0u);
    }
    return ESP_OK;
}

static esp_err_t enter_async_runtime(led_runtime_t *runtime)
{
    if (!runtime->board->i2c_ready ||
        runtime->board->i2c_queue_depth != NOCTI_V3_I2C_ASYNC_QUEUE_DEPTH) {
        esp_err_t err = nocti_v3_board_i2c_reconfigure_queue_depth(
            runtime->board, NOCTI_V3_I2C_ASYNC_QUEUE_DEPTH);
        if (err != ESP_OK) return err;
    }

    const i2c_master_event_callbacks_t callbacks = {
        .on_trans_done = i2c_done_callback,
    };
    const esp_err_t err = i2c_master_register_event_callbacks(
        runtime->board->pca_device, &callbacks, runtime);
    if (err != ESP_OK) {
        (void)nocti_v3_board_i2c_reconfigure_queue_depth(runtime->board, 0u);
        return err;
    }
    runtime->callbacks_registered = true;
    runtime->i2c_init_core = xPortGetCoreID();
    runtime->runtime_ready = true;
    runtime->state = NOCTI_V3_LED_RUNTIME_READY;
    runtime->public_led->state = runtime->state;
    ESP_LOGI(TAG,
             "PCA runtime path ready phase=ASYNC qdepth=%lu persistent_payload=1 callback=1",
             (unsigned long)runtime->board->i2c_queue_depth);
    return ESP_OK;
}

static esp_err_t initialize_device(led_runtime_t *runtime)
{
    runtime->runtime_ready = false;
    esp_err_t err = enter_sync_configuration(runtime);
    if (err == ESP_OK) err = reset_and_program_sync(runtime);
    if (err == ESP_OK) err = enter_async_runtime(runtime);
    if (err != ESP_OK) {
        (void)nocti_v3_board_assert_pca_reset(runtime->board);
        runtime->state = NOCTI_V3_LED_FAULT;
        runtime->public_led->state = runtime->state;
        runtime->last_error = err;
    }
    return err;
}

static void publish_submit_error(led_runtime_t *runtime,
                                 const nocti_v3_led_request_t *request,
                                 esp_err_t result, uint64_t submit_us)
{
    nocti_v3_led_completion_t completion = {
        .request = *request,
        .result = NOCTI_V3_LED_RESULT_SUBMIT_ERROR,
        .submit_result = result,
        .i2c_submit_timestamp_us = submit_us,
        .i2c_complete_timestamp_us = submit_us,
        .callback_core = -1,
    };
    const uint32_t head = __atomic_load_n(&runtime->completion_head,
                                          __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->completion_tail,
                                          __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V3_LED_COMPLETION_RING_CAPACITY) {
        (void)__atomic_fetch_add(&runtime->completion_overflow, 1u,
                                 __ATOMIC_RELAXED);
    } else {
        runtime->completion_ring[head & COMPLETION_MASK] = completion;
        __atomic_store_n(&runtime->completion_head, head + 1u,
                         __ATOMIC_RELEASE);
    }
    if (runtime->completion_consumer != NULL) {
        xTaskNotify(runtime->completion_consumer, (1u << 1), eSetBits);
    }
}

static void led_control_task(void *argument)
{
    led_runtime_t *runtime = (led_runtime_t *)argument;
    runtime->led_task_core = xPortGetCoreID();
    runtime->init_result = initialize_device(runtime);
    runtime->initialized = runtime->init_result == ESP_OK;
    runtime->public_led->last_error = runtime->init_result;
    runtime->public_led->state = runtime->state;
    (void)xSemaphoreGive(runtime->init_done);

    for (;;) {
        if (__atomic_load_n(&runtime->transaction_pending, __ATOMIC_ACQUIRE)) {
            (void)ulTaskNotifyTake(pdTRUE,
                                   pdMS_TO_TICKS(NOCTI_V3_I2C_RUNTIME_TIMEOUT_MS));
            continue;
        }
        nocti_v3_led_request_t request;
        if (xQueueReceive(runtime->request_queue, &request,
                          pdMS_TO_TICKS(100u)) == pdTRUE) {
            uint64_t submit_us = 0u;
            (void)nocti_v3_timebase_now_us(runtime->timebase, &submit_us);
            if (!runtime->runtime_ready || !runtime->board->i2c_ready ||
                runtime->board->i2c_queue_depth == 0u) {
                publish_submit_error(runtime, &request,
                                     ESP_ERR_INVALID_STATE, submit_us);
                continue;
            }
            const uint8_t *payload = request.type == NOCTI_V3_LED_COMMAND_SLOT_ON &&
                                     request.slot_index < NOCTI_V3_SLOT_COUNT ?
                                     runtime->slot_payload[request.slot_index] :
                                     runtime->all_off_payload;
            runtime->pending_request = request;
            runtime->pending_submit_us = submit_us;
            __atomic_store_n(&runtime->transaction_pending, true,
                             __ATOMIC_RELEASE);
            const esp_err_t err = i2c_master_transmit(
                runtime->board->pca_device, payload, 7u,
                NOCTI_V3_I2C_RUNTIME_TIMEOUT_MS);
            if (err != ESP_OK) {
                __atomic_store_n(&runtime->transaction_pending, false,
                                 __ATOMIC_RELEASE);
                (void)__atomic_fetch_add(&runtime->led_errors, 1u,
                                         __ATOMIC_RELAXED);
                runtime->last_error = err;
                publish_submit_error(runtime, &request, err, submit_us);
            }
        }
        __atomic_store_n(&runtime->led_stack_watermark,
                         uxTaskGetStackHighWaterMark(NULL), __ATOMIC_RELEASE);
    }
}

static void recovery_task(void *argument)
{
    led_runtime_t *runtime = (led_runtime_t *)argument;
    runtime->recovery_task_core = xPortGetCoreID();
    for (;;) {
        (void)xSemaphoreTake(runtime->recovery_sem, portMAX_DELAY);
        runtime->state = NOCTI_V3_LED_RECOVERING;
        runtime->public_led->state = runtime->state;
        runtime->runtime_ready = false;
        (void)nocti_v3_board_assert_pca_reset(runtime->board);
        (void)__atomic_fetch_add(&runtime->safe_off_count, 1u,
                                 __ATOMIC_RELAXED);
        esp_err_t err = nocti_v3_led_wait_idle(runtime->public_led,
                                               NOCTI_V3_I2C_RUNTIME_TIMEOUT_MS);
        if (err == ESP_OK) {
            err = nocti_v3_board_i2c_wait_all_done(
                runtime->board, NOCTI_V3_I2C_RUNTIME_TIMEOUT_MS);
        }
        if (err == ESP_OK) {
            if (xQueueReset(runtime->request_queue) != pdPASS) {
                err = ESP_FAIL;
            }
        }
        if (err == ESP_OK) {
            const uint32_t head = __atomic_load_n(&runtime->completion_head,
                                                  __ATOMIC_ACQUIRE);
            __atomic_store_n(&runtime->completion_tail, head, __ATOMIC_RELEASE);
            vTaskDelay(pdMS_TO_TICKS(NOCTI_V3_LED_RECOVERY_COOLDOWN_MS));
            err = initialize_device(runtime);
        }
        runtime->last_error = err;
        (void)__atomic_fetch_add(&runtime->recovery_count, 1u,
                                 __ATOMIC_RELAXED);
        __atomic_store_n(&runtime->recovery_pending, false, __ATOMIC_RELEASE);
        if (err == ESP_OK) {
            __atomic_store_n(&runtime->consecutive_errors, 0u,
                             __ATOMIC_RELEASE);
        } else {
            runtime->state = NOCTI_V3_LED_FAULT;
            runtime->public_led->state = runtime->state;
            nocti_v3_health_record_fault(runtime->health,
                                         NOCTI_V3_FAULT_LED_RECOVERY, err);
        }
        __atomic_store_n(&runtime->recovery_stack_watermark,
                         uxTaskGetStackHighWaterMark(NULL), __ATOMIC_RELEASE);
        if (runtime->completion_consumer != NULL) {
            xTaskNotify(runtime->completion_consumer, (1u << 2), eSetBits);
        }
    }
}

esp_err_t nocti_v3_led_init(nocti_v3_led_t *led,
                            nocti_v3_board_t *board,
                            nocti_v3_timebase_t *timebase,
                            nocti_v3_health_t *health)
{
    if (led == NULL || board == NULL || timebase == NULL || health == NULL ||
        !nocti_v3_timebase_ready(timebase)) return ESP_ERR_INVALID_ARG;
    memset(led, 0, sizeof(*led));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime.current_735_ma = NOCTI_V3_LED_CURRENT_735_MA;
    s_runtime.current_850_ma = NOCTI_V3_LED_CURRENT_850_MA;
    led->board = board;
    led->timebase = timebase;
    led->health = health;
    led->runtime = &s_runtime;
    led->state = NOCTI_V3_LED_RESET_HELD_SAFE;
    s_runtime.public_led = led;
    s_runtime.board = board;
    s_runtime.timebase = timebase;
    s_runtime.health = health;
    s_runtime.state = NOCTI_V3_LED_RESET_HELD_SAFE;
    s_runtime.i2c_callback_core = -1;
    build_persistent_payloads(&s_runtime);
    s_runtime.init_done = xSemaphoreCreateBinaryStatic(
        &s_runtime.init_done_storage);
    s_runtime.recovery_sem = xSemaphoreCreateBinaryStatic(
        &s_runtime.recovery_sem_storage);
    s_runtime.request_queue = xQueueCreateStatic(
        NOCTI_V3_LED_REQUEST_QUEUE_CAPACITY,
        sizeof(nocti_v3_led_request_t), s_runtime.request_queue_buffer,
        &s_runtime.request_queue_storage);
    if (s_runtime.init_done == NULL || s_runtime.recovery_sem == NULL ||
        s_runtime.request_queue == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(led_control_task, "nocti_v3_led",
                                NOCTI_V3_STACK_LED_CONTROL_BYTES, &s_runtime,
                                NOCTI_V3_PRIO_LED_CONTROL, &s_runtime.led_task,
                                NOCTI_V3_CORE_REALTIME) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(recovery_task, "nocti_v3_led_recover",
                                NOCTI_V3_STACK_LED_RECOVERY_BYTES, &s_runtime,
                                NOCTI_V3_PRIO_LED_RECOVERY,
                                &s_runtime.recovery_task,
                                NOCTI_V3_CORE_REALTIME) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_runtime.init_done,
                       pdMS_TO_TICKS(NOCTI_V3_ADS_INIT_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return s_runtime.init_result;
}

esp_err_t nocti_v3_led_safe_init(nocti_v3_led_t *led,
                                 nocti_v3_board_t *board)
{
    if (led == NULL || board == NULL) return ESP_ERR_INVALID_ARG;
    memset(led, 0, sizeof(*led));
    led->board = board;
    const esp_err_t err = nocti_v3_board_assert_pca_reset(board);
    led->state = err == ESP_OK ? NOCTI_V3_LED_RESET_HELD_SAFE :
                                NOCTI_V3_LED_FAULT;
    led->safe_off_count = err == ESP_OK ? 1u : 0u;
    led->last_error = err;
    return err;
}

esp_err_t nocti_v3_led_bind_completion_consumer(nocti_v3_led_t *led,
                                                TaskHandle_t task)
{
    if (led == NULL || led->runtime == NULL || task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ((led_runtime_t *)led->runtime)->completion_consumer = task;
    return ESP_OK;
}

esp_err_t nocti_v3_led_submit(nocti_v3_led_t *led,
                              const nocti_v3_led_request_t *request)
{
    if (led == NULL || led->runtime == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    led_runtime_t *runtime = (led_runtime_t *)led->runtime;
    if (!runtime->runtime_ready) return ESP_ERR_INVALID_STATE;
    if (request->type == NOCTI_V3_LED_COMMAND_SLOT_ON &&
        request->slot_index >= NOCTI_V3_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    if (xQueueSend(runtime->request_queue, request, 0u) != pdTRUE) {
        (void)__atomic_fetch_add(&runtime->i2c_queue_full, 1u,
                                 __ATOMIC_RELAXED);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool nocti_v3_led_take_completion(nocti_v3_led_t *led,
                                  nocti_v3_led_completion_t *out_completion)
{
    if (led == NULL || led->runtime == NULL || out_completion == NULL) {
        return false;
    }
    led_runtime_t *runtime = (led_runtime_t *)led->runtime;
    const uint32_t tail = __atomic_load_n(&runtime->completion_tail,
                                          __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->completion_head,
                                          __ATOMIC_ACQUIRE);
    if (tail == head) return false;
    *out_completion = runtime->completion_ring[tail & COMPLETION_MASK];
    __atomic_store_n(&runtime->completion_tail, tail + 1u,
                     __ATOMIC_RELEASE);
    return true;
}

esp_err_t nocti_v3_led_request_recovery(nocti_v3_led_t *led,
                                        esp_err_t reason)
{
    if (led == NULL || led->runtime == NULL) return ESP_ERR_INVALID_STATE;
    led_runtime_t *runtime = (led_runtime_t *)led->runtime;
    runtime->last_error = reason;
    if (__atomic_exchange_n(&runtime->recovery_pending, true,
                            __ATOMIC_ACQ_REL)) return ESP_OK;
    (void)__atomic_fetch_add(&runtime->led_errors, 1u, __ATOMIC_RELAXED);
    const uint32_t consecutive = __atomic_add_fetch(
        &runtime->consecutive_errors, 1u, __ATOMIC_ACQ_REL);
    (void)nocti_v3_board_assert_pca_reset(runtime->board);
    (void)__atomic_fetch_add(&runtime->safe_off_count, 1u, __ATOMIC_RELAXED);
    if (consecutive >= NOCTI_V3_LED_CONSECUTIVE_ERROR_THRESHOLD) {
        runtime->runtime_ready = false;
        runtime->state = NOCTI_V3_LED_FAULT;
        runtime->public_led->state = runtime->state;
        __atomic_store_n(&runtime->recovery_pending, false, __ATOMIC_RELEASE);
        return ESP_FAIL;
    }
    (void)xSemaphoreGive(runtime->recovery_sem);
    return ESP_OK;
}

esp_err_t nocti_v3_led_force_safe_off(nocti_v3_led_t *led)
{
    if (led == NULL || led->board == NULL) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = nocti_v3_board_assert_pca_reset(led->board);
    led->last_error = err;
    if (err == ESP_OK) {
        led->state = NOCTI_V3_LED_RESET_HELD_SAFE;
        led->safe_off_count++;
        if (led->runtime != NULL) {
            led_runtime_t *runtime = (led_runtime_t *)led->runtime;
            runtime->runtime_ready = false;
            runtime->state = NOCTI_V3_LED_RESET_HELD_SAFE;
            (void)__atomic_fetch_add(&runtime->safe_off_count, 1u,
                                     __ATOMIC_RELAXED);
        }
    } else {
        led->state = NOCTI_V3_LED_FAULT;
    }
    return err;
}

esp_err_t nocti_v3_led_wait_idle(nocti_v3_led_t *led, uint32_t timeout_ms)
{
    if (led == NULL || led->runtime == NULL) return ESP_ERR_INVALID_STATE;
    led_runtime_t *runtime = (led_runtime_t *)led->runtime;
    uint64_t start = 0u, now = 0u;
    (void)nocti_v3_timebase_now_us(runtime->timebase, &start);
    do {
        if (!__atomic_load_n(&runtime->transaction_pending,
                             __ATOMIC_ACQUIRE)) return ESP_OK;
        taskYIELD();
        (void)nocti_v3_timebase_now_us(runtime->timebase, &now);
    } while (now - start < (uint64_t)timeout_ms * 1000u);
    return ESP_ERR_TIMEOUT;
}

bool nocti_v3_led_is_fail_safe(const nocti_v3_led_t *led)
{ return led != NULL && led->board != NULL &&
         nocti_v3_board_led_fail_safe(led->board); }
bool nocti_v3_led_runtime_ready(const nocti_v3_led_t *led)
{ return led != NULL && led->runtime != NULL &&
         __atomic_load_n(&((led_runtime_t *)led->runtime)->runtime_ready,
                         __ATOMIC_ACQUIRE); }
uint8_t nocti_v3_led_slot_to_pca(uint8_t slot_index)
{ return slot_index < NOCTI_V3_SLOT_COUNT ? k_slot_to_pca[slot_index] : 0xFFu; }

void nocti_v3_led_health_snapshot(nocti_v3_led_t *led,
                                  nocti_v3_led_health_snapshot_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (led == NULL || led->runtime == NULL) return;
    led_runtime_t *r = (led_runtime_t *)led->runtime;
    out->led_errors = __atomic_load_n(&r->led_errors, __ATOMIC_ACQUIRE);
    out->i2c_nack = __atomic_load_n(&r->i2c_nack, __ATOMIC_ACQUIRE);
    out->i2c_timeout = __atomic_load_n(&r->i2c_timeout, __ATOMIC_ACQUIRE);
    out->i2c_queue_full = __atomic_load_n(&r->i2c_queue_full, __ATOMIC_ACQUIRE);
    out->i2c_completion_ring_overflow = __atomic_load_n(&r->completion_overflow, __ATOMIC_ACQUIRE);
    out->stale_i2c_completion = __atomic_load_n(&r->stale_completion, __ATOMIC_ACQUIRE);
    out->recovery_count = __atomic_load_n(&r->recovery_count, __ATOMIC_ACQUIRE);
    out->hardware_reset_count = __atomic_load_n(&r->hardware_reset_count, __ATOMIC_ACQUIRE);
    out->led_commit_latency_max_us = __atomic_load_n(&r->commit_latency_max_us, __ATOMIC_ACQUIRE);
    out->consecutive_errors = __atomic_load_n(&r->consecutive_errors, __ATOMIC_ACQUIRE);
    out->safe_off_count = __atomic_load_n(&r->safe_off_count, __ATOMIC_ACQUIRE);
    out->current_735_ma = r->current_735_ma;
    out->current_850_ma = r->current_850_ma;
    out->led_task_stack_watermark_bytes = __atomic_load_n(&r->led_stack_watermark, __ATOMIC_ACQUIRE);
    out->recovery_task_stack_watermark_bytes = __atomic_load_n(&r->recovery_stack_watermark, __ATOMIC_ACQUIRE);
    out->led_task_core = r->led_task_core;
    out->recovery_task_core = r->recovery_task_core;
    out->i2c_init_core = r->i2c_init_core;
    out->i2c_callback_core = __atomic_load_n(&r->i2c_callback_core, __ATOMIC_ACQUIRE);
    out->initialized = r->initialized;
    out->runtime_ready = r->runtime_ready;
    out->transaction_pending = __atomic_load_n(&r->transaction_pending, __ATOMIC_ACQUIRE);
    out->recovery_pending = __atomic_load_n(&r->recovery_pending, __ATOMIC_ACQUIRE);
    out->state = r->state;
    out->last_error = r->last_error;
}

esp_err_t nocti_v3_led_reset_counters(nocti_v3_led_t *led)
{
    if (led == NULL || led->runtime == NULL) return ESP_ERR_INVALID_STATE;
    led_runtime_t *r = (led_runtime_t *)led->runtime;
    if (r->transaction_pending || r->recovery_pending) return ESP_ERR_INVALID_STATE;
    __atomic_store_n(&r->led_errors, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->i2c_nack, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->i2c_timeout, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->i2c_queue_full, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->completion_overflow, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->stale_completion, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->recovery_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->commit_latency_max_us, 0u, __ATOMIC_RELEASE);
    return ESP_OK;
}

esp_err_t nocti_v3_led_set_current_profile(nocti_v3_led_t *led,
                                            uint16_t current_735_ma,
                                            uint16_t current_850_ma)
{
    if (led == NULL || led->runtime == NULL) return ESP_ERR_INVALID_STATE;
    if (current_735_ma > NOCTI_V3_LED_CURRENT_MAX_MA ||
        current_850_ma > NOCTI_V3_LED_CURRENT_MAX_MA) return ESP_ERR_INVALID_ARG;
    led_runtime_t *r = (led_runtime_t *)led->runtime;
    if (r->transaction_pending || r->recovery_pending || !r->runtime_ready)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = enter_sync_configuration(r);
    if (err != ESP_OK) return err;
    r->current_735_ma = current_735_ma;
    r->current_850_ma = current_850_ma;
    build_persistent_payloads(r);
    err = reset_and_program_sync(r);
    if (err == ESP_OK) err = enter_async_runtime(r);
    if (err != ESP_OK) {
        (void)nocti_v3_board_assert_pca_reset(r->board);
        r->runtime_ready = false;
        r->state = NOCTI_V3_LED_FAULT;
        r->last_error = err;
        led->state = r->state;
        return err;
    }
    ESP_LOGI(TAG, "current profile updated 735=%u mA 850=%u mA",
             (unsigned)current_735_ma, (unsigned)current_850_ma);
    return ESP_OK;
}

void nocti_v3_led_get_current_profile(nocti_v3_led_t *led,
                                      uint16_t *current_735_ma,
                                      uint16_t *current_850_ma)
{
    if (led == NULL || led->runtime == NULL) return;
    led_runtime_t *r = (led_runtime_t *)led->runtime;
    if (current_735_ma != NULL) *current_735_ma = r->current_735_ma;
    if (current_850_ma != NULL) *current_850_ma = r->current_850_ma;
}

const char *nocti_v3_led_state_name(nocti_v3_led_state_t state)
{
    switch (state) {
        case NOCTI_V3_LED_UNINITIALIZED: return "UNINITIALIZED";
        case NOCTI_V3_LED_RESET_HELD_SAFE: return "RESET_HELD_SAFE";
        case NOCTI_V3_LED_CONFIGURED_ALL_OFF: return "CONFIGURED_ALL_OFF";
        case NOCTI_V3_LED_RUNTIME_READY: return "RUNTIME_READY";
        case NOCTI_V3_LED_RECOVERING: return "RECOVERING";
        case NOCTI_V3_LED_FAULT: return "FAULT";
        default: return "UNKNOWN";
    }
}
