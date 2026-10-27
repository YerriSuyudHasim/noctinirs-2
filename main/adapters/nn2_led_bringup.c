/* Opt-in LED-only owner. No ADC, no streaming samples, no I2C from ISR. */
#include "nn2_led_bringup.h"
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

#if !CONFIG_GPIO_CTRL_FUNC_IN_IRAM || !CONFIG_GPTIMER_ISR_CACHE_SAFE || !CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM
#error "LED bring-up requires GPIO/GPTimer IRAM/cache-safe configuration"
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
#define SHORT_CAP_US UINT64_C(2000000)
enum { STOP_NONE, STOP_CANCEL, STOP_CAP, STOP_DEADLINE, STOP_TIMER, STOP_IO, STOP_CLEANUP };
/* Boot lifetime: never cleared by STOP, fault clear, cleanup or START. */
static bool s_attempt_used;
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
    if (cause == STOP_CAP) s_status.limit_hit = true;
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
        else if (CONFIG_NN2_LED_SHORT_BENCH && now >= SHORT_CAP_US)
            stop_locked(STOP_CAP, ESP_ERR_TIMEOUT);
        else if (s_busy || now >= event->alarm_value + s_half_us) {
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE, ESP_ERR_TIMEOUT);
        } else {
            s_busy = true;
            s_alarm = event->alarm_value;
            uint64_t next = event->alarm_value + s_half_us;
            if (CONFIG_NN2_LED_SHORT_BENCH && next > SHORT_CAP_US) next = SHORT_CAP_US;
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
        else if (now >= SHORT_CAP_US) stop_locked(STOP_CAP, ESP_ERR_TIMEOUT);
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
    esp_err_t err = i2c_master_transmit(s_pca, data, n, NN2_RTOS_LED_I2C_TIMEOUT_MS);
    if (err != ESP_OK) s_hw_error = err;
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}
static nn2_status_t chip_read(void *ctx, uint8_t reg, uint8_t *data, size_t n)
{
    (void)ctx;
    if (!still_armed()) return NN2_ERR_INVALID_STATE;
    esp_err_t err = i2c_master_transmit_receive(s_pca, &reg, 1, data, n,
        NN2_RTOS_LED_I2C_TIMEOUT_MS);
    if (err != ESP_OK) s_hw_error = err;
    if (err == ESP_OK && reg == 1 && n == 1) {
        portENTER_CRITICAL(&s_lock);
        s_status.pca_mode2 = data[0];
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
    if (s_pca) {
        if (i2c_master_bus_rm_device(s_pca) == ESP_OK) s_pca = NULL;
        else ok = false;
    }
    if (s_bus && !s_pca) {
        if (i2c_del_master_bus(s_bus) == ESP_OK) s_bus = NULL;
        else ok = false;
    }
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

static esp_err_t open_bus(void)
{
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = NN2_BOARD_SDA_GPIO,
        .scl_io_num = NN2_BOARD_SCL_GPIO, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .trans_queue_depth = 0,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &s_bus);
    if (err != ESP_OK) return err;
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = NN2_BOARD_PCA_ADDRESS,
        .scl_speed_hz = 100000, .scl_wait_us = 1000,
    };
    err = i2c_master_bus_add_device(s_bus, &device, &s_pca);
    return err;
}
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
    } else if (valid && CONFIG_NN2_LED_SHORT_BENCH && after >= SHORT_CAP_US) {
        stop_locked(STOP_CAP, ESP_ERR_TIMEOUT);
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
    if (!config || !nn2_core_optical_fs_valid(config->fs)) return NN2_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 20; ++i)
        if (config->current_ma[i] > NN2_OPTICAL_BRINGUP_MA) return NN2_ERR_INVALID_ARG;
    if (CONFIG_NN2_LED_SHORT_BENCH) {
        if (config->fs != 5) return NN2_ERR_INVALID_ARG;
        for (unsigned i = 0; i < 20; ++i)
            if (config->current_ma[i] != 30) return NN2_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    bool ok = (!CONFIG_NN2_LED_SHORT_BENCH || !s_attempt_used) && s_task && s_status.state == NN2_OPTICAL_IDLE && s_status.bus_released;
    if (ok) {
        s_attempt_used = true;
        s_config = *config;
        ++s_generation;
        s_armed = s_start_pending = true;
        s_status = (nn2_led_bringup_status_t){.state = NN2_OPTICAL_STARTING, .attempt_used = s_attempt_used};
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
        .policy_info = CONFIG_NN2_LED_SHORT_BENCH ?
        "NoctiNIRS2/0.1.0;wire=ADR0004/1;SIMULATED;led=REAL_BRINGUP;short_policy=BENCH_WARN;limit_s=2;single_run=1;frames=unsupported;capture=unsupported" : NULL};
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
        " led_warn=%" PRIu32 " led_dispatch_us=%" PRIu32 " led_completion_us=%" PRIu32 "\n",
        (unsigned)led->state, led->frames, led->edges, led->deadline_misses,
        led->transfer_max_us, led->error, led->stack_free, (unsigned)led->bus_released,
        (unsigned)led->pca_mode2,
        (unsigned)led->pca.flags[0], (unsigned)led->pca.flags[1], (unsigned)led->pca.flags[2],
        (unsigned)led->pca.flags[3], (unsigned)led->pca.flags[4], (unsigned)led->pca.flags[5],
        led->pca.valid_channels, led->pca.open_channels, led->pca.short_channels,
        led->pca.invalid_channels,
        CONFIG_NN2_LED_SHORT_BENCH ? "BENCH_WARN" : "STRICT",
        CONFIG_NN2_LED_SHORT_BENCH ? 2u : 0u, CONFIG_NN2_LED_SHORT_BENCH ? 1u : 0u,
        (unsigned)led->attempt_used, (unsigned)led->limit_hit, (unsigned)led->first_stop,
        led->pca.epoch, led->last_complete.epoch, led->first_fault.epoch,
        led->pca.warning_frames, led->dispatch_max_us, led->completion_max_us);
    return count >= 0 && (size_t)count < capacity;
}
