/* Opt-in LED-only owner. Host controls duration; STOP/lease/faults cancel.
 * No ADC, no streaming samples, no I2C from ISR. Stop code2 is reserved. */
#include "sdkconfig.h"
#include "nn2_led_bringup.h"
#if CONFIG_NN2_OPTICAL_ACQUISITION
#include "nn2_acquisition_esp.h"
#include "esp_timer.h"
static uint64_t s_clock_origin;
#endif
#include "nn2_board.h"
#include "nn2_board_pca.h"
#include "nn2_board_wiring.h"
#include "nn2_rtos_config.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#if defined(ESP_PLATFORM) && CONFIG_NN2_OPTICAL_ACQUISITION && NN2_RTOS_LED_TIMING_DIAGNOSTICS
#include "esp_log.h"
#endif
#ifndef CONFIG_NN2_LED_RUNTIME_I2C_HZ
#define CONFIG_NN2_LED_RUNTIME_I2C_HZ 400000
#endif
#define NN2_STRING_(x) #x
#define NN2_STRING(x) NN2_STRING_(x)

#if !CONFIG_GPIO_CTRL_FUNC_IN_IRAM || !CONFIG_GPTIMER_ISR_CACHE_SAFE || !CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM || !CONFIG_I2C_ISR_IRAM_SAFE
#error "LED bring-up requires GPIO/GPTimer/I2C IRAM/cache-safe configuration"
#endif
#if CONFIG_SPIRAM
#error "LED bring-up requires PSRAM disabled"
#endif
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static nn2_led_bringup_status_t s_status;
static nn2_port_optical_config_t s_config;
static uint32_t s_generation, s_worker_generation, s_half_us, s_phase;
static uint64_t s_alarm;
static bool s_armed, s_busy, s_start_pending;
static bool s_defer_activation, s_prepared, s_activate_pending;
/* Bounded correlation recorder. Critical records are written under s_lock;
 * printing is explicitly requested by composition only after ADC STOP ACK. */
#if defined(ESP_PLATFORM) && CONFIG_NN2_OPTICAL_ACQUISITION && NN2_RTOS_LED_TIMING_DIAGNOSTICS
typedef struct {
    uint64_t begin_us, end_us;
    uint32_t frame;
    uint16_t line;
    uint8_t kind, phase; /* 1=interlock hold, 2=PCA phase operation. */
} led_timing_record_t;
static struct {
    led_timing_record_t records[64], longest;
    uint64_t hold_begin_us;
    uint32_t head, generation;
    uint16_t hold_line, depth;
    bool frozen, reported;
} s_timing;
_Static_assert(sizeof(s_timing) <= 2048u, "LED timing recorder exceeds 2 KiB");
static void IRAM_ATTR timing_append_locked(uint8_t kind, uint64_t begin, uint64_t end, uint16_t line)
{
    if (s_timing.frozen) return;
    led_timing_record_t *r=&s_timing.records[s_timing.head++ % 64u];
    /* Scalar stores: no out-of-line structure-copy helper in the ISR. */
    r->begin_us=begin; r->end_us=end; r->line=line;
    r->kind=kind; r->phase=(uint8_t)s_phase; r->frame=s_status.frames;
    if (kind==1u && end-begin > s_timing.longest.end_us-s_timing.longest.begin_us) {
        s_timing.longest.begin_us=begin; s_timing.longest.end_us=end;
        s_timing.longest.line=line; s_timing.longest.kind=kind;
        s_timing.longest.phase=(uint8_t)s_phase; s_timing.longest.frame=s_status.frames;
    }
}
static void IRAM_ATTR timing_begin_locked(uint16_t line)
{
    if (s_timing.depth++ == 0u) {
        s_timing.hold_begin_us=(uint64_t)esp_timer_get_time(); s_timing.hold_line=line;
    }
}
static void IRAM_ATTR timing_end_locked(void)
{
    if (--s_timing.depth == 0u && !s_timing.frozen) {
        uint64_t end=(uint64_t)esp_timer_get_time();
        if (end-s_timing.hold_begin_us >= 8u)
            timing_append_locked(1u,s_timing.hold_begin_us,end,s_timing.hold_line);
    }
}
static void IRAM_ATTR timing_freeze_locked(void)
{
    /* Include the in-progress stop/fault interlock as a partial record. */
    if (!s_timing.frozen && s_timing.depth)
        timing_append_locked(1u,s_timing.hold_begin_us,(uint64_t)esp_timer_get_time(),s_timing.hold_line);
    s_timing.frozen=true;
}
static void timing_reset_locked(void)
{
    s_timing.head=0u; s_timing.generation=s_generation;
    s_timing.longest.begin_us=s_timing.longest.end_us=0u;
    s_timing.longest.line=0u; s_timing.longest.phase=0u; s_timing.longest.frame=0u;
    s_timing.frozen=s_timing.reported=false;
}
static void timing_activity(uint64_t begin, uint16_t line)
{
    uint64_t end=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    timing_append_locked(2u,begin,end,line);
    portEXIT_CRITICAL(&s_lock);
}
#define LED_ENTER() do { portENTER_CRITICAL(&s_lock); timing_begin_locked(__LINE__); } while (0)
#define LED_EXIT() do { timing_end_locked(); portEXIT_CRITICAL(&s_lock); } while (0)
#define LED_ENTER_ISR() do { portENTER_CRITICAL_ISR(&s_lock); timing_begin_locked(__LINE__); } while (0)
#define LED_EXIT_ISR() do { timing_end_locked(); portEXIT_CRITICAL_ISR(&s_lock); } while (0)
#define LED_TIMING_FREEZE() timing_freeze_locked()
#define LED_TIMING_RESET() timing_reset_locked()
#define LED_TIMING_NOW() ((uint64_t)esp_timer_get_time())
#define LED_TIMING_ACTIVITY(begin) timing_activity(begin,__LINE__)
#else
#define LED_ENTER() portENTER_CRITICAL(&s_lock)
#define LED_EXIT() portEXIT_CRITICAL(&s_lock)
#define LED_ENTER_ISR() portENTER_CRITICAL_ISR(&s_lock)
#define LED_EXIT_ISR() portEXIT_CRITICAL_ISR(&s_lock)
#define LED_TIMING_FREEZE() ((void)0)
#define LED_TIMING_RESET() ((void)0)
#define LED_TIMING_NOW() 0ULL
#define LED_TIMING_ACTIVITY(begin) ((void)(begin))
#endif
#ifndef CONFIG_NN2_LED_SHORT_BENCH
#define CONFIG_NN2_LED_SHORT_BENCH 0
#endif
/* Wire reply overhead is60 bytes; INFO must fit the256-byte reply cache. */
static const char s_advisory_info[] = "NoctiNIRS2/0.1.0;wire=ADR0004/1;SIMULATED;led=REAL_BRINGUP;short_policy=BENCH_WARN;i2c=ASYNC@" NN2_STRING(CONFIG_NN2_LED_RUNTIME_I2C_HZ) ";duration=HOST;fs_min=2;fs_max=10;limit_s=0;single_run=0;frames=unsupported;capture=unsupported";
static const char s_strict_info[] = "NoctiNIRS2/0.1.0;wire=ADR0004/1;SIMULATED;led=REAL_BRINGUP;short_policy=STRICT;i2c=ASYNC@" NN2_STRING(CONFIG_NN2_LED_RUNTIME_I2C_HZ) ";duration=HOST;fs_min=2;fs_max=10;limit_s=0;single_run=0;frames=unsupported;capture=unsupported";
_Static_assert(sizeof(s_advisory_info) - 1 <= 196, "INFO exceeds reply cache");
_Static_assert(sizeof(s_strict_info) - 1 <= 196, "INFO exceeds reply cache");
enum { STOP_NONE, STOP_CANCEL, STOP_CAP_RESERVED, STOP_DEADLINE, STOP_TIMER, STOP_IO, STOP_CLEANUP };
/* Called under the same task/ISR interlock as cancellation and reset release. */
static void IRAM_ATTR stop_locked(uint8_t cause, esp_err_t error)
{
    LED_TIMING_FREEZE();
    s_armed = false;
    s_prepared = s_activate_pending = false;
    s_status.state = NN2_OPTICAL_FAULT;
    if (!s_status.first_stop) {
        s_status.first_stop = cause;
        s_status.error = error;
        s_status.first_fault = s_status.pca;
    }
    (void)gpio_set_level(NN2_BOARD_RESET_LED_GPIO, 0);
}

/* Hardware handles below belong exclusively to the LED task. */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_pca;
static gptimer_handle_t s_timer;
static bool s_timer_enabled, s_timer_started;
static bool s_timer_callbacks;
static nn2_board_pca_t s_chip;
static esp_err_t s_hw_error;
/* The async driver retains pointers. Never lend it a caller's stack storage.
 * One owner, one outstanding transfer; buffers survive cancellation and drain. */
static bool s_async, s_i2c_callbacks, s_pending, s_done;
static uint32_t s_io_generation;
static esp_err_t s_io_result;
static uint8_t s_tx[32], s_rx[24];
static uint64_t s_io_complete_us, s_edge_commit_us;

static bool IRAM_ATTR transfer_done(i2c_master_dev_handle_t dev,
    const i2c_master_event_data_t *event, void *ctx)
{
    (void)dev; (void)ctx;
    LED_ENTER_ISR();
    if (s_pending) {
#if CONFIG_NN2_OPTICAL_ACQUISITION
        s_io_complete_us = (uint64_t)esp_timer_get_time();
#endif
        s_io_result = event->event == I2C_EVENT_DONE ? ESP_OK :
            event->event == I2C_EVENT_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        s_status.i2c_event = (uint8_t)event->event;
        s_pending = false;
        s_done = true;
        ++s_status.i2c_completed;
        if (s_io_result != ESP_OK) ++s_status.i2c_errors;
        if (s_io_generation != s_generation || !s_armed) ++s_status.i2c_stale;
        else if (s_io_result != ESP_OK) stop_locked(STOP_IO, s_io_result);
    }
    LED_EXIT_ISR();
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &wake);
    /* IDF 6.1 i2c_master.c discards the callback's returned wake flag.
     * Yield explicitly so a 100 Hz FreeRTOS tick cannot delay this owner. */
    if (wake == pdTRUE) portYIELD_FROM_ISR();
    return wake == pdTRUE;
}

static esp_err_t await_transfer(esp_err_t submitted)
{
    if (submitted != ESP_OK) {
        /* The API did not accept the transaction; no callback is owed. */
        LED_ENTER();
        s_pending = false;
        LED_EXIT();
        return submitted;
    }
    TickType_t began = xTaskGetTickCount();
    for (;;) {
        LED_ENTER();
        bool done = s_done;
        bool valid = s_armed && s_io_generation == s_generation;
        esp_err_t result = s_io_result;
        LED_EXIT();
        if (!valid) return ESP_ERR_INVALID_STATE;
        if (done) return result;
        TickType_t elapsed = xTaskGetTickCount() - began;
        TickType_t budget = pdMS_TO_TICKS(NN2_RTOS_LED_I2C_TIMEOUT_MS);
        if (elapsed >= budget) return ESP_ERR_TIMEOUT;
        (void)ulTaskNotifyTake(pdTRUE, budget - elapsed);
    }
}

static bool begin_transfer(void)
{
    LED_ENTER();
    bool valid = s_armed && s_generation == s_worker_generation && !s_pending;
    if (valid) {
        s_pending = true;
        s_done = false;
        s_io_result = ESP_FAIL;
        s_io_generation = s_generation;
        ++s_status.i2c_submitted;
    }
    LED_EXIT();
    return valid;
}

static nn2_status_t cancel(void *ctx)
{
    (void)ctx;
    LED_ENTER();
    if (s_armed && !s_status.first_stop) s_status.first_stop = STOP_CANCEL;
    LED_TIMING_FREEZE();
    s_armed = false;
    s_start_pending = false;
    ++s_generation;
    if (s_status.state != NN2_OPTICAL_IDLE && s_status.state != NN2_OPTICAL_FAULT)
        s_status.state = NN2_OPTICAL_STOPPING;
    esp_err_t err = nn2_board_hold_emitters_off();
    LED_EXIT();
    if (s_task) xTaskNotifyGive(s_task);
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

#include "nn2_led_scheduler.inc"

/* Timer ISR records immutable absolute deadlines; scheduler task dispatches.
 * Scalar event writes keep the ISR independent of memcpy implementation. */
static bool IRAM_ATTR alarm_callback(gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *event, void *ctx)
{
    (void)ctx;
    bool notify=false;
    uint64_t now=0;
    esp_err_t count_result=gptimer_get_raw_count(timer,&now);
    LED_ENTER_ISR();
    if (s_armed && s_status.state==NN2_OPTICAL_RUNNING) {
        notify=true;
        if (count_result!=ESP_OK) stop_locked(STOP_TIMER,count_result);
        else if (now>=event->alarm_value+s_half_us ||
                 s_deadline_head-s_deadline_tail==NN2_SCHED_DEADLINES) {
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE,ESP_ERR_TIMEOUT);
        } else {
            led_deadline_t *edge=&s_deadlines[s_deadline_head++ % NN2_SCHED_DEADLINES];
            edge->alarm=event->alarm_value; edge->generation=s_generation;
            edge->id=++s_next_command; edge->phase=s_next_phase; edge->frame=s_next_frame;
            s_next_phase=(s_next_phase+1u)%40u;
            if (!s_next_phase) ++s_next_frame;
            gptimer_alarm_config_t alarm={.alarm_count=event->alarm_value+s_half_us};
            /* Permission + rearm use the STOP interlock; never rebase on now. */
            if (gptimer_set_alarm_action(timer,&alarm)!=ESP_OK)
                stop_locked(STOP_TIMER,ESP_FAIL);
        }
    }
    LED_EXIT_ISR();
    BaseType_t wake=pdFALSE;
    if (notify) vTaskNotifyGiveFromISR(s_scheduler_task,&wake);
    return wake==pdTRUE;
}
static bool still_armed(void)
{
    LED_ENTER();
    bool ok = s_armed && s_generation == s_worker_generation;
    if (ok && CONFIG_NN2_LED_SHORT_BENCH && s_timer_started) {
        uint64_t now = 0;
        if (gptimer_get_raw_count(s_timer, &now) != ESP_OK) stop_locked(STOP_TIMER, ESP_FAIL);
        ok = s_armed;
    }
    LED_EXIT();
    return ok;
}
static nn2_status_t chip_reset(void *ctx, bool asserted)
{
    (void)ctx;
    LED_ENTER();
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (asserted) result = nn2_board_hold_emitters_off();
    else if (s_armed && s_generation == s_worker_generation)
        result = nn2_board_release_emitters_reset();
    LED_EXIT();
    return result == ESP_OK ? NN2_OK : NN2_ERR_IO;
}
static void chip_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    esp_rom_delay_us(us);
}
static nn2_status_t chip_write(void *ctx, const uint8_t *data, size_t n)
{
    (void)ctx;
    if (!still_armed()) return NN2_ERR_INVALID_STATE;
    esp_err_t err;
    if (s_async) {
        if (n > sizeof(s_tx) || !begin_transfer()) return NN2_ERR_INVALID_STATE;
        memcpy(s_tx, data, n);
        err = await_transfer(i2c_master_transmit(s_pca, s_tx, n, NN2_RTOS_LED_I2C_TIMEOUT_MS));
    } else err = i2c_master_transmit(s_pca, data, n, NN2_RTOS_LED_I2C_TIMEOUT_MS);
    if (err != ESP_OK) s_hw_error = err;
    else if (s_async) s_edge_commit_us=s_io_complete_us;
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}
static nn2_status_t chip_read(void *ctx, uint8_t reg, uint8_t *data, size_t n)
{
    (void)ctx;
    if (!still_armed()) return NN2_ERR_INVALID_STATE;
    esp_err_t err;
    if (s_async) {
        if (n > sizeof(s_rx) || !begin_transfer()) return NN2_ERR_INVALID_STATE;
        s_tx[0] = reg;
        err = await_transfer(i2c_master_transmit_receive(s_pca, s_tx, 1, s_rx, n,
            NN2_RTOS_LED_I2C_TIMEOUT_MS));
        if (err == ESP_OK) memcpy(data, s_rx, n);
    } else err = i2c_master_transmit_receive(s_pca, &reg, 1, data, n,
        NN2_RTOS_LED_I2C_TIMEOUT_MS);
    if (err != ESP_OK) s_hw_error = err;
    if (err == ESP_OK && ((reg == 1 && n == 1) || (reg == 0x80 && n == 2))) {
        LED_ENTER();
        s_status.pca_mode2 = data[n - 1];
        LED_EXIT();
    }
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

static void fault(esp_err_t error)
{
    LED_ENTER();
    stop_locked(STOP_IO, error);
    LED_EXIT();
}
static bool close_bus(void)
{
    /* Reset has already been asserted by the caller. A failed drain keeps
     * handles, callback context and buffers alive, and denies the next START. */
    if (s_async && s_bus) {
        esp_err_t drained = i2c_master_bus_wait_all_done(s_bus, NN2_RTOS_LED_I2C_TIMEOUT_MS);
        LED_ENTER();
        bool pending = s_pending;
        /* Pinned IDF6.1 NACK/TIMEOUT can finish internally without calling
         * on_trans_done. wait_all_done does not prove the direct transaction's
         * lifetime ended: it only drains queued records. Retain everything.
         * No callback + no public quiescence proof => MCU reboot required. */
        if (pending) s_status.reboot_required = true;
        LED_EXIT();
        if (drained != ESP_OK || pending) return false;
    }
    if (s_i2c_callbacks) {
        const i2c_master_event_callbacks_t callbacks = {0};
        if (i2c_master_register_event_callbacks(s_pca, &callbacks, NULL) != ESP_OK) return false;
        s_i2c_callbacks = false;
    }
    if (s_pca) {
        if (i2c_master_bus_rm_device(s_pca) != ESP_OK) return false;
        s_pca = NULL;
    }
    if (s_bus) {
        if (i2c_del_master_bus(s_bus) != ESP_OK) return false;
        s_bus = NULL;
    }
    s_async = false;
    LED_ENTER();
    /* A late real completion followed by successful drain/detach is proof.
     * Otherwise this flag cannot clear via CLEAR_FAULT or another START. */
    s_status.reboot_required = false;
    LED_EXIT();
    return true;
}

static void cleanup(void)
{
    bool ok = nn2_board_hold_emitters_off() == ESP_OK;
    if (s_timer_started) {
        if (gptimer_stop(s_timer) == ESP_OK) s_timer_started = false;
        else ok = false;
    }
    if (s_timer_enabled && !s_timer_started) {
        if (gptimer_disable(s_timer) == ESP_OK) s_timer_enabled = false;
        else ok = false;
    }
    if (s_timer && !s_timer_callbacks && !s_timer_enabled && !s_timer_started) {
        if (gptimer_del_timer(s_timer) == ESP_OK) s_timer = NULL;
        else ok = false;
    }
    if (!close_bus()) ok = false;
    LED_ENTER();
    s_busy = s_edge_pending = false;
    bool scheduler_quiet=!s_scheduler_processing;
    s_status.bus_released = ok && scheduler_quiet && !s_bus && !s_pca && !s_timer_enabled && !s_timer_started &&
        (!s_timer || s_timer_callbacks);
    if (!s_status.bus_released && (!ok || scheduler_quiet)) {
        s_status.state = NN2_OPTICAL_FAULT;
        if (!s_status.first_stop) { s_status.first_stop = STOP_CLEANUP; s_status.error = ESP_FAIL; }
    } else if (s_status.bus_released && s_status.state != NN2_OPTICAL_FAULT) {
        s_status.state = NN2_OPTICAL_IDLE;
    }
    LED_EXIT();
}

static esp_err_t open_bus_mode(bool asynchronous)
{
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = NN2_BOARD_SDA_GPIO,
        .scl_io_num = NN2_BOARD_SCL_GPIO, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .trans_queue_depth = asynchronous ? 1 : 0,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &s_bus);
    if (err != ESP_OK) return err;
    s_async = asynchronous;
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = NN2_BOARD_PCA_ADDRESS,
        .scl_speed_hz = asynchronous ? CONFIG_NN2_LED_RUNTIME_I2C_HZ : 100000, .scl_wait_us = 1000,
    };
    err = i2c_master_bus_add_device(s_bus, &device, &s_pca);
    if (err == ESP_OK && asynchronous) {
        const i2c_master_event_callbacks_t callbacks = {.on_trans_done = transfer_done};
        err = i2c_master_register_event_callbacks(s_pca, &callbacks, NULL);
        if (err == ESP_OK) s_i2c_callbacks = true;
    }
    return err;
}
static esp_err_t open_bus(void) { return open_bus_mode(false); }
static const nn2_board_pca_io_t s_io = {
    .reset = chip_reset, .delay_us = chip_delay,
    .write = chip_write, .read = chip_read,
};
static esp_err_t prepare(const nn2_port_optical_config_t *config)
{
    s_hw_error = ESP_OK;
    esp_err_t err = open_bus();
    if (err != ESP_OK) return err;
    const nn2_board_pca_io_t io = {
        .reset = chip_reset, .delay_us = chip_delay,
        .write = chip_write, .read = chip_read,
    };
    if (nn2_board_pca_prepare(&s_chip, &io, config->current_ma) != NN2_OK)
        return s_hw_error != ESP_OK ? s_hw_error : ESP_ERR_INVALID_RESPONSE;
    s_chip.short_advisory = CONFIG_NN2_LED_SHORT_BENCH;
    s_chip.compact_off = true;
    /* Prepared and verified all-off. Recreate this exclusive bus with async
     * queueing only now; boot/configuration operations remain synchronous. */
    if (!still_armed()) return ESP_ERR_INVALID_STATE;
    if (!close_bus()) return ESP_FAIL;
    err = open_bus_mode(true);
    if (err != ESP_OK) return err;
    if (!s_timer) {
        const gptimer_config_t timer = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP,
            .resolution_hz = 1000000,
        };
        err = gptimer_new_timer(&timer, &s_timer);
        if (err != ESP_OK) return err;
        const gptimer_event_callbacks_t callbacks = {.on_alarm = alarm_callback};
        err = gptimer_register_event_callbacks(s_timer, &callbacks, NULL);
        if (err != ESP_OK) {
            /* Keep an unregistered timer out of future reuse. */
            if (gptimer_del_timer(s_timer) == ESP_OK) s_timer = NULL;
            return err;
        }
        s_timer_callbacks = true;
    }
    err = gptimer_enable(s_timer);
    if (err != ESP_OK) return err;
    s_timer_enabled = true;
    err = gptimer_set_raw_count(s_timer, 0);
    if (err != ESP_OK) return err;
    gptimer_alarm_config_t alarm = {
        .alarm_count = nn2_core_optical_half_slot_us(config->fs),
    };
    err = gptimer_set_alarm_action(s_timer, &alarm);
    if (err != ESP_OK) return err;
    /* Before publishing prepared=true: composition has not started the ADC. */
    uint32_t watermark=(uint32_t)uxTaskGetStackHighWaterMark(NULL);
    LED_ENTER();
    s_status.stack_free=watermark;
    s_status.pca = s_chip.diagnostics;
    s_status.last_complete = s_chip.last_complete;
    if (!s_status.first_stop && s_chip.fault_saved) s_status.first_fault = s_chip.first_fault;
    bool valid = s_armed && s_generation == s_worker_generation;
    if (valid) {
        s_half_us = nn2_core_optical_half_slot_us(config->fs);
        scheduler_reset_locked();
        s_phase = 0u; s_busy = false; s_prepared = true;
    }
    LED_EXIT();
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
/* Owner-only activation: no allocation, PCA initialization or bus recreation. */
static esp_err_t activate_prepared(void)
{
    LED_ENTER();
    bool valid = s_armed && s_prepared && s_generation == s_worker_generation;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (valid) {
#if CONFIG_NN2_OPTICAL_ACQUISITION
        s_clock_origin = (uint64_t)esp_timer_get_time();
#endif
        err = gptimer_start(s_timer);
        if (err == ESP_OK) {
            s_timer_started = true; s_prepared = false;
            s_status.state = NN2_OPTICAL_RUNNING;
        }
    }
    LED_EXIT();
    return err;
}

/* Priority-22 I2C owner: persistent payloads are prepared before ADC START.
 * Each driver call blocks on ISR completion; no frame/window work here. */
static void phase_work(uint32_t phase, uint64_t alarm)
{
    led_completion_t done={0};
    LED_ENTER();
    done.edge=s_edge;
    bool allowed=s_armed && s_generation==s_worker_generation;
    LED_EXIT();
    if (!allowed) return;
    if (gptimer_get_raw_count(s_timer,&done.begin)!=ESP_OK) { fault(ESP_FAIL); return; }
    if (done.begin>=alarm+s_half_us) {
        LED_ENTER();
        if (s_armed) {
            uint32_t dispatch=(uint32_t)(done.begin-alarm);
            if (dispatch>s_status.dispatch_max_us) s_status.dispatch_max_us=dispatch;
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE,ESP_ERR_TIMEOUT);
        }
        LED_EXIT();
        return;
    }
    uint64_t activity_begin=LED_TIMING_NOW();
    s_edge_commit_us=0u;
    done.result=(phase & 1u) ? nn2_board_pca_off(&s_chip) :
        nn2_board_pca_on(&s_chip,(uint8_t)(phase/2u));
    done.commit=s_edge_commit_us;
    /* Preserve the approved PCA fault policy. These additional reads are
     * absent from V4's minimal edge path, but still block rather than spin. */
    if (done.result==NN2_OK && phase==39u) done.result=nn2_board_pca_health(&s_chip);
    LED_TIMING_ACTIVITY(activity_begin);
    done.count_result=gptimer_get_raw_count(s_timer,&done.end);
    LED_ENTER();
    s_status.pca=s_chip.diagnostics;
    s_status.last_complete=s_chip.last_complete;
    if (!s_status.first_stop && s_chip.fault_saved) s_status.first_fault=s_chip.first_fault;
    LED_EXIT();
    scheduler_complete(&done);
}
/* Priority-21 scheduler owns completion acceptance and optical window handoff. */
static void scheduler_commit(const led_completion_t *done)
{
    uint64_t alarm=done->edge.alarm;
    uint32_t phase=done->edge.phase;
    LED_ENTER();
    bool valid=s_armed && done->edge.generation==s_generation;
    if (valid) {
        uint32_t dispatch=done->begin>alarm ? (uint32_t)(done->begin-alarm) : 0u;
        uint32_t elapsed=done->end>=done->begin ? (uint32_t)(done->end-done->begin) : 0u;
        uint32_t completion=done->end>alarm ? (uint32_t)(done->end-alarm) : 0u;
        if (dispatch>s_status.dispatch_max_us) s_status.dispatch_max_us=dispatch;
        if (elapsed>s_status.transfer_max_us) s_status.transfer_max_us=elapsed;
        if (completion>s_status.completion_max_us) s_status.completion_max_us=completion;
        if (done->count_result!=ESP_OK) stop_locked(STOP_TIMER,done->count_result);
        else if (done->end>=alarm+s_half_us) {
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE,ESP_ERR_TIMEOUT);
        } else if (done->result!=NN2_OK)
            stop_locked(STOP_IO,s_hw_error!=ESP_OK ? s_hw_error : ESP_ERR_INVALID_RESPONSE);
        valid=s_armed;
    }
    LED_EXIT();
#if CONFIG_NN2_OPTICAL_ACQUISITION
    if (valid) {
        uint64_t frame_origin=s_clock_origin+alarm-(uint64_t)phase*s_half_us;
        nn2_core_acq_window_t window={
            .commit_us=done->commit,
            .end_us=s_clock_origin+alarm+s_half_us-NN2_ACQ_GUARD_US,
            .origin_us=frame_origin,
            .slot_start_us=frame_origin+(uint64_t)(phase/2u)*2u*s_half_us,
            .frame=done->edge.frame,.phase=(uint8_t)phase,
        };
        if (!still_armed()) return; /* STOP owns any overtaken completion. */
        if (!done->commit || !nn2_acquisition_window(&window)) {
            fault(ESP_ERR_INVALID_STATE); return;
        }
    }
#endif
    LED_ENTER();
    if (s_armed && done->edge.generation==s_generation) {
        ++s_status.edges;
        if (phase==39u) ++s_status.frames;
        s_busy=false;
    }
    LED_EXIT();
}
static void owner_step(void)
{
        LED_ENTER();
        bool start = s_start_pending && s_armed;
        nn2_port_optical_config_t config = s_config;
        if (start) {
            s_start_pending = false;
            s_worker_generation = s_generation;
        }
        bool activate = s_activate_pending && s_armed && s_prepared;
        if (activate) s_activate_pending = false;
        bool deferred = s_defer_activation;
        bool armed = s_armed;
        bool work = s_edge_pending && s_status.state == NN2_OPTICAL_RUNNING;
        if (work) s_edge_pending=false;
        uint32_t phase = s_phase;
        uint64_t alarm = s_alarm;
        LED_EXIT();
        if (start) {
            esp_err_t err = prepare(&config);
            if (err == ESP_OK && !deferred) err = activate_prepared();
            if (err != ESP_OK && still_armed()) fault(err);
        } else if (activate) {
            esp_err_t err = activate_prepared();
            if (err != ESP_OK && still_armed()) fault(err);
        } else if (armed && work) {
            phase_work(phase, alarm);
        }
        LED_ENTER();
        bool needs_cleanup = !s_armed && !s_status.bus_released;
        LED_EXIT();
        if (needs_cleanup) cleanup();
        /* No stack scans, formatting or per-edge allocation in this loop. */
}
static void owner_task(void *ctx)
{
    (void)ctx;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        owner_step();
    }
}
static nn2_status_t request_start(const nn2_port_optical_config_t *config, bool deferred)
{
    if (!config) return NN2_ERR_INVALID_ARG;
#if CONFIG_NN2_OPTICAL_ACQUISITION
    if (!nn2_core_acquisition_fs_valid(config->fs)) return NN2_ERR_INVALID_ARG;
#else
    if (!nn2_core_optical_fs_valid(config->fs)) return NN2_ERR_INVALID_ARG;
#endif
    for (unsigned i = 0; i < 20; ++i)
        if (config->current_ma[i] > NN2_OPTICAL_BRINGUP_MA) return NN2_ERR_INVALID_ARG;
    if (CONFIG_NN2_LED_SHORT_BENCH) {
        for (unsigned i = 0; i < 20; ++i)
            if (config->current_ma[i] != 30) return NN2_ERR_INVALID_ARG;
    }
    LED_ENTER();
    bool ok = s_task && s_status.state == NN2_OPTICAL_IDLE && s_status.bus_released;
    if (ok) {
        s_config = *config;
        ++s_generation;
        LED_TIMING_RESET();
        s_armed = s_start_pending = true;
        s_defer_activation = deferred; s_prepared = s_activate_pending = false;
        s_status = (nn2_led_bringup_status_t){.state = NN2_OPTICAL_STARTING, .attempt_used = true};
    }
    LED_EXIT();
    if (!ok) return NN2_ERR_INVALID_STATE;
    xTaskNotifyGive(s_task);
    return NN2_OK;
}
static nn2_status_t start(void *ctx, const nn2_port_optical_config_t *config)
{ (void)ctx; return request_start(config, false); }
nn2_status_t nn2_led_bringup_prepare(const nn2_port_optical_config_t *config)
{ return request_start(config, true); }
bool nn2_led_bringup_prepared(void)
{
    LED_ENTER();
    bool ready = s_armed && s_prepared && s_generation == s_worker_generation;
    LED_EXIT();
    return ready;
}
nn2_status_t nn2_led_bringup_activate(void)
{
    LED_ENTER();
    bool ready = s_armed && s_prepared && !s_activate_pending && s_defer_activation &&
        s_generation == s_worker_generation;
    if (ready) s_activate_pending = true;
    LED_EXIT();
    if (ready) xTaskNotifyGive(s_task);
    return ready ? NN2_OK : NN2_ERR_INVALID_STATE;
}
void nn2_led_bringup_report_timing(void)
{
#if defined(ESP_PLATFORM) && CONFIG_NN2_OPTICAL_ACQUISITION && NN2_RTOS_LED_TIMING_DIAGNOSTICS
    portENTER_CRITICAL(&s_lock);
    bool report=s_timing.frozen && !s_timing.reported && s_timing.generation!=0u;
    uint32_t head=s_timing.head, generation=s_timing.generation;
    led_timing_record_t longest=s_timing.longest;
    if (report) s_timing.reported=true;
    portEXIT_CRITICAL(&s_lock);
    if (!report) return;
    uint32_t count=head<16u ? head : 16u;
    ESP_LOGI("led_timing_summary", "generation=%lu recorded=%lu printed=%lu max_hold_us=%llu max_hold_begin_us=%llu max_hold_end_us=%llu max_hold_line=%u",
        (unsigned long)generation,(unsigned long)head,(unsigned long)count,
        (unsigned long long)(longest.end_us-longest.begin_us),
        (unsigned long long)longest.begin_us,(unsigned long long)longest.end_us,(unsigned)longest.line);
    for (uint32_t i=0u;i<count;++i) {
        portENTER_CRITICAL(&s_lock);
        led_timing_record_t r=s_timing.records[(head-count+i)%64u];
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI("led_timing", "kind=%u begin_us=%llu end_us=%llu elapsed_us=%llu line=%u phase=%u frame=%lu",
            (unsigned)r.kind,(unsigned long long)r.begin_us,(unsigned long long)r.end_us,
            (unsigned long long)(r.end_us-r.begin_us),(unsigned)r.line,(unsigned)r.phase,(unsigned long)r.frame);
    }
#endif
}
nn2_led_bringup_status_t nn2_led_bringup_status(void)
{
    LED_ENTER();
    nn2_led_bringup_status_t status = s_status;
    LED_EXIT();
    return status;
}
static uint8_t state(void *ctx)
{
    (void)ctx;
    return nn2_led_bringup_status().state;
}
static nn2_status_t clear_fault(void *ctx)
{
    (void)ctx;
    LED_ENTER();
    bool ok = s_status.bus_released && !s_armed;
    if (ok) {
        s_status.state = NN2_OPTICAL_IDLE;
        /* Retain first terminal error and evidence across clear-fault. */
    }
    LED_EXIT();
    return ok ? NN2_OK : NN2_ERR_INVALID_STATE;
}
esp_err_t nn2_led_bringup_init(void)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nn2_board_hold_emitters_off();
    if (err != ESP_OK) return err;
    /* Before task creation: one synchronous safe-off/readback attempt on every
     * LED-profile boot. Release the bus before publishing IDLE. */
    s_status = (nn2_led_bringup_status_t){.state = NN2_OPTICAL_STARTING};
    s_armed = true;
    s_worker_generation = ++s_generation;
    s_hw_error = ESP_OK;
    err = open_bus();
    if (err == ESP_OK && nn2_board_pca_safe_boot(&s_io) != NN2_OK)
        err = s_hw_error != ESP_OK ? s_hw_error : ESP_ERR_INVALID_RESPONSE;
    s_armed = false;
    if (err != ESP_OK) fault(err);
    cleanup();
    if (err != ESP_OK || !s_status.bus_released) return err != ESP_OK ? err : ESP_FAIL;
    if (!s_scheduler_task && xTaskCreatePinnedToCore(scheduler_task,"nn2_sched",
        NN2_RTOS_SCHED_STACK_BYTES,NULL,NN2_RTOS_SCHED_PRIORITY,
        &s_scheduler_task,NN2_RTOS_SCHED_CORE)!=pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(owner_task,"nn2_led",NN2_RTOS_LED_STACK_BYTES,
        NULL,NN2_RTOS_LED_PRIORITY,&s_task,NN2_RTOS_LED_CORE)!=pdPASS) return ESP_ERR_NO_MEM;
    printf("nn2_v4_pipeline: led=%u/core%d sched=%u/core%d adc=%u/core%d consumer=%u/core%d\n",
        (unsigned)NN2_RTOS_LED_PRIORITY,NN2_RTOS_LED_CORE,
        (unsigned)NN2_RTOS_SCHED_PRIORITY,NN2_RTOS_SCHED_CORE,
        (unsigned)NN2_RTOS_ADS131_PRIORITY,NN2_RTOS_ADS131_CORE,
        (unsigned)NN2_RTOS_ADS131_CONSUMER_PRIORITY,NN2_RTOS_ADS131_CONSUMER_CORE);
    return ESP_OK;
}
nn2_port_optical_t nn2_led_bringup_port(void)
{
    return (nn2_port_optical_t){.start = start, .state = state,
        .clear_fault = clear_fault, .current_limit_ma = NN2_OPTICAL_BRINGUP_MA,
        .policy_info = CONFIG_NN2_LED_SHORT_BENCH ? s_advisory_info : s_strict_info};
}
nn2_port_safety_t nn2_led_bringup_safety_port(void)
{
    return (nn2_port_safety_t){.hold_off = cancel};
}

bool nn2_led_bringup_format_status(char *out, size_t capacity,
    const nn2_led_bringup_status_t *led)
{
    if (!out || !capacity || !led) return false;
    int count = snprintf(out, capacity,
        " led_state=%u led_frames=%" PRIu32 " led_edges=%" PRIu32
        " led_missed=%" PRIu32 " led_max_us=%" PRIu32 " led_error=%" PRId32
        " led_stack=%" PRIu32 " led_bus_free=%u led_mode2=%02X"
        " led_eflags=%02X%02X%02X%02X%02X%02X led_ef_valid=%06" PRIX32
        " led_open=%06" PRIX32 " led_short=%06" PRIX32 " led_ef_invalid=%06" PRIX32
        " short_policy=%s limit_s=%u single_run=%u led_attempt=%u led_cap=%u led_stop=%u"
        " led_epoch=%" PRIu32 " led_complete=%" PRIu32 " led_fault_epoch=%" PRIu32
        " led_warn=%" PRIu32 " led_dispatch_us=%" PRIu32 " led_completion_us=%" PRIu32
        " led_i2c_tx=%" PRIu32 " led_i2c_done=%" PRIu32
        " led_i2c_err=%" PRIu32 " led_i2c_stale=%" PRIu32 " led_i2c_event=%u led_reboot=%u\n",
        (unsigned)led->state, led->frames, led->edges, led->deadline_misses,
        led->transfer_max_us, led->error, led->stack_free, (unsigned)led->bus_released,
        (unsigned)led->pca_mode2,
        (unsigned)led->pca.flags[0], (unsigned)led->pca.flags[1], (unsigned)led->pca.flags[2],
        (unsigned)led->pca.flags[3], (unsigned)led->pca.flags[4], (unsigned)led->pca.flags[5],
        led->pca.valid_channels, led->pca.open_channels, led->pca.short_channels,
        led->pca.invalid_channels,
        CONFIG_NN2_LED_SHORT_BENCH ? "BENCH_WARN" : "STRICT",
        0u, 0u,
        (unsigned)led->attempt_used, (unsigned)led->limit_hit, (unsigned)led->first_stop,
        led->pca.epoch, led->last_complete.epoch, led->first_fault.epoch,
        led->pca.warning_frames, led->dispatch_max_us, led->completion_max_us,
        led->i2c_submitted, led->i2c_completed, led->i2c_errors, led->i2c_stale, (unsigned)led->i2c_event, (unsigned)led->reboot_required);
    return count >= 0 && (size_t)count < capacity;
}
