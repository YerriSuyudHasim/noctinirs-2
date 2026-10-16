// ============================================================
// File: components/nocti_timebase/nocti_timebase.c
// NoctiNIRS master monotonic clock based on one ESP32-S3 GPTimer
// ============================================================

#include "nocti_timebase.h"

#include <limits.h>
#include <string.h>

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "nocti_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nocti_timebase";

#define NOCTI_TIMEBASE_RESOLUTION_HZ       (1000000u)
#define NOCTI_TIMEBASE_SLEEP_SPIN_TAIL_US  (200u)
#define NOCTI_TIMEBASE_ROM_DELAY_GUARD_US  (8u)

// Leave enough distance between the current counter value and the newly armed
// absolute alarm. If ISR dispatch is almost one full period late, skip that
// unsafe near-future deadline instead of risking an alarm programmed in the
// past while gptimer_set_alarm_action() is executing.
#define NOCTI_TIMEBASE_REARM_GUARD_US      (50u)

typedef enum {
    NOCTI_TB_UNINITIALIZED = 0,
    NOCTI_TB_INITIALIZING,
    NOCTI_TB_READY,
} nocti_timebase_state_t;

static gptimer_handle_t s_timer = NULL;

// Explicit DRAM placement keeps callback state available while flash cache is
// disabled. The associated GPTimer Kconfig options are enabled in sdkconfig.
static DRAM_ATTR portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR volatile nocti_timebase_state_t s_state =
    NOCTI_TB_UNINITIALIZED;
static DRAM_ATTR volatile bool s_tick_running = false;
static DRAM_ATTR nocti_timebase_tick_cfg_t s_tick_cfg;
static DRAM_ATTR uint32_t s_tick_generation = 0u;
static DRAM_ATTR uint64_t s_last_delivered_us = 0u;
static DRAM_ATTR uint32_t s_resolution_hz = 0u;
static DRAM_ATTR nocti_timebase_status_t s_status;

static inline uint32_t IRAM_ATTR saturating_u32(uint64_t value)
{
    return value > (uint64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static inline bool IRAM_ATTR timer_read_us(uint64_t *out_us)
{
    if (!out_us || s_timer == NULL || s_state != NOCTI_TB_READY) {
        return false;
    }

    uint64_t count = 0u;
    const esp_err_t err = gptimer_get_raw_count(s_timer, &count);
    if (err != ESP_OK) {
        __atomic_fetch_add(&s_status.read_error_count, 1u, __ATOMIC_RELAXED);
        return false;
    }

    // The timer is required to resolve exactly one count per microsecond.
    *out_us = count;
    return true;
}

static bool IRAM_ATTR on_master_alarm(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_ctx)
{
    (void)user_ctx;

    if (!timer || !edata) return false;

    if (__atomic_load_n(&s_status.alarm_isr_core_id, __ATOMIC_RELAXED) < 0) {
        __atomic_store_n(&s_status.alarm_isr_core_id,
                         (int8_t)xPortGetCoreID(),
                         __ATOMIC_RELAXED);
    }

    nocti_timebase_tick_cfg_t cfg;
    uint32_t generation;
    bool running;

    portENTER_CRITICAL_ISR(&s_lock);
    running = s_tick_running;
    cfg = s_tick_cfg;
    generation = s_tick_generation;
    portEXIT_CRITICAL_ISR(&s_lock);

    if (!running || cfg.tick_period_us == 0u) return false;

    const uint64_t original_alarm_us = edata->alarm_value;
    const uint64_t observed_us = edata->count_value;
    const uint64_t period_us = (uint64_t)cfg.tick_period_us;

    const uint64_t lateness_us = observed_us > original_alarm_us
        ? observed_us - original_alarm_us
        : 0u;

    const uint32_t lateness32 = saturating_u32(lateness_us);
    uint32_t previous_max = __atomic_load_n(
        &s_status.max_isr_lateness_us, __ATOMIC_RELAXED);
    while (lateness32 > previous_max &&
           !__atomic_compare_exchange_n(
               &s_status.max_isr_lateness_us,
               &previous_max,
               lateness32,
               false,
               __ATOMIC_RELAXED,
               __ATOMIC_RELAXED)) {
    }

    // The callback timestamp must never point into the future. First find the
    // newest ideal deadline that was already due when the ISR captured the
    // counter. Then arm the first deadline that remains safely in the future.
    const uint64_t due_periods_after_alarm =
        observed_us > original_alarm_us
            ? (observed_us - original_alarm_us) / period_us
            : 0u;
    const uint64_t latest_scheduled_us =
        original_alarm_us + due_periods_after_alarm * period_us;

    uint64_t guarded_observed_us = observed_us;
    if (UINT64_MAX - guarded_observed_us >= NOCTI_TIMEBASE_REARM_GUARD_US) {
        guarded_observed_us += NOCTI_TIMEBASE_REARM_GUARD_US;
    } else {
        guarded_observed_us = UINT64_MAX;
    }

    uint64_t next_alarm_us = latest_scheduled_us + period_us;
    if (next_alarm_us <= guarded_observed_us) {
        const uint64_t additional_periods =
            ((guarded_observed_us - next_alarm_us) / period_us) + 1u;
        next_alarm_us += additional_periods * period_us;
    }

    const gptimer_alarm_config_t next_alarm = {
        .alarm_count = next_alarm_us,
        .reload_count = 0u,
        .flags.auto_reload_on_alarm = false,
    };

    // Serialize rearm against STOP/START. If the generation changed, this is a
    // stale interrupt from an older schedule and must not rearm the new one.
    portENTER_CRITICAL_ISR(&s_lock);
    if (!s_tick_running || s_tick_generation != generation) {
        portEXIT_CRITICAL_ISR(&s_lock);
        return false;
    }

    uint64_t elapsed_ticks64 = 1u;
    if (latest_scheduled_us > s_last_delivered_us) {
        elapsed_ticks64 =
            (latest_scheduled_us - s_last_delivered_us) / period_us;
        if (elapsed_ticks64 == 0u) elapsed_ticks64 = 1u;
    }

    uint32_t elapsed_ticks = UINT32_MAX;
    if (elapsed_ticks64 <= (uint64_t)UINT32_MAX) {
        elapsed_ticks = (uint32_t)elapsed_ticks64;
    } else {
        s_status.excessive_catchup_count += 1u;
    }

    const esp_err_t rearm_err = gptimer_set_alarm_action(timer, &next_alarm);
    if (rearm_err != ESP_OK) {
        s_tick_running = false;
        s_status.tick_running = false;
        s_status.alarm_rearm_error_count += 1u;
        portEXIT_CRITICAL_ISR(&s_lock);
        return false;
    }

    s_last_delivered_us = latest_scheduled_us;
    s_status.next_alarm_us = next_alarm_us;
    s_status.alarm_irq_count += 1u;
    s_status.represented_tick_count += elapsed_ticks64;
    if (elapsed_ticks64 > 1u) {
        s_status.catchup_tick_count += elapsed_ticks64 - 1u;
    }
    portEXIT_CRITICAL_ISR(&s_lock);

    // Report only the newest due ideal tick. The scheduler receives the total
    // represented period count separately and advances skipped program slots in
    // task context, never through an ISR callback loop.
    if (cfg.isr_cb) {
        return cfg.isr_cb((nocti_time_us_t)latest_scheduled_us,
                          elapsed_ticks,
                          cfg.user);
    }
    return false;
}

static void reset_runtime_state(void)
{
    portENTER_CRITICAL(&s_lock);
    const int8_t init_core_id = s_status.init_core_id;
    const int8_t alarm_isr_core_id = s_status.alarm_isr_core_id;
    memset(&s_tick_cfg, 0, sizeof(s_tick_cfg));
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialized = true;
    s_status.init_core_id = init_core_id;
    s_status.alarm_isr_core_id = alarm_isr_core_id;
    s_status.resolution_hz = s_resolution_hz;
    s_tick_running = false;
    s_tick_generation += 1u;
    s_last_delivered_us = 0u;
    portEXIT_CRITICAL(&s_lock);
}

bool nocti_timebase_init(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_state == NOCTI_TB_READY) {
        portEXIT_CRITICAL(&s_lock);
        return true;
    }
    if (s_state == NOCTI_TB_INITIALIZING) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    s_state = NOCTI_TB_INITIALIZING;
    portEXIT_CRITICAL(&s_lock);

    gptimer_handle_t timer = NULL;

    // Select XTAL explicitly rather than DEFAULT/APB. This keeps the NoctiNIRS
    // clock independent of later CPU/APB frequency scaling. The driver verifies
    // that the requested 1 MHz resolution was actually produced.
    const gptimer_config_t timer_cfg = {
        .clk_src = GPTIMER_CLK_SRC_XTAL,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = NOCTI_TIMEBASE_RESOLUTION_HZ,
        .intr_priority = 0,
    };

    esp_err_t err = gptimer_new_timer(&timer_cfg, &timer);
    if (err != ESP_OK || timer == NULL) {
        NOCTI_LOGE(TAG, "gptimer_new_timer failed: %s", esp_err_to_name(err));
        goto fail;
    }

    // ESP-IDF requires the first callback registration before enable.
    const gptimer_event_callbacks_t callbacks = {
        .on_alarm = on_master_alarm,
    };
    err = gptimer_register_event_callbacks(timer, &callbacks, NULL);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "register callback failed: %s", esp_err_to_name(err));
        goto fail_delete;
    }

    err = gptimer_enable(timer);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "gptimer_enable failed: %s", esp_err_to_name(err));
        goto fail_delete;
    }

    err = gptimer_set_raw_count(timer, 0u);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "set_raw_count failed: %s", esp_err_to_name(err));
        goto fail_disable;
    }

    uint32_t actual_resolution_hz = 0u;
    err = gptimer_get_resolution(timer, &actual_resolution_hz);
    if (err != ESP_OK || actual_resolution_hz != NOCTI_TIMEBASE_RESOLUTION_HZ) {
        NOCTI_LOGE(TAG,
                   "unexpected resolution: err=%s actual=%u expected=%u",
                   esp_err_to_name(err),
                   (unsigned)actual_resolution_hz,
                   (unsigned)NOCTI_TIMEBASE_RESOLUTION_HZ);
        goto fail_disable;
    }

    err = gptimer_start(timer);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "gptimer_start failed: %s", esp_err_to_name(err));
        goto fail_disable;
    }

    portENTER_CRITICAL(&s_lock);
    s_timer = timer;
    s_resolution_hz = actual_resolution_hz;
    s_status.init_core_id = (int8_t)xPortGetCoreID();
    s_status.alarm_isr_core_id = -1;
    s_state = NOCTI_TB_READY;
    portEXIT_CRITICAL(&s_lock);

    reset_runtime_state();

    NOCTI_LOGI(TAG,
               "init ok: one GPTimer, XTAL source, resolution=%u Hz, core=%d",
               (unsigned)s_resolution_hz,
               (int)s_status.init_core_id);
    return true;

fail_disable:
    (void)gptimer_disable(timer);
fail_delete:
    (void)gptimer_del_timer(timer);
fail:
    portENTER_CRITICAL(&s_lock);
    s_timer = NULL;
    s_resolution_hz = 0u;
    s_state = NOCTI_TB_UNINITIALIZED;
    portEXIT_CRITICAL(&s_lock);
    return false;
}

bool nocti_timebase_is_ready(void)
{
    return s_state == NOCTI_TB_READY && s_timer != NULL;
}

bool IRAM_ATTR nocti_timebase_try_now_us(nocti_time_us_t *out_us)
{
    if (!out_us) return false;

    uint64_t now_us = 0u;
    if (!timer_read_us(&now_us)) return false;

    *out_us = (nocti_time_us_t)now_us;
    return true;
}

nocti_time_us_t nocti_timebase_now_us(void)
{
    nocti_time_us_t now_us = 0;
    (void)nocti_timebase_try_now_us(&now_us);
    return now_us;
}

nocti_time_us_t IRAM_ATTR nocti_timebase_now_us_isr(void)
{
    nocti_time_us_t now_us = 0;
    (void)nocti_timebase_try_now_us(&now_us);
    return now_us;
}

bool nocti_timebase_start_tick(const nocti_timebase_tick_cfg_t *cfg)
{
    if (!cfg || cfg->tick_period_us == 0u) return false;
    if (!nocti_timebase_is_ready() && !nocti_timebase_init()) return false;

    uint64_t now_us = 0u;
    if (!timer_read_us(&now_us)) return false;

    const uint64_t first_alarm_us = now_us + (uint64_t)cfg->tick_period_us;
    const gptimer_alarm_config_t alarm = {
        .alarm_count = first_alarm_us,
        .reload_count = 0u,
        .flags.auto_reload_on_alarm = false,
    };

    portENTER_CRITICAL(&s_lock);
    if (s_tick_running) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }

    s_tick_generation += 1u;
    s_tick_cfg = *cfg;
    s_tick_running = true;
    s_status.tick_running = true;
    s_status.tick_period_us = cfg->tick_period_us;
    s_status.next_alarm_us = first_alarm_us;
    s_last_delivered_us = first_alarm_us - (uint64_t)cfg->tick_period_us;
    s_status.alarm_irq_count = 0u;
    s_status.represented_tick_count = 0u;
    s_status.catchup_tick_count = 0u;
    s_status.max_isr_lateness_us = 0u;
    s_status.alarm_rearm_error_count = 0u;
    s_status.excessive_catchup_count = 0u;
    portEXIT_CRITICAL(&s_lock);

    const esp_err_t err = gptimer_set_alarm_action(s_timer, &alarm);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_tick_generation += 1u;
        s_tick_running = false;
        s_status.tick_running = false;
        s_status.next_alarm_us = 0u;
        s_last_delivered_us = 0u;
        memset(&s_tick_cfg, 0, sizeof(s_tick_cfg));
        portEXIT_CRITICAL(&s_lock);

        NOCTI_LOGE(TAG, "set first alarm failed: %s", esp_err_to_name(err));
        return false;
    }

    NOCTI_LOGI(TAG,
               "tick started: period_us=%u first_alarm_us=%llu",
               (unsigned)cfg->tick_period_us,
               (unsigned long long)first_alarm_us);
    return true;
}

void nocti_timebase_stop_tick(void)
{
    if (!nocti_timebase_is_ready()) return;

    portENTER_CRITICAL(&s_lock);
    const bool was_running = s_tick_running;
    s_tick_generation += 1u;
    s_tick_running = false;
    s_status.tick_running = false;
    s_status.next_alarm_us = 0u;
    s_last_delivered_us = 0u;
    portEXIT_CRITICAL(&s_lock);

    if (!was_running) return;

    const esp_err_t err = gptimer_set_alarm_action(s_timer, NULL);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "disable alarm failed: %s", esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_lock);
    memset(&s_tick_cfg, 0, sizeof(s_tick_cfg));
    portEXIT_CRITICAL(&s_lock);

    NOCTI_LOGI(TAG, "tick stopped; master counter remains running");
}

void nocti_timebase_get_status(nocti_timebase_status_t *out_status)
{
    if (!out_status) return;

    memset(out_status, 0, sizeof(*out_status));

    portENTER_CRITICAL(&s_lock);
    out_status->initialized = s_state == NOCTI_TB_READY;
    out_status->tick_running = s_tick_running;
    // v15.1/v16: preserve the actual allocation/ISR core IDs in the public
    // snapshot. Leaving these fields zeroed incorrectly fails the core plan.
    out_status->init_core_id = s_status.init_core_id;
    out_status->alarm_isr_core_id = s_status.alarm_isr_core_id;
    out_status->resolution_hz = s_resolution_hz;
    out_status->tick_period_us = s_status.tick_period_us;
    out_status->next_alarm_us = s_status.next_alarm_us;
    out_status->alarm_irq_count = s_status.alarm_irq_count;
    out_status->represented_tick_count = s_status.represented_tick_count;
    out_status->catchup_tick_count = s_status.catchup_tick_count;
    out_status->alarm_rearm_error_count =
        s_status.alarm_rearm_error_count;
    portEXIT_CRITICAL(&s_lock);

    // Timestamp reads can fail in task or ISR context without taking s_lock.
    out_status->max_isr_lateness_us = __atomic_load_n(
        &s_status.max_isr_lateness_us, __ATOMIC_RELAXED);
    out_status->read_error_count = __atomic_load_n(
        &s_status.read_error_count, __ATOMIC_RELAXED);
    out_status->excessive_catchup_count = __atomic_load_n(
        &s_status.excessive_catchup_count, __ATOMIC_RELAXED);
}

void nocti_timebase_busywait_us(uint32_t us)
{
    if (us == 0u) return;
    if (!nocti_timebase_is_ready()) {
        esp_rom_delay_us(us);
        return;
    }

    uint64_t start_us = 0u;
    if (!timer_read_us(&start_us)) {
        esp_rom_delay_us(us);
        return;
    }
    const uint64_t deadline_us = start_us + (uint64_t)us;

    // Avoid repeatedly taking the GPTimer driver's short spinlock for the whole
    // delay. The final absolute check still guarantees no undershoot after a
    // preemption or interrupt.
    if (us > (NOCTI_TIMEBASE_ROM_DELAY_GUARD_US * 2u)) {
        esp_rom_delay_us(us - NOCTI_TIMEBASE_ROM_DELAY_GUARD_US);
    }

    while (true) {
        uint64_t now_us = 0u;
        if (!timer_read_us(&now_us)) {
            // The coarse delay already consumed nearly the entire request. A
            // small ROM guard is safer than hanging forever on a failed read.
            esp_rom_delay_us(NOCTI_TIMEBASE_ROM_DELAY_GUARD_US);
            return;
        }
        if (now_us >= deadline_us) return;
        __asm__ __volatile__("nop");
    }
}

void nocti_timebase_sleep_until_us(nocti_time_us_t deadline_us)
{
    const uint32_t rtos_tick_us =
        configTICK_RATE_HZ > 0
            ? 1000000u / (uint32_t)configTICK_RATE_HZ
            : 1000u;

    nocti_time_us_t last_now_us = 0;
    while (true) {
        nocti_time_us_t now_us = 0;
        if (!nocti_timebase_try_now_us(&now_us)) {
            // A master-clock read failure is a health fault. Preserve the no-
            // undershoot property using the last known remaining interval.
            const nocti_time_us_t remaining_us = deadline_us - last_now_us;
            if (remaining_us > 0) {
                esp_rom_delay_us(saturating_u32((uint64_t)remaining_us));
            }
            return;
        }
        last_now_us = now_us;

        const nocti_time_us_t remaining_us = deadline_us - now_us;
        if (remaining_us <= 0) return;

        if (remaining_us <=
            (nocti_time_us_t)NOCTI_TIMEBASE_SLEEP_SPIN_TAIL_US) {
            nocti_timebase_busywait_us((uint32_t)remaining_us);
            return;
        }

        const uint64_t coarse_us =
            (uint64_t)remaining_us - NOCTI_TIMEBASE_SLEEP_SPIN_TAIL_US;
        TickType_t ticks =
            rtos_tick_us > 0u ? (TickType_t)(coarse_us / rtos_tick_us) : 0u;

        if (ticks > 0u) {
            vTaskDelay(ticks);
        } else {
            taskYIELD();
        }
    }
}
