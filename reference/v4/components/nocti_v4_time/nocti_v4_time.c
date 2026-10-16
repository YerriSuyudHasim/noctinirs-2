#include "nocti_v4_time.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bool IRAM_ATTR time_alarm_trampoline(gptimer_handle_t timer,
                                             const gptimer_alarm_event_data_t *event,
                                             void *user_data)
{
    nocti_v4_time_t *timebase = (nocti_v4_time_t *)user_data;
    if (timebase == NULL) {
        return false;
    }
    nocti_v4_time_alarm_handler_t handler = __atomic_load_n(&timebase->alarm_handler,
                                                              __ATOMIC_ACQUIRE);
    void *handler_data = __atomic_load_n(&timebase->alarm_user_data,
                                         __ATOMIC_ACQUIRE);
    return handler != NULL ? handler(timer, event, handler_data) : false;
}

esp_err_t nocti_v4_time_init(nocti_v4_time_t *timebase)
{
    if (timebase == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(timebase, 0, sizeof(*timebase));
    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_XTAL,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000u,
        .intr_priority = 0,
    };
    esp_err_t err = gptimer_new_timer(&config, &timebase->timer);
    if (err != ESP_OK) {
        return err;
    }
    const gptimer_event_callbacks_t callbacks = {
        .on_alarm = time_alarm_trampoline,
    };
    /* ESP-IDF 5.5.1 requires this first registration before enable. The
     * scheduler later publishes its ISR handler through the trampoline. */
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
    timebase->init_core = xPortGetCoreID();
    timebase->initialized = true;
    return ESP_OK;
}

bool nocti_v4_time_ready(const nocti_v4_time_t *timebase)
{
    return timebase != NULL && timebase->initialized && timebase->timer != NULL &&
           timebase->resolution_hz == 1000000u;
}

esp_err_t nocti_v4_time_now_us(const nocti_v4_time_t *timebase, uint64_t *out_us)
{
    if (!nocti_v4_time_ready(timebase) || out_us == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return gptimer_get_raw_count(timebase->timer, out_us);
}

esp_err_t IRAM_ATTR nocti_v4_time_now_us_isr(const nocti_v4_time_t *timebase,
                                              uint64_t *out_us)
{
    if (timebase == NULL || timebase->timer == NULL || out_us == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return gptimer_get_raw_count(timebase->timer, out_us);
}

esp_err_t nocti_v4_time_set_alarm_handler(nocti_v4_time_t *timebase,
                                           nocti_v4_time_alarm_handler_t handler,
                                           void *user_data)
{
    if (!nocti_v4_time_ready(timebase)) {
        return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&timebase->alarm_user_data, user_data, __ATOMIC_RELEASE);
    __atomic_store_n(&timebase->alarm_handler, handler, __ATOMIC_RELEASE);
    return ESP_OK;
}
