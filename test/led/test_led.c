/* Runs production owner steps and ISR callback with explicit fake peripherals.
 * This is deterministic fault injection, not RTOS or electrical timing evidence. */
#include <stdio.h>
#include <string.h>
#include "../../main/adapters/nn2_led_bringup.c"
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %u: %s\n", __LINE__, #x); } } while (0)
static uint8_t regs[0x47];
static bool reset_low = true, occupied, remove_fails;
static bool timer_start_fails, timer_alarm_fails;
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
uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t wait) { (void)clear; (void)wait; return 1; }
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 4096; }
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
    uint32_t stack, void *ctx, unsigned priority, TaskHandle_t *task, int core) {
    (void)entry; (void)ctx;
    CHECK(!strcmp(name, "nn2_led") && stack == 6144 && priority == 5 && core == 1);
    *task = &s_task; return pdPASS;
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c, i2c_master_bus_handle_t *out) {
    CHECK(c->i2c_port == 0 && c->sda_io_num == 15 && c->scl_io_num == 16 && c->trans_queue_depth == 0);
    if (occupied) return ESP_FAIL;
    occupied = true; *out = &s_bus; return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *c, i2c_master_dev_handle_t *out) {
    CHECK(bus != NULL && c->device_address == 1 && c->scl_speed_hz == 100000);
    *out = &s_pca; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device) { (void)device; return remove_fails ? ESP_FAIL : ESP_OK; }
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) { (void)bus; occupied = false; return ESP_OK; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t n, int timeout) {
    CHECK(device != NULL && timeout == 20 && n >= 2);
    clock_count += io_cost;
    if (++io_count == fail_at || reset_low) return ESP_FAIL;
    if (cancel_at_write) { cancel_at_write = false; (void)cancel(NULL); return ESP_FAIL; }
    uint8_t address = data[0] & 0x7f;
    if (s_status.state == NN2_OPTICAL_RUNNING && address >= 2 && address <= 7)
        CHECK(n == 7 && data[0] == 0x82); /* V3 full ON and OFF wire images. */
    if (address == 0x40) memset(regs + 0x22, data[1], 24);
    else if (address == 0x3f) memset(regs + 0x0a, data[1], 24);
    else if (address == 1) regs[1] = (uint8_t)((regs[1] & 0xc7) | (data[1] & 0x28));
    else for (size_t i = 1; i < n; ++i) regs[address + (data[0] & 0x80 ? i - 1 : 0)] = data[i];
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t tn, uint8_t *rx, size_t rn, int timeout) {
    CHECK(device != NULL && tn == 1 && timeout == 20);
    clock_count += io_cost;
    if (++io_count == fail_at || reset_low) return ESP_FAIL;
    memcpy(rx, regs + (*tx & 0x7f), rn);
    if (corrupt_read) rx[0] ^= 1;
    if (corrupt_current_read && rn == 24 && regs[0x22]) rx[0] ^= 1;
    return ESP_OK;
}
esp_err_t gptimer_new_timer(const gptimer_config_t *c, gptimer_handle_t *out) { CHECK(c->resolution_hz == 1000000); *out = &s_timer; return ESP_OK; }
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t, const gptimer_event_callbacks_t *c, void *ctx) { (void)t; (void)ctx; CHECK(c->on_alarm == alarm_callback); return ESP_OK; }
esp_err_t gptimer_del_timer(gptimer_handle_t t) { (void)t; return ESP_OK; }
esp_err_t gptimer_enable(gptimer_handle_t t) { (void)t; return ESP_OK; }
esp_err_t gptimer_disable(gptimer_handle_t t) { (void)t; return ESP_OK; }
esp_err_t gptimer_start(gptimer_handle_t t) { (void)t; return timer_start_fails ? ESP_FAIL : ESP_OK; }
esp_err_t gptimer_stop(gptimer_handle_t t) { (void)t; return ESP_OK; }
esp_err_t gptimer_set_raw_count(gptimer_handle_t t, uint64_t value) { (void)t; clock_count = value; return ESP_OK; }
esp_err_t gptimer_get_raw_count(gptimer_handle_t t, uint64_t *value) { (void)t; *value = clock_count; return ESP_OK; }
esp_err_t gptimer_set_alarm_action(gptimer_handle_t t, const gptimer_alarm_config_t *c) { (void)t; next_alarm = c->alarm_count; return timer_alarm_fails ? ESP_FAIL : ESP_OK; }
static nn2_port_optical_config_t configuration(uint8_t fs) {
    nn2_port_optical_config_t c = {.fs = fs};
    for (unsigned i = 0; i < 20; ++i) c.current_ma[i] = 30;
    return c;
}
static void restore(void) {
    (void)cancel(NULL); owner_step();
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
        bool allowed = fs == 1 || fs == 2 || fs == 4 || fs == 5 || fs == 8 || fs == 10;
        CHECK(nn2_core_optical_fs_valid((uint8_t)fs) == allowed);
        if (!allowed) continue;
        begin((uint8_t)fs);
        CHECK(state(NULL) == NN2_OPTICAL_RUNNING && !reset_low);
        CHECK(s_half_us * 40u * fs == 1000000u);
        for (unsigned ch = 0; ch < 24; ++ch)
            CHECK(regs[0x22 + ch] == (ch >= 10 && ch <= 13 ? 0 : 133));
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
    begin(10); alarm_tick(false); alarm_tick(false);
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.deadline_misses == 1);
    owner_step(); restore();
    begin(10); io_cost = 3000; alarm_tick(true);
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
    CHECK(s_status.pca_mode2 == 0x45 && s_status.error == ESP_ERR_INVALID_RESPONSE);
    CHECK(s_status.pca.valid_channels == 0xffc3ff);
    CHECK(s_status.pca.short_channels == 1 && s_status.pca.open_channels == 0x800000);
    CHECK(s_status.pca.flags[0] == 1 && s_status.pca.flags[5] == 0x80);
    CHECK(regs[0x41] == 0 && regs[0x46] == 0); /* Reset erased device flags only. */
    owner_step(); CHECK(s_status.pca.open_channels == 0x800000);
    restore();
    nn2_port_optical_config_t config = configuration(5);
    config.current_ma[0] = 0;
    CHECK(start(NULL, &config) == NN2_OK); owner_step();
    regs[0x41] = 3; /* No sample for skipped zero-current slot0. */
    alarm_tick(true); alarm_tick(true);
    CHECK(s_status.pca.valid_channels == 0 && s_status.pca.invalid_channels == 0);
    regs[0x41] = 0xc; /* slot1/ch1 reserved error code3; preserve it as unknown. */
    alarm_tick(true);
    CHECK(s_status.pca.valid_channels == 2 && s_status.pca.invalid_channels == 2);
    CHECK(s_status.pca.open_channels == 0 && s_status.pca.short_channels == 0);
    restore();
    begin(5); fail_at = io_count + 2; alarm_tick(true); /* ON succeeded, EFLAG read failed. */
    CHECK(reset_low && state(NULL) == NN2_OPTICAL_FAULT && s_status.pca.valid_channels == 0);
    restore();
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
    geometry_and_scan(); fault_paths(); boot_faults(); eflag_evidence();
    printf("%u LED checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
