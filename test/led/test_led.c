/* Runs production owner steps and ISR callback with explicit fake peripherals.
 * This is deterministic fault injection, not RTOS or electrical timing evidence. */
#include <stdio.h>
#include <string.h>
#include "nn2_app_comm.h"
#include "../../main/adapters/nn2_led_bringup.c"
static unsigned checks, failures;
static i2c_master_callback_t fake_callback;
static void *fake_callback_ctx;
static bool defer_completion, drain_fails, callback_fails, suppress_completion;
static bool cancel_on_wait, deadline_on_wait;
static i2c_master_event_t fake_event = I2C_EVENT_DONE;
static TickType_t fake_ticks;
static void finish_transfer(void) {
    const i2c_master_event_data_t event = {.event = fake_event};
    if (fake_callback) (void)fake_callback(s_pca, &event, fake_callback_ctx);
}
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %u: %s\n", __LINE__, #x); } } while (0)
static uint8_t regs[0x47];
static bool reset_low = true, occupied, remove_fails;
static bool timer_start_fails, timer_alarm_fails, timer_count_fails, timer_stop_fails;
static unsigned cancel_io;
static unsigned timer_fail;
static bool register_fails;
static bool cancel_at_write, corrupt_read;
static bool corrupt_current_read;
static unsigned io_count, fail_at;
static uint32_t io_cost = 100;
static uint64_t clock_count, next_alarm;
static void por(void) {
    memset(regs, 0, sizeof(regs));
    regs[1] = 0x05; /* MODE2 read-only reserved bits, per V2 and datasheet. */
    for (unsigned i = 2; i < 8; ++i) regs[i] = 0xaa;
}
esp_err_t gpio_set_level(int pin, uint32_t level) {
    CHECK(pin == 18);
    reset_low = level == 0;
    if (reset_low) por();
    return ESP_OK;
}
esp_err_t nn2_board_hold_emitters_off(void) { return gpio_set_level(18, 0); }
esp_err_t nn2_board_release_emitters_reset(void) {
    return gpio_set_level(18, 1);
}
void esp_rom_delay_us(uint32_t us) { (void)us; }
void xTaskNotifyGive(TaskHandle_t task) { CHECK(task != NULL); }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *wake) { CHECK(task != NULL); *wake = pdTRUE; }
TickType_t xTaskGetTickCount(void) { return fake_ticks; }
uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t wait) {
    (void)clear;
    if (cancel_on_wait) { cancel_on_wait = false; (void)cancel(NULL); }
    if (deadline_on_wait) {
        deadline_on_wait = false;
        clock_count = next_alarm;
        const gptimer_alarm_event_data_t event = {.count_value = clock_count, .alarm_value = next_alarm};
        (void)alarm_callback(s_timer, &event, NULL);
    }
    if (!suppress_completion && s_pending) finish_transfer();
    fake_ticks += wait;
    return 1;
}
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 4096; }
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
    uint32_t stack, void *ctx, unsigned priority, TaskHandle_t *task, int core) {
    (void)entry; (void)ctx;
    CHECK(!strcmp(name, "nn2_led") && stack == 6144 && priority == 22 && core == 1);
    *task = &s_task; return pdPASS;
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c, i2c_master_bus_handle_t *out) {
    CHECK(c->i2c_port == 0 && c->sda_io_num == 15 && c->scl_io_num == 16 && c->trans_queue_depth <= 1);
    if (occupied) return ESP_FAIL;
    occupied = true; *out = &s_bus; return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *c, i2c_master_dev_handle_t *out) {
    CHECK(bus != NULL && c->device_address == 1 && c->scl_speed_hz == (uint32_t)(s_async ? CONFIG_NN2_LED_RUNTIME_I2C_HZ : 100000));
    *out = &s_pca; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device) { (void)device; return remove_fails ? ESP_FAIL : ESP_OK; }
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) { (void)bus; occupied = false; return ESP_OK; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t n, int timeout) {
    CHECK(device != NULL && timeout == 20 && n >= 2);
    clock_count += io_cost;
    ++io_count;
    if (cancel_io && io_count == cancel_io) { (void)cancel(NULL); return ESP_FAIL; }
    if (io_count == fail_at || reset_low) return ESP_FAIL;
    if (fake_callback) CHECK(data == s_tx); /* Driver never retains a stack pointer. */
    if (cancel_at_write) { cancel_at_write = false; (void)cancel(NULL); return ESP_FAIL; }
    uint8_t address = data[0] & 0x7f;
    if (s_status.state == NN2_OPTICAL_RUNNING && address >= 2 && address <= 7)
        CHECK((n == 7 && data[0] == 0x82) ||
              (n == 2 && data[1] == 0 && address == 2 + s_chip.selected / 4));
    if (address == 0x40) memset(regs + 0x22, data[1], 24);
    else if (address == 0x3f) memset(regs + 0x0a, data[1], 24);
    else if (address == 1) regs[1] = (uint8_t)((regs[1] & 0xc7) | (data[1] & 0x28));
    else for (size_t i = 1; i < n; ++i) regs[address + (data[0] & 0x80 ? i - 1 : 0)] = data[i];
    if (fake_callback && !defer_completion && !suppress_completion) finish_transfer();
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t tn, uint8_t *rx, size_t rn, int timeout) {
    CHECK(device != NULL && tn == 1 && timeout == 20);
    clock_count += io_cost;
    ++io_count;
    if (cancel_io && io_count == cancel_io) { (void)cancel(NULL); return ESP_FAIL; }
    if (io_count == fail_at || reset_low) return ESP_FAIL;
    if (fake_callback) CHECK(tx == s_tx && rx == s_rx);
    memcpy(rx, regs + (*tx & 0x7f), rn);
    if (corrupt_read) rx[0] ^= 1;
    if (corrupt_current_read && rn == 24 && regs[0x22]) rx[0] ^= 1;
    if (fake_callback && !defer_completion && !suppress_completion) finish_transfer();
    return ESP_OK;
}
esp_err_t i2c_master_register_event_callbacks(i2c_master_dev_handle_t d,
    const i2c_master_event_callbacks_t *callbacks, void *ctx) {
    CHECK(d != NULL);
    if (callback_fails) return ESP_FAIL;
    fake_callback = callbacks->on_trans_done;
    fake_callback_ctx = ctx;
    return ESP_OK;
}
esp_err_t i2c_master_bus_wait_all_done(i2c_master_bus_handle_t b, int timeout) {
    CHECK(b != NULL && timeout == 20);
    return drain_fails ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t gptimer_new_timer(const gptimer_config_t *c, gptimer_handle_t *out) { CHECK(c->resolution_hz == 1000000); *out = &s_timer; return timer_fail == 1 ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t, const gptimer_event_callbacks_t *c, void *ctx) { (void)t; (void)ctx; CHECK(c->on_alarm == alarm_callback); return timer_fail == 2 || register_fails ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_del_timer(gptimer_handle_t t) { (void)t; return timer_fail == 3 ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_enable(gptimer_handle_t t) { (void)t; return timer_fail == 4 ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_disable(gptimer_handle_t t) { (void)t; return timer_fail == 5 ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_start(gptimer_handle_t t) { (void)t; return timer_start_fails ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_stop(gptimer_handle_t t) { (void)t; return timer_stop_fails ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_set_raw_count(gptimer_handle_t t, uint64_t value) { (void)t; clock_count = value; return timer_fail == 6 ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_get_raw_count(gptimer_handle_t t, uint64_t *value) { (void)t; *value = clock_count; return timer_count_fails ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_set_alarm_action(gptimer_handle_t t, const gptimer_alarm_config_t *c) { (void)t; next_alarm = c->alarm_count; return timer_alarm_fails ? ESP_FAIL : ESP_OK; }
static nn2_port_optical_config_t configuration(uint8_t fs) {
    nn2_port_optical_config_t c = {.fs = fs};
    for (unsigned i = 0; i < 20; ++i) c.current_ma[i] = 30;
    return c;
}
static void restore(void) {
    (void)cancel(NULL); owner_step();
    timer_fail = 0; register_fails = false;
    timer_count_fails = timer_stop_fails = false; cancel_io = 0;
    remove_fails = false; occupied = false; fail_at = io_count = 0;
    corrupt_read = corrupt_current_read = cancel_at_write = timer_start_fails = timer_alarm_fails = false; io_cost = 100;
    CHECK(clear_fault(NULL) == NN2_OK);
}
static void begin(uint8_t fs) {
    nn2_port_optical_config_t config = configuration(fs);
    CHECK(start(NULL, &config) == NN2_OK);
    CHECK(state(NULL) == NN2_OPTICAL_STARTING && !s_status.bus_released);
    owner_step();
}
static void alarm_tick(bool consume) {
    clock_count = next_alarm;
    gptimer_alarm_event_data_t event = {.count_value = clock_count, .alarm_value = next_alarm};
    (void)alarm_callback(s_timer, &event, NULL);
    if (consume) owner_step();
}
static void geometry_and_scan(void) {
    static const uint8_t map[20] = {0,1,2,3,4,5,6,7,8,9,14,15,16,17,18,19,20,21,22,23};
    for (unsigned fs = 0; fs <= 255; ++fs) {
        bool allowed = fs >= 2 && fs <= 10;
        CHECK(nn2_core_optical_fs_valid((uint8_t)fs) == allowed);
        if (!allowed) continue;
        begin((uint8_t)fs);
        CHECK(state(NULL) == NN2_OPTICAL_RUNNING && !reset_low);
        CHECK(s_half_us * 40u * fs <= 1000000u);
        CHECK(1000000u - s_half_us * 40u * fs < 40u * fs);
        for (unsigned ch = 0; ch < 24; ++ch)
            { CHECK(regs[0x22 + ch] == (ch >= 10 && ch <= 13 ? 0 : 133));
              CHECK(regs[0x0a + ch] == 255); }
        for (unsigned slot = 0; slot < 20; ++slot) {
            alarm_tick(true);
            for (unsigned b = 0; b < 6; ++b)
                CHECK(regs[2 + b] == (b == map[slot] / 4u ? (1u << (2u * (map[slot] % 4u))) : 0));
            alarm_tick(true);
            for (unsigned b = 0; b < 6; ++b) CHECK(regs[2 + b] == 0);
        }
        CHECK(s_status.frames == 1 && s_status.edges == 40 && s_status.error == 0);
        restore(); CHECK(reset_low && s_status.bus_released);
    }
}
static void fault_paths(void) {
    /* Failure at each prepare I2C operation must prevent scan and hold reset. */
    begin(5); unsigned operations = io_count; restore();
    for (unsigned i = 1; i <= operations; ++i) {
        fail_at = i; begin(5);
        CHECK(state(NULL) == NN2_OPTICAL_FAULT && reset_low && s_status.bus_released);
        restore();
    }
    corrupt_read = true; begin(5); CHECK(state(NULL) == NN2_OPTICAL_FAULT && reset_low); restore();
    corrupt_current_read = true; begin(5);
    CHECK(state(NULL) == NN2_OPTICAL_FAULT && reset_low); restore();
    cancel_at_write = true; begin(5);
    CHECK(state(NULL) == NN2_OPTICAL_IDLE && reset_low && s_status.bus_released); restore();
    begin(5); regs[1] = 0x85;
    for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
    CHECK(state(NULL) == NN2_OPTICAL_FAULT && reset_low && s_status.pca_mode2 == 0x85); restore();
    begin(5); alarm_tick(false); alarm_tick(false);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.deadline_misses == 1);
    owner_step(); restore();
    begin(5); io_cost = 6000; alarm_tick(true);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT); restore();
    begin(5); cancel_at_write = true; alarm_tick(true);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_IDLE && s_status.bus_released); restore();
    nn2_port_optical_config_t config = configuration(5);
    CHECK(start(NULL, &config) == NN2_OK); (void)cancel(NULL); owner_step();
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_IDLE); restore();
    begin(5); (void)cancel(NULL); remove_fails = true; owner_step();
    CHECK(state(NULL) == NN2_OPTICAL_FAULT && !s_status.bus_released && s_pca != NULL);
    CHECK(clear_fault(NULL) != NN2_OK && start(NULL, &config) != NN2_OK);
    remove_fails = false; owner_step(); restore();
    occupied = true; begin(5); CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT); restore();
    timer_start_fails = true; begin(5);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.bus_released); restore();
    begin(5); timer_alarm_fails = true; alarm_tick(true);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.bus_released); restore();
    config.current_ma[0] = 31;
    CHECK(start(NULL, &config) == NN2_ERR_INVALID_ARG);
    config = configuration(11); CHECK(start(NULL, &config) == NN2_ERR_INVALID_ARG);
}
static void eflag_evidence(void) {
    begin(5);
    /* Reproduce the Owner's exact aggregate status, with distinguishable
     * hardware-channel evidence. Physical light is not modeled. */
    regs[1] = 0x45;
    regs[0x41] = 0x01; /* ch0 SHORT */
    regs[0x46] = 0x80; /* ch23 OPEN */
    for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
    CHECK(state(NULL) == NN2_OPTICAL_FAULT && reset_low && s_status.bus_released);
    CHECK(s_status.edges == 39 && s_status.frames == 0 && s_status.deadline_misses == 0);
    CHECK(s_status.pca_mode2 == 0x05 && s_status.error == ESP_ERR_INVALID_RESPONSE);
    CHECK(s_status.pca.valid_channels == 0xffc3ff);
    CHECK(s_status.pca.short_channels == 1 && s_status.pca.open_channels == 0x800000);
    CHECK(s_status.pca.flags[0] == 1 && s_status.pca.flags[5] == 0x80);
    CHECK(regs[0x41] == 0 && regs[0x46] == 0); /* Reset erased device flags only. */
    owner_step(); CHECK(s_status.pca.open_channels == 0x800000);
    restore();
    nn2_port_optical_config_t config = configuration(5);
    if (CONFIG_NN2_LED_SHORT_BENCH) return;
    config.current_ma[0] = 0;
    CHECK(start(NULL, &config) == NN2_OK); owner_step();
    regs[0x41] = 3; /* No sample for skipped zero-current slot0. */
    alarm_tick(true); alarm_tick(true);
    CHECK(s_status.pca.valid_channels == 0 && s_status.pca.invalid_channels == 0);
    regs[0x41] = 0xc; /* slot1/ch1 reserved error code3; preserve it as unknown. */
    alarm_tick(true); alarm_tick(true);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT);
    CHECK(s_status.pca.valid_channels == 2 && s_status.pca.invalid_channels == 2);
    CHECK(s_status.pca.open_channels == 0 && s_status.pca.short_channels == 0);
    restore();
    begin(5); alarm_tick(true); fail_at = io_count + 1; alarm_tick(true); /* EFLAG read failed. */
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.pca.valid_channels == 0);
    restore();
}
static void short_pattern(unsigned code) {
    for (unsigned i = 0; i < 6; ++i) regs[0x41 + i] = (uint8_t)(code * 0x55u);
}
static void classifier_tests(void) {
    for (unsigned error = 0; error < 2; ++error) {
        for (unsigned pattern = 0; pattern < 4; ++pattern) {
            begin(5); regs[1] = (uint8_t)(5u | (error ? 0x40u : 0));
            if (pattern == 1) short_pattern(1);
            if (pattern == 2) regs[0x41] = 1;
            /* pattern3: missing sample, injected at frame health boundary. */
            for (unsigned phase = 0; phase < 40; ++phase) {
                if (pattern == 3 && phase == 39) s_chip.expected_channels |= 1u << 10;
                alarm_tick(true);
            }
            bool healthy = !error && pattern == 0;
            bool warning = CONFIG_NN2_LED_SHORT_BENCH && error && pattern == 1;
            CHECK((state(NULL) == NN2_OPTICAL_RUNNING) == (healthy || warning));
            CHECK(s_status.pca.warning_frames == (warning ? 1u : 0u));
            if (!healthy && !warning) CHECK(reset_low);
            restore();
        }
    }
    /* Every active channel and all nonhealthy codes; OPEN/reserved stop at read. */
    for (unsigned slot = 0; slot < 20; ++slot) for (unsigned code = 1; code < 4; ++code) {
        begin(5);
        for (unsigned phase = 0; phase < slot * 2; ++phase) alarm_tick(true);
        uint8_t ch = nn2_board_pca_channel((uint8_t)slot);
        regs[0x41 + ch / 4] = (uint8_t)(code << (2 * (ch % 4)));
        alarm_tick(true); alarm_tick(true);
        uint32_t bit = UINT32_C(1) << ch;
        CHECK(s_status.pca.valid_channels & bit);
        CHECK((code == 1 ? s_status.pca.short_channels : code == 2 ?
               s_status.pca.open_channels : s_status.pca.invalid_channels) == bit);
        if (code >= 2) CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT);
        restore();
    }
    if (CONFIG_NN2_LED_SHORT_BENCH) {
        begin(5); regs[1] = 0x45; short_pattern(1);
        for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
        CHECK(s_status.frames == 1 && s_status.pca.warning_frames == 1);
        CHECK(s_status.pca.epoch == 1 && s_status.last_complete.epoch == 1);
        /* Frame2 cannot borrow slot0 from frame1. */
        alarm_tick(true); s_chip.expected_channels &= ~UINT32_C(1); alarm_tick(true);
        s_chip.expected_channels |= 1;
        for (unsigned i = 2; i < 40; ++i) alarm_tick(true);
        CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT);
        CHECK(s_status.pca.epoch == 2 && s_status.pca.valid_channels == 0xffc3fe);
        CHECK(s_status.last_complete.epoch == 1 && s_status.first_fault.epoch == 2);
        restore();
    }
    begin(5);
    for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
    CHECK(nn2_board_pca_health(&s_chip) != NN2_OK); /* Replay of completed frame. */
    restore();
    for (unsigned mode = 0; mode < 3; ++mode) {
        begin(5); regs[1] = (uint8_t)(5u | (mode == 0 ? 0x80u : mode == 1 ? 0x20u : 0x08u));
        for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
        CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT); restore();
    }
    begin(5); regs[0] = 0;
    for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT); restore();
}
static void boundary_tests(void) {
    /* Cancellation at every prepare transaction; no late release/rearm. */
    begin(5); unsigned operations = io_count; restore();
    for (unsigned i = 1; i <= operations; ++i) {
        cancel_io = i; begin(5);
        CHECK(reset_low && !s_armed && s_status.bus_released); restore();
    }
    for (unsigned phase = 0; phase < 40; ++phase) {
        begin(5);
        for (unsigned i = 0; i < phase; ++i) alarm_tick(true);
        cancel_io = io_count + 1; alarm_tick(true);
        CHECK(reset_low && !s_armed && s_status.bus_released);
        uint32_t edges = s_status.edges;
        alarm_tick(true); CHECK(s_status.edges == edges && reset_low); restore();
    }
    for (unsigned kind = 1; kind <= 6; ++kind) {
        if (kind == 3 || kind == 5) continue; /* Teardown is tested with a running timer below. */
        if (kind <= 2) { s_timer = NULL; s_timer_callbacks = false; }
        timer_fail = kind; begin(5);
        CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT); restore();
    }
    s_timer = NULL; s_timer_callbacks = false;
    timer_fail = 3; register_fails = true; begin(5);
    CHECK(reset_low && !s_status.bus_released && s_timer != NULL);
    timer_fail = 0; register_fails = false; owner_step(); restore();
    begin(5); timer_fail = 5; (void)cancel(NULL); owner_step();
    CHECK(reset_low && !s_status.bus_released && clear_fault(NULL) != NN2_OK);
    timer_fail = 0; owner_step(); restore();
    /* Each phase's bus failure, including EFLAG and frame MODE1/MODE2 reads. */
    for (unsigned phase = 0; phase < 40; ++phase) {
        unsigned transactions = phase == 39 ? 3 : (phase & 1u) ? 2 : 1;
        for (unsigned tx = 1; tx <= transactions; ++tx) {
            begin(5);
            for (unsigned i = 0; i < phase; ++i) alarm_tick(true);
            fail_at = io_count + tx; alarm_tick(true);
            CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT); restore();
        }
    }
    begin(5); alarm_tick(false); clock_count += s_half_us;
    owner_step(); CHECK(reset_low && s_status.deadline_misses == 1);
    CHECK(s_status.dispatch_max_us == s_half_us); restore();
    begin(5); timer_count_fails = true; alarm_tick(true);
    CHECK(reset_low && s_status.first_stop == STOP_TIMER);
    timer_count_fails = false; restore();
    begin(5); (void)cancel(NULL); timer_stop_fails = true; owner_step();
    CHECK(reset_low && !s_status.bus_released && clear_fault(NULL) != NN2_OK);
    timer_stop_fails = false; owner_step(); restore();
    if (CONFIG_NN2_LED_SHORT_BENCH) {
        nn2_port_optical_config_t c = configuration(5);
        c.fs = 1; CHECK(start(NULL, &c) == NN2_ERR_INVALID_ARG);
        c.fs = 5; c.current_ma[3] = 0; CHECK(start(NULL, &c) == NN2_ERR_INVALID_ARG);
        c.current_ma[3] = 29; CHECK(start(NULL, &c) == NN2_ERR_INVALID_ARG);
        begin(5); (void)cancel(NULL); owner_step();
        CHECK(clear_fault(NULL) == NN2_OK); c = configuration(5);
        CHECK(start(NULL, &c) == NN2_OK); /* Same boot: no attempt quota. */
        (void)cancel(NULL); owner_step();
        CHECK(clear_fault(NULL) == NN2_OK && start(NULL, &c) == NN2_OK);
        (void)cancel(NULL); owner_step(); restore();
    }
    begin(5); unsigned prior_io = io_count;
    for (unsigned i = 0; i < 1000; ++i) {
        nn2_led_bringup_status_t snapshot = nn2_led_bringup_status();
        CHECK(snapshot.state == NN2_OPTICAL_RUNNING);
    }
    nn2_led_bringup_status_t maximum;
    memset(&maximum, 0xff, sizeof(maximum));
    /* Set bools explicitly; other all-one integers exercise maximum text width. */
    maximum.limit_hit = maximum.attempt_used = maximum.bus_released = true;
    maximum.error = INT32_MIN;
    char text[1536]; memset(text, 'x', 750); text[750] = 0;
    CHECK(nn2_led_bringup_format_status(text + 750, sizeof(text) - 750, &maximum));
    CHECK(strlen(text) < sizeof(text) && text[strlen(text) - 1] == '\n');
    CHECK(strstr(text, "short_policy=") && strstr(text, "led_completion_us=4294967295"));
    CHECK(!nn2_led_bringup_format_status(text, 8, &maximum) && text[7] == 0);
    CHECK(!nn2_led_bringup_format_status(NULL, 0, &maximum));
    CHECK(io_count == prior_io); restore(); /* STATUS never reads hardware. */
}
static void host_duration_and_repeated_runs(void) {
    nn2_port_optical_config_t c = configuration(5);
    for (unsigned run = 0; run < 2; ++run) {
        begin(5);
        if (CONFIG_NN2_LED_SHORT_BENCH) { regs[1] = 0x45; short_pattern(1); }
        unsigned phases = run == 0 ? 3000u : 180000u; /* Fake time:15s and15min. */
        for (unsigned i = 0; i < phases; ++i) alarm_tick(true);
        CHECK(state(NULL) == NN2_OPTICAL_RUNNING && !s_status.limit_hit);
        CHECK(s_status.frames == phases / 40 && s_status.edges == phases);
        CHECK(s_status.deadline_misses == 0 && s_status.first_stop == STOP_NONE);
        CHECK(s_status.pca.warning_frames == (CONFIG_NN2_LED_SHORT_BENCH ? phases / 40 : 0));
        /* START stays blocked while active and while old work drains. */
        CHECK(start(NULL, &c) == NN2_ERR_INVALID_STATE);
        (void)cancel(NULL);
        CHECK(start(NULL, &c) == NN2_ERR_INVALID_STATE);
        owner_step();
        CHECK(reset_low && s_status.bus_released && state(NULL) == NN2_OPTICAL_IDLE);
        /* Next iteration starts on the same boot, without resetting test globals. */
    }
    begin(5); regs[1] = 0x85;
    for (unsigned i = 0; i < 40; ++i) alarm_tick(true);
    CHECK(state(NULL) == NN2_OPTICAL_FAULT && reset_low);
    CHECK(start(NULL, &c) == NN2_ERR_INVALID_STATE); /* No automatic fault clear. */
    CHECK(clear_fault(NULL) == NN2_OK && start(NULL, &c) == NN2_OK);
    (void)cancel(NULL); owner_step(); restore();
}
static uint16_t protocol_command(nn2_app_comm_storage_t *app, uint32_t id, uint16_t op) {
    nn2_core_packet_t request = {.type = NN2_MSG_REQUEST, .protocol_version = 1, .length = 6};
    nn2_core_put_u32(request.payload, id); nn2_core_put_u16(request.payload + 4, op);
    uint8_t input[NN2_CORE_PACKET_MAX], output[NN2_CORE_PACKET_MAX];
    size_t n = nn2_core_packet_encode(&request, input, sizeof(input));
    size_t m = nn2_app_comm_process(app, 1, 1000, input, n, output, sizeof(output));
    nn2_core_packet_t reply = {0};
    CHECK(m && nn2_core_packet_decode(output, m, &reply));
    return nn2_core_get_u16(reply.payload + 6);
}
static void protocol_interlocks(void) {
    for (unsigned boundary = 0; boundary < 4; ++boundary) {
        nn2_app_comm_storage_t app;
        nn2_port_safety_t safety = nn2_led_bringup_safety_port();
        nn2_port_optical_t optical = nn2_led_bringup_port();
        CHECK(nn2_app_comm_init(&app, 42, &safety, 7) == NN2_OK);
        CHECK(nn2_app_comm_bind_optical(&app, &optical) == NN2_OK);
        CHECK(strlen(optical.policy_info) + 60 <= 256);
        CHECK(protocol_command(&app, 1, NN2_CMD_START) == NN2_REPLY_OK);
        uint32_t generation = s_generation;
        CHECK(protocol_command(&app, 1, NN2_CMD_START) == NN2_REPLY_OK);
        CHECK(s_generation == generation); /* Cached retransmit never starts again. */
        if (boundary) { owner_step(); alarm_tick(false); }
        if (boundary < 2) CHECK(protocol_command(&app, 2, NN2_CMD_STOP) == NN2_REPLY_OK);
        else if (boundary == 2) {
            CHECK(nn2_app_comm_tick(&app, 1000 + NN2_APP_HOST_SILENCE_US, true) == NN2_OK);
            CHECK(nn2_app_comm_tick(&app, 1000 + NN2_APP_HOST_SILENCE_US + NN2_APP_PEER_GRACE_US, true) == NN2_OK);
        } else CHECK(nn2_app_comm_safe_off(&app) == NN2_OK);
        owner_step();
        CHECK(reset_low && !s_armed && s_status.bus_released);
        CHECK(s_status.first_stop == STOP_CANCEL);
        if (CONFIG_NN2_LED_SHORT_BENCH) {
            nn2_port_optical_config_t c = configuration(5);
            CHECK(clear_fault(NULL) == NN2_OK && start(NULL, &c) == NN2_OK);
            (void)cancel(NULL); owner_step();
        }
        restore();
    }
}
static void asynchronous_boundaries(void) {
    begin(10); defer_completion = true;
    alarm_tick(true);
    CHECK(s_status.edges == 1 && s_status.i2c_submitted == 1 && s_status.i2c_completed == 1);
    CHECK(s_status.i2c_errors == 0 && !s_pending);
    restore(); defer_completion = false;
    for (unsigned event = 0; event < 2; ++event) {
        begin(10); fake_event = event ? I2C_EVENT_TIMEOUT : I2C_EVENT_NACK;
        alarm_tick(true);
        CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.i2c_errors == 1);
        CHECK(s_status.i2c_event == (uint8_t)fake_event);
        CHECK(s_status.error == (event ? ESP_ERR_TIMEOUT : ESP_FAIL));
        fake_event = I2C_EVENT_DONE; restore();
    }
    begin(10); defer_completion = true; cancel_on_wait = true;
    alarm_tick(true);
    CHECK(reset_low && s_status.edges == 0 && s_status.i2c_stale == 1 && s_status.bus_released);
    defer_completion = false; restore();
    begin(10); defer_completion = true; cancel_on_wait = true;
    uint8_t readback[2] = {0xa5, 0x5a};
    CHECK(chip_read(NULL, 0x80, readback, 2) != NN2_OK);
    CHECK(readback[0] == 0xa5 && readback[1] == 0x5a); /* No canceled read publication. */
    owner_step(); defer_completion = false; restore();
    begin(10); suppress_completion = true; deadline_on_wait = true;
    alarm_tick(true);
    CHECK(reset_low && s_status.first_stop == STOP_DEADLINE && s_status.deadline_misses == 1);
    CHECK(s_pending && !s_status.bus_released && s_status.edges == 0);
    suppress_completion = false; finish_transfer(); owner_step();
    CHECK(reset_low && s_status.bus_released && s_status.i2c_stale == 1);
    restore();
    /* Accepted-but-unfinished I/O retains buffers, callback and handles. */
    begin(10); suppress_completion = true; drain_fails = true;
    alarm_tick(true);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && !s_status.bus_released);
    CHECK(s_pending && s_pca && fake_callback && clear_fault(NULL) != NN2_OK);
    nn2_port_optical_config_t c = configuration(10);
    CHECK(start(NULL, &c) == NN2_ERR_INVALID_STATE);
    uint32_t edges = s_status.edges;
    suppress_completion = false; finish_transfer(); drain_fails = false; owner_step();
    CHECK(reset_low && s_status.edges == edges && s_status.i2c_stale == 1 && s_status.bus_released);
    restore();
    begin(10); (void)cancel(NULL); callback_fails = true; owner_step();
    CHECK(reset_low && !s_status.bus_released && s_pca && fake_callback);
    callback_fails = false; owner_step(); restore();
    callback_fails = true; begin(10);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.bus_released);
    callback_fails = false; restore();
}
static void boot_faults(void) {
    for (unsigned step = 1; step <= 38; ++step) {
        s_armed = true; s_worker_generation = ++s_generation;
        s_status.state = NN2_OPTICAL_STARTING; s_status.bus_released = false;
        CHECK(open_bus() == ESP_OK);
        io_count = 0; fail_at = step;
        CHECK(nn2_board_pca_safe_boot(&s_io) != NN2_OK && reset_low);
        restore();
    }
}
int main(void) {
    CHECK(nn2_led_bringup_init() == ESP_OK && reset_low && !occupied);
    asynchronous_boundaries(); geometry_and_scan(); fault_paths(); boot_faults(); eflag_evidence(); classifier_tests(); boundary_tests(); protocol_interlocks(); host_duration_and_repeated_runs();
    printf("%u LED checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
