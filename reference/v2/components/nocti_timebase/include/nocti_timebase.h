// ============================================================
// File: components/nocti_timebase/include/nocti_timebase.h
// NoctiNIRS master monotonic clock based on one ESP32-S3 GPTimer
// ============================================================

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The callback receives the newest ideal tick deadline represented by this
// interrupt plus the number of ideal periods that elapsed since the previous
// callback. A delayed interrupt is therefore reported once, without replaying
// a long callback loop inside the ISR.
typedef bool (*nocti_timebase_isr_cb_t)(nocti_time_us_t scheduled_us,
                                        uint32_t elapsed_ticks,
                                        void *user);

typedef struct {
    uint32_t tick_period_us;
    nocti_timebase_isr_cb_t isr_cb;  // May be NULL for an alarm-only heartbeat.
    void *user;
} nocti_timebase_tick_cfg_t;

typedef struct {
    bool initialized;
    bool tick_running;
    int8_t init_core_id;       // core that allocated/enabled the GPTimer
    int8_t alarm_isr_core_id;  // core observed by the first alarm callback
    uint32_t resolution_hz;
    uint32_t tick_period_us;

    uint64_t next_alarm_us;
    uint64_t alarm_irq_count;
    uint64_t represented_tick_count;
    uint64_t catchup_tick_count;
    uint32_t max_isr_lateness_us;
    uint32_t read_error_count;
    uint32_t alarm_rearm_error_count;
    uint32_t excessive_catchup_count;
} nocti_timebase_status_t;

// Initialize one free-running 1 MHz GPTimer. The callback is registered before
// the timer is enabled, allowing the same hardware counter to serve both as the
// monotonic timestamp source and as the scheduler alarm source.
bool nocti_timebase_init(void);

bool nocti_timebase_is_ready(void);

// Safe task-context timestamp read. Epoch is the successful timebase init.
nocti_time_us_t nocti_timebase_now_us(void);

// ISR-safe timestamp read. For cache-disabled operation, enable both
// CONFIG_GPTIMER_ISR_CACHE_SAFE and CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM.
nocti_time_us_t nocti_timebase_now_us_isr(void);

// Error-reporting timestamp read for diagnostics and strict call sites.
bool nocti_timebase_try_now_us(nocti_time_us_t *out_us);

// Start an absolute periodic alarm on the same free-running GPTimer. The first
// tick occurs one full period after this call arms the alarm.
bool nocti_timebase_start_tick(const nocti_timebase_tick_cfg_t *cfg);

// Disable the alarm while leaving the monotonic counter running.
void nocti_timebase_stop_tick(void);

void nocti_timebase_get_status(nocti_timebase_status_t *out_status);

// Precise short delay. A ROM delay handles the coarse part, followed by an
// absolute GPTimer deadline check so preemption cannot cause an undershoot.
void nocti_timebase_busywait_us(uint32_t us);

// Hybrid RTOS sleep followed by a short absolute-deadline precision tail.
void nocti_timebase_sleep_until_us(nocti_time_us_t deadline_us);

#ifdef __cplusplus
}
#endif
