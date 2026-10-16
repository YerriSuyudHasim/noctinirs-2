#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gptimer.h"
#include "esp_err.h"

typedef bool (*nocti_v4_time_alarm_handler_t)(
    gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *user_data);

typedef struct {
    gptimer_handle_t timer;
    bool initialized;
    int32_t init_core;
    uint32_t resolution_hz;
    volatile nocti_v4_time_alarm_handler_t alarm_handler;
    void * volatile alarm_user_data;
} nocti_v4_time_t;

/* The same 1 MHz, count-up source is used in task and GPIO-ISR contexts. */
esp_err_t nocti_v4_time_init(nocti_v4_time_t *timebase);
esp_err_t nocti_v4_time_now_us(const nocti_v4_time_t *timebase, uint64_t *out_us);
esp_err_t nocti_v4_time_now_us_isr(const nocti_v4_time_t *timebase, uint64_t *out_us);
/* The GPTimer ISR trampoline is registered before the timebase is enabled.
 * This only publishes the later owner; it does not re-register an enabled timer. */
esp_err_t nocti_v4_time_set_alarm_handler(nocti_v4_time_t *timebase,
                                           nocti_v4_time_alarm_handler_t handler,
                                           void *user_data);
bool nocti_v4_time_ready(const nocti_v4_time_t *timebase);
