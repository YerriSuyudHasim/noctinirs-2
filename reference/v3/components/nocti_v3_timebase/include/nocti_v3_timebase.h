#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gptimer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOCTI_V3_TIMEBASE_NOTIFY_DEADLINE (1u << 0)

typedef struct {
    uint64_t alarm_timestamp_us;
    uint64_t callback_timestamp_us;
    uint32_t event_id;
    uint32_t generation;
} nocti_v3_deadline_event_t;

typedef struct {
    bool initialized;
    bool running;
    volatile bool deadline_armed;
    volatile bool deadline_event_ready;
    volatile uint32_t deadline_event_overflow;
    uint32_t resolution_hz;
    int init_core;
    volatile int callback_core;
    volatile uint32_t deadline_generation;
    volatile uint32_t deadline_event_id;
    TaskHandle_t deadline_consumer;
    nocti_v3_deadline_event_t deadline_event;
    gptimer_handle_t timer;
} nocti_v3_timebase_t;

/* Must be called on the chosen real-time core before any ISR uses the timer. */
esp_err_t nocti_v3_timebase_init(nocti_v3_timebase_t *timebase);
esp_err_t nocti_v3_timebase_deinit(nocti_v3_timebase_t *timebase);
esp_err_t nocti_v3_timebase_now_us(const nocti_v3_timebase_t *timebase,
                                   uint64_t *out_timestamp_us);
esp_err_t nocti_v3_timebase_now_us_isr(const nocti_v3_timebase_t *timebase,
                                       uint64_t *out_timestamp_us);
bool nocti_v3_timebase_ready(const nocti_v3_timebase_t *timebase);

/* Absolute one-shot deadline infrastructure. Prompt 2 leaves it disarmed. */
esp_err_t nocti_v3_timebase_bind_deadline_consumer(nocti_v3_timebase_t *timebase,
                                                    TaskHandle_t task);
esp_err_t nocti_v3_timebase_arm_absolute(nocti_v3_timebase_t *timebase,
                                         uint64_t absolute_timestamp_us,
                                         uint32_t event_id,
                                         uint32_t generation);
esp_err_t nocti_v3_timebase_disarm_deadline(nocti_v3_timebase_t *timebase);
bool nocti_v3_timebase_take_deadline_event(nocti_v3_timebase_t *timebase,
                                           nocti_v3_deadline_event_t *out_event);
uint32_t nocti_v3_timebase_deadline_overflow(const nocti_v3_timebase_t *timebase);

#ifdef __cplusplus
}
#endif
