// ============================================================
// File: components/nocti_scheduler/nocti_scheduler.c
// ============================================================

#include "nocti_scheduler.h"

#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"

#include "nocti_log.h"
#include "nocti_timebase.h"

static const char *TAG = "nocti_sched";

// Coarse sleep followed by a short spin gives good timing while avoiding a
// full-tick busy wait.
#define NOCTI_SCHED_SPIN_TAIL_US       (150u)
#define NOCTI_SCHED_DEFAULT_END_GUARD  (400u)
#define NOCTI_SCHED_DEFAULT_LATE_WARN  (200u)
#define NOCTI_SCHED_CB_WARN_US         (1200u)
#define NOCTI_SCHED_COARSE_GUARD_US     (250u)

typedef struct {
    nocti_scheduler_cfg_t cfg;
    nocti_sched_cb_t cb;
    void *user;

    TaskHandle_t task;
    volatile bool running;
    volatile int8_t task_core_id;

    uint32_t frame_index;
    uint32_t slot_index;
    uint32_t frame_elapsed_us;
    uint32_t dark_used_us;

    // ISR -> task timestamp handoff protected by a sequence lock.
    volatile uint32_t tick_seq;
    volatile uint32_t tick_lo;
    volatile uint32_t tick_hi;

    // Total number of ideal GPTimer periods represented by the latest timestamp.
    // It is written by one ISR and read together with the timestamp under the
    // same sequence lock, preventing count/timestamp races across tick edges.
    volatile uint64_t represented_tick_total;
    uint64_t consumed_tick_total;

    nocti_scheduler_stats_t stats;
} sched_ctx_t;

static sched_ctx_t s;

static inline uint32_t saturating_u32_from_time(nocti_time_us_t value)
{
    if (value <= 0) return 0u;
    if (value > (nocti_time_us_t)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)value;
}

static inline void read_tick_snapshot_atomic(nocti_time_us_t *out_tick_us,
                                             uint64_t *out_tick_total)
{
    if (!out_tick_us || !out_tick_total) return;

    while (true) {
        const uint32_t seq1 = __atomic_load_n(
            &s.tick_seq, __ATOMIC_ACQUIRE);
        const uint32_t hi = s.tick_hi;
        const uint32_t lo = s.tick_lo;
        const uint64_t total = s.represented_tick_total;
        const uint32_t seq2 = __atomic_load_n(
            &s.tick_seq, __ATOMIC_ACQUIRE);

        if ((seq1 == seq2) && ((seq1 & 1u) == 0u)) {
            const uint64_t value = ((uint64_t)hi << 32u) | (uint64_t)lo;
            *out_tick_us = (nocti_time_us_t)value;
            *out_tick_total = total;
            return;
        }
    }
}

static esp_err_t emit_event(nocti_sched_phase_t phase,
                            const nocti_slot_t *slot_opt,
                            nocti_time_us_t t_event_us,
                            uint32_t missed_ticks_before,
                            uint32_t tick_lateness_us,
                            nocti_sched_abort_reason_t abort_reason)
{
    if (!s.cb) return ESP_OK;

    nocti_sched_event_t ev;
    memset(&ev, 0, sizeof(ev));

    ev.t_event_us = t_event_us;
    ev.t_dispatch_us = nocti_timebase_now_us();
    ev.frame_index = s.frame_index;
    ev.slot_index = s.slot_index;
    ev.phase = phase;
    ev.abort_reason = abort_reason;
    ev.missed_ticks_before = missed_ticks_before;
    ev.tick_lateness_us = tick_lateness_us;

    if (slot_opt) {
        ev.slot = *slot_opt;
    }

    const nocti_time_us_t cb_start_us = ev.t_dispatch_us;
    const esp_err_t err = s.cb(&ev, s.user);
    const nocti_time_us_t cb_end_us = nocti_timebase_now_us();
    const uint32_t cb_us = saturating_u32_from_time(cb_end_us - cb_start_us);

    if (cb_us > s.stats.max_callback_us) {
        s.stats.max_callback_us = cb_us;
    }
    if (cb_us > NOCTI_SCHED_CB_WARN_US) {
        s.stats.callback_overrun_count++;
    }
    if (err != ESP_OK) {
        s.stats.callback_error_count++;
    }

    return err;
}

static bool should_insert_dark_this_tick(void)
{
    if (s.cfg.dark_us_total == 0u) return false;
    if (s.dark_used_us >= s.cfg.dark_us_total) return false;

    const uint32_t remaining_dark = s.cfg.dark_us_total - s.dark_used_us;

    if (s.cfg.dark_mode == NOCTI_DARK_BLOCK) {
        const uint32_t remaining_in_frame =
            s.cfg.frame_period_us - s.frame_elapsed_us;
        return remaining_in_frame <= remaining_dark;
    }

    const uint64_t dark_target =
        ((uint64_t)s.cfg.dark_us_total * (uint64_t)s.frame_elapsed_us) /
        (uint64_t)s.cfg.frame_period_us;

    return (uint64_t)s.dark_used_us < dark_target;
}

static inline void wait_until_deadline_us(nocti_time_us_t deadline_us)
{
    // Do not call nocti_timebase_sleep_until_us() here: that generic helper uses
    // a 1.5-ms spin tail, which made the optical task occupy most of the
    // acquisition core. Sleep in whole RTOS ticks only while a guard remains,
    // then use a short 150-us precision tail. The higher-priority ADC task can
    // still preempt this scheduler throughout the wait.
    const uint32_t rtos_tick_us =
        (configTICK_RATE_HZ > 0) ?
        (1000000u / (uint32_t)configTICK_RATE_HZ) : 1000u;

    while (true) {
        const nocti_time_us_t now_us = nocti_timebase_now_us();
        const nocti_time_us_t remaining_us = deadline_us - now_us;

        if (remaining_us <= 0) return;

        if (remaining_us <= (nocti_time_us_t)NOCTI_SCHED_SPIN_TAIL_US) {
            nocti_timebase_busywait_us((uint32_t)remaining_us);
            return;
        }

        const uint32_t remaining32 = saturating_u32_from_time(remaining_us);
        const uint32_t reserved_us =
            NOCTI_SCHED_SPIN_TAIL_US + NOCTI_SCHED_COARSE_GUARD_US;

        if (rtos_tick_us > 0u && remaining32 > reserved_us + rtos_tick_us) {
            TickType_t sleep_ticks =
                (TickType_t)((remaining32 - reserved_us) / rtos_tick_us);
            if (sleep_ticks > 0u) {
                vTaskDelay(sleep_ticks);
                continue;
            }
        }

        // The remaining interval is shorter than a safely quantized RTOS sleep.
        // Yield so the ADC producer can drain DRDY frames; finish with the short
        // precision spin above.
        taskYIELD();
    }
}

static void advance_program_state(bool was_dark)
{
    if (was_dark) {
        s.dark_used_us += s.cfg.tick_period_us;
    } else {
        s.slot_index++;
        if (s.slot_index >= s.cfg.slot_count) {
            s.slot_index = 0u;
        }
    }

    s.frame_elapsed_us += s.cfg.tick_period_us;

    if (s.frame_elapsed_us >= s.cfg.frame_period_us) {
        s.frame_index++;
        s.frame_elapsed_us = 0u;
        s.dark_used_us = 0u;
        s.slot_index = 0u;
    }
}

static void skip_program_ticks(uint32_t count)
{
    for (uint32_t i = 0u; i < count; i++) {
        const bool was_dark = should_insert_dark_this_tick();
        advance_program_state(was_dark);
    }
}

static bool IRAM_ATTR timebase_tick_isr(nocti_time_us_t scheduled_us,
                                        uint32_t elapsed_ticks,
                                        void *user)
{
    const TaskHandle_t task = (TaskHandle_t)user;
    if (elapsed_ticks == 0u) elapsed_ticks = 1u;

    (void)__atomic_fetch_add(&s.tick_seq, 1u, __ATOMIC_RELAXED);
    const uint64_t value = (uint64_t)scheduled_us;
    s.tick_hi = (uint32_t)(value >> 32u);
    s.tick_lo = (uint32_t)(value & 0xFFFFFFFFu);
    s.represented_tick_total += (uint64_t)elapsed_ticks;
    (void)__atomic_fetch_add(&s.tick_seq, 1u, __ATOMIC_RELEASE);

    s.stats.tick_isr_count += elapsed_ticks;

    BaseType_t high_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &high_priority_woken);
    return high_priority_woken == pdTRUE;
}

static void emit_abort_and_wait(const nocti_slot_t *slot,
                                nocti_time_us_t t_tick_end,
                                uint32_t missed_ticks_before,
                                uint32_t tick_lateness_us,
                                nocti_sched_abort_reason_t abort_reason)
{
    s.stats.aborted_slot_count++;
    switch (abort_reason) {
        case NOCTI_ABORT_LATE_DISPATCH:
            s.stats.abort_late_dispatch_count++;
            break;
        case NOCTI_ABORT_BEGIN_ERROR:
            s.stats.abort_begin_error_count++;
            break;
        case NOCTI_ABORT_LATE_BEGIN_COMPLETION:
            s.stats.abort_late_begin_completion_count++;
            break;
        default:
            break;
    }

    (void)emit_event(NOCTI_EV_SLOT_ABORT,
                     slot,
                     nocti_timebase_now_us(),
                     missed_ticks_before,
                     tick_lateness_us,
                     abort_reason);
    wait_until_deadline_us(t_tick_end);
}

static void scheduler_task(void *arg)
{
    (void)arg;
    s.task_core_id = (int8_t)xPortGetCoreID();

    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        nocti_time_us_t t_tick = 0;
        uint64_t represented_total = 0u;
        read_tick_snapshot_atomic(&t_tick, &represented_total);

        const uint64_t pending64 = represented_total - s.consumed_tick_total;
        s.consumed_tick_total = represented_total;
        const uint32_t pending_ticks = pending64 > (uint64_t)UINT32_MAX
            ? UINT32_MAX
            : (uint32_t)pending64;

        if (!s.running || pending_ticks == 0u) {
            continue;
        }
        const nocti_time_us_t t_tick_end =
            t_tick + (nocti_time_us_t)s.cfg.tick_period_us;

        const uint32_t missed_ticks_before = pending_ticks - 1u;
        if (missed_ticks_before > 0u) {
            s.stats.missed_tick_count += missed_ticks_before;
            skip_program_ticks(missed_ticks_before);
        }

        const nocti_time_us_t dispatch_now_us = nocti_timebase_now_us();
        const uint32_t tick_lateness_us =
            saturating_u32_from_time(dispatch_now_us - t_tick);

        if (tick_lateness_us > s.stats.max_tick_lateness_us) {
            s.stats.max_tick_lateness_us = tick_lateness_us;
        }
        if (tick_lateness_us > s.cfg.late_warn_us) {
            s.stats.late_tick_count++;
        }
        s.stats.processed_tick_count++;

        const bool do_dark = should_insert_dark_this_tick();

        if (do_dark) {
            (void)emit_event(NOCTI_EV_DARK_TICK,
                             NULL,
                             t_tick,
                             missed_ticks_before,
                             tick_lateness_us,
                             NOCTI_ABORT_NONE);
            wait_until_deadline_us(t_tick_end);
            advance_program_state(true);
            continue;
        }

        const nocti_slot_t *slot = &s.cfg.slots[s.slot_index];
        const nocti_time_us_t t_led_off_deadline =
            t_tick + (nocti_time_us_t)slot->on_time_us;
        const nocti_time_us_t t_latest_begin =
            t_led_off_deadline -
            (nocti_time_us_t)slot->settle_us -
            (nocti_time_us_t)slot->sample_window_us;

        // Do not turn the LED on when the scheduler arrived so late that even
        // a zero-latency I2C transaction could not leave the configured minimum
        // settled ON window before the absolute LED-OFF deadline.
        if (dispatch_now_us > t_latest_begin) {
            emit_abort_and_wait(slot,
                                t_tick_end,
                                missed_ticks_before,
                                tick_lateness_us,
                                NOCTI_ABORT_LATE_DISPATCH);
            advance_program_state(false);
            continue;
        }

        const esp_err_t begin_err = emit_event(NOCTI_EV_SLOT_BEGIN,
                                               slot,
                                               t_tick,
                                               missed_ticks_before,
                                               tick_lateness_us,
                                               NOCTI_ABORT_NONE);

        if (begin_err != ESP_OK) {
            emit_abort_and_wait(slot,
                                t_tick_end,
                                missed_ticks_before,
                                tick_lateness_us,
                                NOCTI_ABORT_BEGIN_ERROR);
            advance_program_state(false);
            continue;
        }

        // SLOT_BEGIN performs only the physical LED-ON transaction and
        // non-blocking bookkeeping. The LED-OFF edge is tied to the absolute
        // slot deadline, never to callback completion.
        const nocti_time_us_t after_begin_us = nocti_timebase_now_us();
        if (after_begin_us + (nocti_time_us_t)slot->settle_us +
                (nocti_time_us_t)slot->sample_window_us >
            t_led_off_deadline) {
            // The actual LED-ON I2C completion was too late to guarantee the
            // configured minimum-valid settled window. Abort rather than accepting a
            // shorter or transition-contaminated optical window.
            emit_abort_and_wait(slot,
                                t_tick_end,
                                missed_ticks_before,
                                tick_lateness_us,
                                NOCTI_ABORT_LATE_BEGIN_COMPLETION);
            advance_program_state(false);
            continue;
        }

        wait_until_deadline_us(t_led_off_deadline);
        (void)emit_event(NOCTI_EV_SLOT_END,
                         slot,
                         t_led_off_deadline,
                         missed_ticks_before,
                         tick_lateness_us,
                         NOCTI_ABORT_NONE);

        const nocti_time_us_t after_end_us = nocti_timebase_now_us();
        if (after_end_us > t_tick_end) {
            const uint32_t overrun_us =
                saturating_u32_from_time(after_end_us - t_tick_end);
            s.stats.slot_overrun_count++;
            if (overrun_us > s.stats.max_slot_overrun_us) {
                s.stats.max_slot_overrun_us = overrun_us;
            }
        }

        wait_until_deadline_us(t_tick_end);
        advance_program_state(false);
    }
}

bool nocti_scheduler_init(const nocti_scheduler_cfg_t *cfg,
                          nocti_sched_cb_t cb,
                          void *user)
{
    if (!cfg || !cb) return false;
    if (!cfg->slots || cfg->slot_count == 0u) return false;
    if (cfg->tick_period_us == 0u || cfg->frame_period_us == 0u) return false;
    if (cfg->dark_us_total > cfg->frame_period_us) return false;
    if ((cfg->frame_period_us % cfg->tick_period_us) != 0u) return false;
    if ((cfg->dark_us_total % cfg->tick_period_us) != 0u) return false;

    const uint32_t ticks_per_frame =
        cfg->frame_period_us / cfg->tick_period_us;
    const uint32_t dark_ticks = cfg->dark_us_total / cfg->tick_period_us;
    if ((uint32_t)cfg->slot_count + dark_ticks != ticks_per_frame) return false;

    memset(&s, 0, sizeof(s));
    s.cfg = *cfg;
    s.cb = cb;
    s.user = user;

    if (s.cfg.slot_end_guard_us == 0u) {
        s.cfg.slot_end_guard_us = NOCTI_SCHED_DEFAULT_END_GUARD;
    }
    if (s.cfg.late_warn_us == 0u) {
        s.cfg.late_warn_us = NOCTI_SCHED_DEFAULT_LATE_WARN;
    }

    for (uint16_t i = 0u; i < s.cfg.slot_count; i++) {
        const nocti_slot_t *slot = &s.cfg.slots[i];
        const uint32_t settle_plus_window =
            (uint32_t)slot->settle_us + (uint32_t)slot->sample_window_us;
        const uint32_t total_tick =
            (uint32_t)slot->on_time_us + (uint32_t)slot->off_gap_us;

        if (settle_plus_window > (uint32_t)slot->on_time_us) return false;
        if (settle_plus_window > (uint32_t)slot->off_gap_us) return false;
        if (total_tick != s.cfg.tick_period_us) return false;
        if ((uint32_t)slot->on_time_us +
                (uint32_t)s.cfg.slot_end_guard_us >
            s.cfg.tick_period_us) return false;
        if (slot->wavelength != NOCTI_WL_735 &&
            slot->wavelength != NOCTI_WL_850) return false;
        if (slot->led_id >= NOCTI_MAX_FNIRS_SOURCES) return false;
    }

    if (s.cfg.stack_bytes == 0u) {
        s.cfg.stack_bytes = 4096u;
    }
    if (s.cfg.priority == 0u) {
        s.cfg.priority = 10u;
    }

    s.task_core_id = -1;

    BaseType_t task_created;
    if (s.cfg.pin_to_core) {
        task_created = xTaskCreatePinnedToCore(scheduler_task,
                                               "nocti_sched",
                                               s.cfg.stack_bytes,
                                               NULL,
                                               s.cfg.priority,
                                               &s.task,
                                               s.cfg.core_id);
    } else {
        task_created = xTaskCreate(scheduler_task,
                                   "nocti_sched",
                                   s.cfg.stack_bytes,
                                   NULL,
                                   s.cfg.priority,
                                   &s.task);
    }

    return task_created == pdPASS;
}

bool nocti_scheduler_start(void)
{
    if (!s.task || s.running) return false;

    s.frame_index = 0u;
    s.slot_index = 0u;
    s.frame_elapsed_us = 0u;
    s.dark_used_us = 0u;
    s.tick_seq = 0u;
    s.tick_lo = 0u;
    s.tick_hi = 0u;
    s.represented_tick_total = 0u;
    s.consumed_tick_total = 0u;
    memset(&s.stats, 0, sizeof(s.stats));

    s.running = true;

    const nocti_timebase_tick_cfg_t tick_cfg = {
        .tick_period_us = s.cfg.tick_period_us,
        .isr_cb = timebase_tick_isr,
        .user = (void *)s.task,
    };

    if (!nocti_timebase_start_tick(&tick_cfg)) {
        s.running = false;
        return false;
    }

    return true;
}

void nocti_scheduler_stop(void)
{
    if (!s.running) return;

    s.running = false;
    nocti_timebase_stop_tick();
}

bool nocti_scheduler_is_running(void)
{
    return s.running;
}

void nocti_scheduler_get_stats(nocti_scheduler_stats_t *out_stats)
{
    if (!out_stats) return;
    *out_stats = s.stats;
    out_stats->task_core_id = s.task_core_id;
}
