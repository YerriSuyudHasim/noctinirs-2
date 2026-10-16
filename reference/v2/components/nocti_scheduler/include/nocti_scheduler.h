// ============================================================
// File: components/nocti_scheduler/include/nocti_scheduler.h
// ============================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// NoctiNIRS optical scheduler
//
// Each hardware tick owns exactly one optical slot (or one dark tick).
// SLOT_BEGIN is dispatched at the absolute tick edge and SLOT_END at the
// absolute tick + on_time_us deadline. Callback duration therefore cannot
// silently move the LED-OFF edge toward the next slot. The application records
// the actual I2C STOP timestamps and accepts only ADC samples that are fully
// settled inside the resulting physical LED-ON and LED-OFF intervals.
// ============================================================

typedef enum {
    NOCTI_DARK_BLOCK = 0,
    NOCTI_DARK_INTERLEAVED = 1,
} nocti_dark_mode_t;

typedef enum {
    NOCTI_EV_DARK_TICK = 0,
    NOCTI_EV_SLOT_BEGIN = 1,
    NOCTI_EV_SLOT_SAMPLE = 2,
    NOCTI_EV_SLOT_END = 3,
    NOCTI_EV_SLOT_ABORT = 4,
} nocti_sched_phase_t;

typedef enum {
    NOCTI_ABORT_NONE = 0,
    // Scheduler task started too late to guarantee the configured minimum
    // settled window. SLOT_BEGIN was never dispatched, so the LED state was not
    // changed by the rejected slot.
    NOCTI_ABORT_LATE_DISPATCH = 1,
    // SLOT_BEGIN callback returned an error; the LED state may be unknown.
    NOCTI_ABORT_BEGIN_ERROR = 2,
    // SLOT_BEGIN returned successfully, but the physical LED-ON transaction
    // completed too late to preserve the minimum settled window.
    NOCTI_ABORT_LATE_BEGIN_COMPLETION = 3,
} nocti_sched_abort_reason_t;

typedef struct {
    uint8_t led_id;
    nocti_wavelength_t wavelength;

    // Absolute timing inside one tick. SLOT_END is dispatched at
    // tick_start + on_time_us; off_gap_us is the remaining tick duration.
    // settle_us and sample_window_us define the minimum acceptable settled
    // interval on both sides of the transition and are used for admission
    // checks. The application still validates the actual ADC timestamps.
    uint16_t on_time_us;
    uint16_t settle_us;
    uint16_t sample_window_us;
    uint16_t off_gap_us;

    // Optional per-slot LED current metadata.
    uint16_t current_mA;
} nocti_slot_t;

typedef struct {
    const nocti_slot_t *slots;
    uint16_t slot_count;

    uint32_t tick_period_us;
    uint32_t frame_period_us;

    uint32_t dark_us_total;
    nocti_dark_mode_t dark_mode;

    uint8_t priority;
    // ESP-IDF task creation APIs interpret stack depth in bytes.
    uint16_t stack_bytes;

    bool pin_to_core;
    int8_t core_id;

    // Reserve time after the absolute LED-OFF deadline for the LED-OFF I2C
    // transaction and callback bookkeeping.
    uint16_t slot_end_guard_us;

    // A processed tick later than this threshold contributes to late_tick_count.
    uint16_t late_warn_us;
} nocti_scheduler_cfg_t;

typedef struct {
    // Ideal event time in the common NoctiNIRS timebase. BEGIN is the tick
    // edge and END is tick edge + on_time_us.
    nocti_time_us_t t_event_us;

    // Actual time immediately before callback dispatch.
    nocti_time_us_t t_dispatch_us;

    uint32_t frame_index;
    uint32_t slot_index;
    nocti_sched_phase_t phase;
    nocti_sched_abort_reason_t abort_reason;

    // Number of timer ticks that had to be skipped immediately before this tick.
    // Non-zero means at least one optical slot was not executed.
    uint32_t missed_ticks_before;

    // Delay from ideal tick edge to scheduler processing for this tick.
    uint32_t tick_lateness_us;

    nocti_slot_t slot;
} nocti_sched_event_t;

typedef struct {
    int8_t task_core_id;

    uint32_t tick_isr_count;
    uint32_t processed_tick_count;
    uint32_t missed_tick_count;

    uint32_t late_tick_count;
    uint32_t max_tick_lateness_us;

    uint32_t aborted_slot_count;
    uint32_t abort_late_dispatch_count;
    uint32_t abort_begin_error_count;
    uint32_t abort_late_begin_completion_count;
    uint32_t slot_overrun_count;
    uint32_t max_slot_overrun_us;

    uint32_t callback_error_count;
    uint32_t callback_overrun_count;
    uint32_t max_callback_us;
} nocti_scheduler_stats_t;

// Return ESP_OK when the event was handled successfully. In particular, a
// SLOT_BEGIN error causes the scheduler to abort that slot instead of sampling
// with an LED state that is unknown or invalid.
typedef esp_err_t (*nocti_sched_cb_t)(const nocti_sched_event_t *ev, void *user);

bool nocti_scheduler_init(const nocti_scheduler_cfg_t *cfg,
                          nocti_sched_cb_t cb,
                          void *user);
bool nocti_scheduler_start(void);
void nocti_scheduler_stop(void);
bool nocti_scheduler_is_running(void);
void nocti_scheduler_get_stats(nocti_scheduler_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
