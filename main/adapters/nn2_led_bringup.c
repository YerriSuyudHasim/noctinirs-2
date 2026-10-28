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
    s_armed = false;
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

static bool IRAM_ATTR transfer_done(i2c_master_dev_handle_t dev,
    const i2c_master_event_data_t *event, void *ctx)
{
    (void)dev; (void)ctx;
    portENTER_CRITICAL_ISR(&s_lock);
    if (s_pending) {
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
    portEXIT_CRITICAL_ISR(&s_lock);
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
        portENTER_CRITICAL(&s_lock);
        s_pending = false;
        portEXIT_CRITICAL(&s_lock);
        return submitted;
    }
    TickType_t began = xTaskGetTickCount();
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        bool done = s_done;
        bool valid = s_armed && s_io_generation == s_generation;
        esp_err_t result = s_io_result;
        portEXIT_CRITICAL(&s_lock);
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
    portENTER_CRITICAL(&s_lock);
    bool valid = s_armed && s_generation == s_worker_generation && !s_pending;
    if (valid) {
        s_pending = true;
        s_done = false;
        s_io_result = ESP_FAIL;
        s_io_generation = s_generation;
        ++s_status.i2c_submitted;
    }
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

static nn2_status_t cancel(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_lock);
    if (s_armed && !s_status.first_stop) s_status.first_stop = STOP_CANCEL;
    s_armed = false;
    s_start_pending = false;
    ++s_generation;
    if (s_status.state != NN2_OPTICAL_IDLE && s_status.state != NN2_OPTICAL_FAULT)
        s_status.state = NN2_OPTICAL_STOPPING;
    esp_err_t err = nn2_board_hold_emitters_off();
    portEXIT_CRITICAL(&s_lock);
    if (s_task) xTaskNotifyGive(s_task);
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

/* ISR can only cancel, never release reset. All referenced data is internal RAM.
 * The GPIO function and timer control function must be linked into IRAM. */
static bool IRAM_ATTR alarm_callback(gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *event, void *ctx)
{
    (void)ctx;
    bool notify = false;
    uint64_t now = 0;
    esp_err_t count_result = gptimer_get_raw_count(timer, &now);
    portENTER_CRITICAL_ISR(&s_lock);
    if (s_armed && s_status.state == NN2_OPTICAL_RUNNING) {
        notify = true;
        if (count_result == ESP_OK && now >= event->alarm_value) {
            uint64_t late = now - event->alarm_value;
            uint32_t dispatch = late > UINT32_MAX ? UINT32_MAX : (uint32_t)late;
            if (dispatch > s_status.dispatch_max_us) s_status.dispatch_max_us = dispatch;
        }
        if (count_result != ESP_OK) stop_locked(STOP_TIMER, count_result);
        else if (s_busy || now >= event->alarm_value + s_half_us) {
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE, ESP_ERR_TIMEOUT);
        } else {
            s_busy = true;
            s_alarm = event->alarm_value;
            uint64_t next = event->alarm_value + s_half_us;
            gptimer_alarm_config_t alarm = {.alarm_count = next};
            /* Cancellation cannot interleave between permission and rearm. */
            if (gptimer_set_alarm_action(timer, &alarm) != ESP_OK)
                stop_locked(STOP_TIMER, ESP_FAIL);
        }
    }
    portEXIT_CRITICAL_ISR(&s_lock);
    BaseType_t wake = pdFALSE;
    if (notify) vTaskNotifyGiveFromISR(s_task, &wake);
    return wake == pdTRUE;
}

static bool still_armed(void)
{
    portENTER_CRITICAL(&s_lock);
    bool ok = s_armed && s_generation == s_worker_generation;
    if (ok && CONFIG_NN2_LED_SHORT_BENCH && s_timer_started) {
        uint64_t now = 0;
        if (gptimer_get_raw_count(s_timer, &now) != ESP_OK) stop_locked(STOP_TIMER, ESP_FAIL);
        ok = s_armed;
    }
    portEXIT_CRITICAL(&s_lock);
    return ok;
}
static nn2_status_t chip_reset(void *ctx, bool asserted)
{
    (void)ctx;
    portENTER_CRITICAL(&s_lock);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (asserted) result = nn2_board_hold_emitters_off();
    else if (s_armed && s_generation == s_worker_generation)
        result = nn2_board_release_emitters_reset();
    portEXIT_CRITICAL(&s_lock);
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
        portENTER_CRITICAL(&s_lock);
        s_status.pca_mode2 = data[n - 1];
        portEXIT_CRITICAL(&s_lock);
    }
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

static void fault(esp_err_t error)
{
    portENTER_CRITICAL(&s_lock);
    stop_locked(STOP_IO, error);
    portEXIT_CRITICAL(&s_lock);
}
static bool close_bus(void)
{
    /* Reset has already been asserted by the caller. A failed drain keeps
     * handles, callback context and buffers alive, and denies the next START. */
    if (s_async && s_bus) {
        esp_err_t drained = i2c_master_bus_wait_all_done(s_bus, NN2_RTOS_LED_I2C_TIMEOUT_MS);
        portENTER_CRITICAL(&s_lock);
        bool pending = s_pending;
        /* Pinned IDF6.1 NACK/TIMEOUT can finish internally without calling
         * on_trans_done. wait_all_done does not prove the direct transaction's
         * lifetime ended: it only drains queued records. Retain everything.
         * No callback + no public quiescence proof => MCU reboot required. */
        if (pending) s_status.reboot_required = true;
        portEXIT_CRITICAL(&s_lock);
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
    portENTER_CRITICAL(&s_lock);
    /* A late real completion followed by successful drain/detach is proof.
     * Otherwise this flag cannot clear via CLEAR_FAULT or another START. */
    s_status.reboot_required = false;
    portEXIT_CRITICAL(&s_lock);
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
    portENTER_CRITICAL(&s_lock);
    s_busy = false;
    s_status.bus_released = ok && !s_bus && !s_pca && !s_timer_enabled && !s_timer_started &&
        (!s_timer || s_timer_callbacks);
    if (!s_status.bus_released) {
        s_status.state = NN2_OPTICAL_FAULT;
        if (!s_status.first_stop) { s_status.first_stop = STOP_CLEANUP; s_status.error = ESP_FAIL; }
    } else if (s_status.state != NN2_OPTICAL_FAULT) {
        s_status.state = NN2_OPTICAL_IDLE;
    }
    portEXIT_CRITICAL(&s_lock);
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
    portENTER_CRITICAL(&s_lock);
    s_status.pca = s_chip.diagnostics;
    s_status.last_complete = s_chip.last_complete;
    if (!s_status.first_stop && s_chip.fault_saved)
        s_status.first_fault = s_chip.first_fault;
    bool valid = s_armed && s_generation == s_worker_generation;
    if (valid) {
        s_half_us = nn2_core_optical_half_slot_us(config->fs);
        s_phase = 0;
        s_busy = false;
        s_status.state = NN2_OPTICAL_RUNNING;
    }
    if (valid) {
        #if CONFIG_NN2_OPTICAL_ACQUISITION
        s_clock_origin = (uint64_t)esp_timer_get_time();
#endif
        err = gptimer_start(s_timer);
        if (err == ESP_OK) s_timer_started = true;
    } else err = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL(&s_lock);
    return err;
}

static void phase_work(uint32_t phase, uint64_t alarm)
{
    uint64_t before = 0, after = 0;
    if (!still_armed()) return;
    if (gptimer_get_raw_count(s_timer, &before) != ESP_OK) { fault(ESP_FAIL); return; }
    portENTER_CRITICAL(&s_lock);
    uint32_t dispatch = before > alarm ? (uint32_t)(before - alarm) : 0;
    if (dispatch > s_status.dispatch_max_us) s_status.dispatch_max_us = dispatch;
    if (before >= alarm + s_half_us) {
        ++s_status.deadline_misses;
        stop_locked(STOP_DEADLINE, ESP_ERR_TIMEOUT);
    }
    bool allowed = s_armed && s_generation == s_worker_generation;
    portEXIT_CRITICAL(&s_lock);
    if (!allowed) return;
    nn2_status_t result = (phase & 1u) ? nn2_board_pca_off(&s_chip) :
        nn2_board_pca_on(&s_chip, (uint8_t)(phase / 2));
    if (result == NN2_OK && phase == 39) result = nn2_board_pca_health(&s_chip);
#if CONFIG_NN2_OPTICAL_ACQUISITION
    uint64_t commit = (uint64_t)esp_timer_get_time();
    uint64_t frame_origin = s_clock_origin + alarm - (uint64_t)phase * s_half_us;
    nn2_core_acq_window_t window = {
        .commit_us = commit,
        .end_us = s_clock_origin + alarm + s_half_us - NN2_ACQ_GUARD_US,
        .origin_us = frame_origin,
        .slot_start_us = frame_origin + (uint64_t)(phase / 2u) * 2u * s_half_us,
        .frame = s_status.frames,
        .phase = (uint8_t)phase,
    };
    if (result == NN2_OK && still_armed() && !nn2_acquisition_window(&window)) {
        fault(ESP_ERR_NO_MEM); return;
    }
#endif
    esp_err_t count_result = gptimer_get_raw_count(s_timer, &after);
    portENTER_CRITICAL(&s_lock);
    s_status.pca = s_chip.diagnostics;
    s_status.last_complete = s_chip.last_complete;
    if (!s_status.first_stop && s_chip.fault_saved)
        s_status.first_fault = s_chip.first_fault;
    bool valid = s_armed && s_generation == s_worker_generation;
    uint32_t elapsed = count_result == ESP_OK ? (uint32_t)(after - before) : 0;
    uint32_t completion = count_result == ESP_OK && after > alarm ? (uint32_t)(after - alarm) : 0;
    if (elapsed > s_status.transfer_max_us) s_status.transfer_max_us = elapsed;
    if (completion > s_status.completion_max_us) s_status.completion_max_us = completion;
    if (valid && count_result != ESP_OK) {
        stop_locked(STOP_TIMER, count_result);
    } else if (valid && result == NN2_OK && after < alarm + s_half_us) {
        ++s_status.edges;
        if (phase == 39) ++s_status.frames;
        s_phase = (phase + 1) % 40u;
        s_busy = false;
    } else if (valid) {
        if (after >= alarm + s_half_us) {
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE, ESP_ERR_TIMEOUT);
        } else stop_locked(STOP_IO,
            s_hw_error != ESP_OK ? s_hw_error : ESP_ERR_INVALID_RESPONSE);
    }
    portEXIT_CRITICAL(&s_lock);
}
static void owner_step(void)
{
        portENTER_CRITICAL(&s_lock);
        bool start = s_start_pending && s_armed;
        nn2_port_optical_config_t config = s_config;
        if (start) {
            s_start_pending = false;
            s_worker_generation = s_generation;
        }
        bool armed = s_armed;
        bool work = s_busy && s_status.state == NN2_OPTICAL_RUNNING;
        uint32_t phase = s_phase;
        uint64_t alarm = s_alarm;
        portEXIT_CRITICAL(&s_lock);
        if (start) {
            esp_err_t err = prepare(&config);
            if (err != ESP_OK && still_armed()) fault(err);
        } else if (armed && work) {
            phase_work(phase, alarm);
        }
        portENTER_CRITICAL(&s_lock);
        bool needs_cleanup = !s_armed && !s_status.bus_released;
        portEXIT_CRITICAL(&s_lock);
        if (needs_cleanup) cleanup();
        uint32_t watermark = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        portENTER_CRITICAL(&s_lock);
        s_status.stack_free = watermark;
        portEXIT_CRITICAL(&s_lock);
}
static void owner_task(void *ctx)
{
    (void)ctx;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        owner_step();
    }
}
static nn2_status_t start(void *ctx, const nn2_port_optical_config_t *config)
{
    (void)ctx;
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
    portENTER_CRITICAL(&s_lock);
    bool ok = s_task && s_status.state == NN2_OPTICAL_IDLE && s_status.bus_released;
    if (ok) {
        s_config = *config;
        ++s_generation;
        s_armed = s_start_pending = true;
        s_status = (nn2_led_bringup_status_t){.state = NN2_OPTICAL_STARTING, .attempt_used = true};
    }
    portEXIT_CRITICAL(&s_lock);
    if (!ok) return NN2_ERR_INVALID_STATE;
    xTaskNotifyGive(s_task);
    return NN2_OK;
}
nn2_led_bringup_status_t nn2_led_bringup_status(void)
{
    portENTER_CRITICAL(&s_lock);
    nn2_led_bringup_status_t status = s_status;
    portEXIT_CRITICAL(&s_lock);
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
    portENTER_CRITICAL(&s_lock);
    bool ok = s_status.bus_released && !s_armed;
    if (ok) {
        s_status.state = NN2_OPTICAL_IDLE;
        /* Retain first terminal error and evidence across clear-fault. */
    }
    portEXIT_CRITICAL(&s_lock);
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
    return xTaskCreatePinnedToCore(owner_task, "nn2_led", NN2_RTOS_LED_STACK_BYTES,
        NULL, NN2_RTOS_LED_PRIORITY, &s_task, NN2_RTOS_LED_CORE) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
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
