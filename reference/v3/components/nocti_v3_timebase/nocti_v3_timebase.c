#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_timebase.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/portmacro.h"

static const char *TAG = "nocti_v3_timebase";

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer,
                               const gptimer_alarm_event_data_t *event_data,
                               void *user_context)
{
    (void)timer;
    nocti_v3_timebase_t *timebase = (nocti_v3_timebase_t *)user_context;
    BaseType_t high_priority_woken = pdFALSE;

    timebase->callback_core = xPortGetCoreID();
    if (__atomic_load_n(&timebase->deadline_event_ready, __ATOMIC_ACQUIRE)) {
        (void)__atomic_fetch_add(&timebase->deadline_event_overflow, 1u, __ATOMIC_RELAXED);
    }
    timebase->deadline_event.alarm_timestamp_us = event_data->alarm_value;
    timebase->deadline_event.callback_timestamp_us = event_data->count_value;
    timebase->deadline_event.event_id = timebase->deadline_event_id;
    timebase->deadline_event.generation = timebase->deadline_generation;
    __atomic_store_n(&timebase->deadline_event_ready, true, __ATOMIC_RELEASE);
    __atomic_store_n(&timebase->deadline_armed, false, __ATOMIC_RELEASE);

    TaskHandle_t consumer = timebase->deadline_consumer;
    if (consumer != NULL) {
        (void)xTaskNotifyFromISR(consumer,
                                NOCTI_V3_TIMEBASE_NOTIFY_DEADLINE,
                                eSetBits,
                                &high_priority_woken);
    }
    return high_priority_woken == pdTRUE;
}

esp_err_t nocti_v3_timebase_init(nocti_v3_timebase_t *timebase)
{
    if (timebase == NULL) return ESP_ERR_INVALID_ARG;
    memset(timebase, 0, sizeof(*timebase));
    timebase->callback_core = -1;
    timebase->init_core = xPortGetCoreID();

    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_XTAL,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000u,
        .intr_priority = 0,
    };

    esp_err_t err = gptimer_new_timer(&config, &timebase->timer);
    if (err != ESP_OK) return err;

    const gptimer_event_callbacks_t callbacks = {
        .on_alarm = on_alarm,
    };
    err = gptimer_register_event_callbacks(timebase->timer, &callbacks, timebase);
    if (err != ESP_OK) {
        (void)gptimer_del_timer(timebase->timer);
        timebase->timer = NULL;
        return err;
    }

    err = gptimer_enable(timebase->timer);
    if (err != ESP_OK) {
        (void)gptimer_del_timer(timebase->timer);
        timebase->timer = NULL;
        return err;
    }
    err = gptimer_start(timebase->timer);
    if (err != ESP_OK) {
        (void)gptimer_disable(timebase->timer);
        (void)gptimer_del_timer(timebase->timer);
        timebase->timer = NULL;
        return err;
    }

    timebase->resolution_hz = 1000000u;
    timebase->initialized = true;
    timebase->running = true;
    ESP_LOGI(TAG,
             "monotonic GPTimer ready resolution=1 MHz init_core=%d absolute_deadline=disarmed",
             timebase->init_core);
    return ESP_OK;
}

esp_err_t nocti_v3_timebase_now_us(const nocti_v3_timebase_t *timebase,
                                   uint64_t *out_timestamp_us)
{
    if (timebase == NULL || out_timestamp_us == NULL) return ESP_ERR_INVALID_ARG;
    if (!timebase->initialized || !timebase->running || timebase->timer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return gptimer_get_raw_count(timebase->timer, out_timestamp_us);
}

esp_err_t IRAM_ATTR nocti_v3_timebase_now_us_isr(
    const nocti_v3_timebase_t *timebase,
    uint64_t *out_timestamp_us)
{
    if (timebase == NULL || out_timestamp_us == NULL || timebase->timer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return gptimer_get_raw_count(timebase->timer, out_timestamp_us);
}

bool nocti_v3_timebase_ready(const nocti_v3_timebase_t *timebase)
{
    return timebase != NULL && timebase->initialized && timebase->running &&
           timebase->timer != NULL && timebase->resolution_hz == 1000000u;
}

esp_err_t nocti_v3_timebase_bind_deadline_consumer(nocti_v3_timebase_t *timebase,
                                                    TaskHandle_t task)
{
    if (!nocti_v3_timebase_ready(timebase)) return ESP_ERR_INVALID_STATE;
    if (__atomic_load_n(&timebase->deadline_armed, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    timebase->deadline_consumer = task;
    return ESP_OK;
}

esp_err_t nocti_v3_timebase_arm_absolute(nocti_v3_timebase_t *timebase,
                                         uint64_t absolute_timestamp_us,
                                         uint32_t event_id,
                                         uint32_t generation)
{
    if (!nocti_v3_timebase_ready(timebase)) return ESP_ERR_INVALID_STATE;

    uint64_t now_us = 0u;
    esp_err_t err = nocti_v3_timebase_now_us(timebase, &now_us);
    if (err != ESP_OK) return err;
    if (absolute_timestamp_us <= now_us) return ESP_ERR_INVALID_ARG;

    timebase->deadline_event_id = event_id;
    timebase->deadline_generation = generation;
    __atomic_store_n(&timebase->deadline_event_ready, false, __ATOMIC_RELEASE);

    const gptimer_alarm_config_t alarm = {
        .alarm_count = absolute_timestamp_us,
        .reload_count = 0u,
        .flags = {
            .auto_reload_on_alarm = false,
        },
    };
    err = gptimer_set_alarm_action(timebase->timer, &alarm);
    if (err == ESP_OK) {
        __atomic_store_n(&timebase->deadline_armed, true, __ATOMIC_RELEASE);
    }
    return err;
}

esp_err_t nocti_v3_timebase_disarm_deadline(nocti_v3_timebase_t *timebase)
{
    if (!nocti_v3_timebase_ready(timebase)) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = gptimer_set_alarm_action(timebase->timer, NULL);
    if (err == ESP_OK) {
        __atomic_store_n(&timebase->deadline_armed, false, __ATOMIC_RELEASE);
        __atomic_store_n(&timebase->deadline_event_ready, false, __ATOMIC_RELEASE);
    }
    return err;
}

uint32_t nocti_v3_timebase_deadline_overflow(const nocti_v3_timebase_t *timebase)
{
    return timebase != NULL ? __atomic_load_n(&timebase->deadline_event_overflow, __ATOMIC_ACQUIRE) : 0u;
}

bool nocti_v3_timebase_take_deadline_event(nocti_v3_timebase_t *timebase,
                                           nocti_v3_deadline_event_t *out_event)
{
    if (timebase == NULL || out_event == NULL) return false;
    if (!__atomic_exchange_n(&timebase->deadline_event_ready,
                             false,
                             __ATOMIC_ACQ_REL)) {
        return false;
    }
    *out_event = timebase->deadline_event;
    return true;
}

esp_err_t nocti_v3_timebase_deinit(nocti_v3_timebase_t *timebase)
{
    if (timebase == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t first_error = ESP_OK;
    if (timebase->timer != NULL) {
        (void)gptimer_set_alarm_action(timebase->timer, NULL);
    }
    if (timebase->running && timebase->timer != NULL) {
        const esp_err_t err = gptimer_stop(timebase->timer);
        if (err != ESP_OK) first_error = err;
        timebase->running = false;
    }
    if (timebase->initialized && timebase->timer != NULL) {
        const esp_err_t disable_err = gptimer_disable(timebase->timer);
        if (disable_err != ESP_OK && first_error == ESP_OK) first_error = disable_err;
        const esp_err_t delete_err = gptimer_del_timer(timebase->timer);
        if (delete_err != ESP_OK && first_error == ESP_OK) first_error = delete_err;
    }
    memset(timebase, 0, sizeof(*timebase));
    return first_error;
}
