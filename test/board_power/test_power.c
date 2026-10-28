#include "nn2_board_power.h"
#include "nn2_app_battery.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)
static int task_identity, foreign_identity, bus_identity, gauge_identity, bq_identity;
static TaskHandle_t task = &task_identity;
static bool bus_live, external_bus, gauge_live, bq_live, safe_gpio;
static bool missing_gauge, wrong_part, bad_readback, remove_error;
static int ce = 1, writes, transfers, calls, fail_at;
static uint8_t bq[11] = {0x58, 0x3B, 0x20, 0x11, 0xB2, 0x9A, 0x73, 0x4B, 0x24, 0, 0xC0};
static uint8_t bad_register;

static esp_err_t step(void) { return ++calls == fail_at ? ESP_ERR_TIMEOUT : ESP_OK; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return task; }
esp_err_t nn2_board_init_safe(void) { safe_gpio = true; return step(); }
esp_err_t gpio_set_level(int gpio, uint32_t level)
{
    CHECK(gpio == 8 && level <= 1 && safe_gpio);
    esp_err_t err = step();
    if (err == ESP_OK) ce = (int)level;
    return err;
}
esp_err_t gpio_config(const gpio_config_t *config)
{
    CHECK(config->pin_bit_mask == (UINT64_C(1) << 8));
    CHECK(ce == 1 && config->mode == GPIO_MODE_OUTPUT);
    return step();
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *out)
{
    if (bus_live || external_bus) return ESP_ERR_INVALID_STATE;
    CHECK(config->i2c_port == 0 && config->sda_io_num == 15 && config->scl_io_num == 16);
    CHECK(config->trans_queue_depth == 0 && !config->flags.enable_internal_pullup);
    esp_err_t err = step();
    if (err == ESP_OK) { bus_live = true; *out = &bus_identity; }
    return err;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *config, i2c_master_dev_handle_t *out)
{
    CHECK(bus == &bus_identity && bus_live && config->scl_speed_hz == 100000);
    CHECK(config->device_address == 0x36 || config->device_address == 0x6B);
    esp_err_t err = step();
    if (err != ESP_OK) return err;
    if (config->device_address == 0x36) { gauge_live = true; *out = &gauge_identity; }
    else { bq_live = true; *out = &bq_identity; }
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    if (remove_error) return ESP_FAIL;
    if (device == &gauge_identity) { CHECK(gauge_live); gauge_live = false; }
    else { CHECK(device == &bq_identity && bq_live); bq_live = false; }
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
    CHECK(bus == &bus_identity && bus_live && !gauge_live && !bq_live);
    bus_live = false;
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *tx, size_t tx_size, uint8_t *rx, size_t rx_size, int timeout_ms)
{
    CHECK(bus_live && tx_size == 1 && timeout_ms == 20);
    transfers++;
    esp_err_t err = step();
    if (err != ESP_OK) return err;
    if (device == &gauge_identity) {
        CHECK(gauge_live && rx_size == 2);
        if (missing_gauge) return ESP_ERR_TIMEOUT;
        uint16_t value = tx[0] == 0x08 ? 0x0011 : tx[0] == 0x02 ? 0xC800 : 0x3280;
        CHECK(tx[0] == 0x08 || tx[0] == 0x02 || tx[0] == 0x04);
        rx[0] = (uint8_t)(value >> 8); rx[1] = (uint8_t)value;
    } else {
        CHECK(device == &bq_identity && bq_live && rx_size == 1 && tx[0] <= 10);
        rx[0] = wrong_part && tx[0] == 0x0A ? 0 : bq[tx[0]];
        if (bad_readback && writes > 0 && tx[0] == 0x05) rx[0] |= 0x10;
        if (bad_register && writes >= 4 && tx[0] == bad_register) rx[0] ^= 0x04;
    }
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
    const uint8_t *tx, size_t size, int timeout_ms)
{
    CHECK(device == &bq_identity && bus_live && ce == 1 && size == 2 && timeout_ms == 20);
    CHECK(tx[0] == 0x01 || tx[0] == 0x02 || tx[0] == 0x04 || tx[0] == 0x05);
    esp_err_t err = step();
    if (err == ESP_OK) { bq[tx[0]] = tx[1]; writes++; }
    return err;
}

static int monitor_reads;
static nn2_status_t monitor_result;
static nn2_status_t fake_sample(void *ctx, nn2_port_battery_sample_t *out)
{
    CHECK(ctx == &monitor_reads);
    monitor_reads++;
    *out = (nn2_port_battery_sample_t){.voltage_uv = (uint32_t)monitor_reads};
    return monitor_result;
}
static void schedule_test(void)
{
    nn2_app_battery_storage_t storage = {0};
    nn2_port_battery_t port = {.ctx = &monitor_reads, .read = fake_sample};
    CHECK(nn2_app_battery_init(&storage, &port) == NN2_OK);
    CHECK(nn2_app_battery_poll(&storage, 0, false, false) == NN2_OK && monitor_reads == 0);
    CHECK(nn2_app_battery_poll(&storage, 0, false, true) == NN2_OK && monitor_reads == 1);
    CHECK(nn2_app_battery_poll(&storage, 59999999, false, true) == NN2_OK && monitor_reads == 1);
    CHECK(nn2_app_battery_poll(&storage, 60000000, false, true) == NN2_OK && monitor_reads == 2);
    CHECK(nn2_app_battery_poll(&storage, 120000000, true, true) == NN2_OK && monitor_reads == 2);
    CHECK(nn2_app_battery_poll(&storage, 359999999, true, true) == NN2_OK && monitor_reads == 2);
    CHECK(nn2_app_battery_poll(&storage, 360000000, true, false) == NN2_OK && monitor_reads == 2);
    CHECK(nn2_app_battery_poll(&storage, 400000000, true, true) == NN2_OK && monitor_reads == 3);
    CHECK(nn2_app_battery_poll(&storage, 460000000, false, true) == NN2_OK && monitor_reads == 4);
    monitor_result = NN2_ERR_TIMEOUT;
    CHECK(nn2_app_battery_poll(&storage, 520000000, false, true) == NN2_ERR_TIMEOUT);
    nn2_app_battery_status_t status = nn2_app_battery_get_status(&storage);
    CHECK(status.valid && status.sample.voltage_uv == 4 && status.last_success_us == 460000000);
    CHECK(status.last_attempt_us == 520000000 && status.last_result == NN2_ERR_TIMEOUT);
    CHECK(nn2_app_battery_poll(&storage, 520000001, false, true) == NN2_OK && monitor_reads == 5);
    CHECK(nn2_app_battery_poll(&storage, 519999999, false, true) == NN2_ERR_INVALID_ARG);
    monitor_result = NN2_OK;
    CHECK(nn2_app_battery_poll(&storage, UINT64_C(9000000000), true, true) == NN2_OK && monitor_reads == 6);
    CHECK(nn2_app_battery_poll(&storage, UINT64_C(9000000000), true, true) == NN2_OK && monitor_reads == 6);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    const char *mode = argv[1];
    if (strcmp(mode, "schedule") == 0) { schedule_test(); puts("PASS schedule"); return 0; }
    if (strcmp(mode, "missing-gauge") == 0) missing_gauge = true;
    if (strcmp(mode, "wrong-part") == 0) wrong_part = true;
    if (strcmp(mode, "readback") == 0) bad_readback = true;
    if (strcmp(mode, "current-readback") == 0) bad_register = 2;
    if (strcmp(mode, "voltage-readback") == 0) bad_register = 4;
    bool preserve_bits = strcmp(mode, "preserve-bits") == 0;
    if (preserve_bits) { bq[2] = 0x23; bq[4] = 0xB3; }
    if (strcmp(mode, "safety-timer") == 0) bq[5] &= 0xF7u;
    if (strcmp(mode, "occupied") == 0) external_bus = true;
    if (strcmp(mode, "fault") == 0) { CHECK(argc == 3); fail_at = atoi(argv[2]); }
    bool disabled_boot = strcmp(mode, "disabled-boot") == 0;
    esp_err_t result = nn2_board_power_init(!disabled_boot);
    if (missing_gauge || wrong_part || bad_readback || bad_register || external_bus || fail_at ||
        strcmp(mode, "safety-timer") == 0) {
        CHECK(result != ESP_OK && ce == 1 && !bus_live);
        puts("PASS startup rejection"); return 0;
    }
    CHECK(result == ESP_OK && ce == (disabled_boot ? 1 : 0));
    if (!disabled_boot) CHECK(bq[1] == 0x1B && bq[5] == 0x8A && bq[2] == (preserve_bits ? 2 : 0) && bq[4] == (preserve_bits ? 0xAB : 0xAA) && writes == 4);
    else CHECK(writes == 0);
    CHECK(bq[0] == 0x58 && bq[3] == 0x11);
    CHECK(nn2_board_power_init(true) == ESP_ERR_INVALID_STATE);
    if (strcmp(mode, "wrong-owner") == 0) {
        int previous = transfers;
        task = &foreign_identity;
        CHECK(nn2_board_power_set_charge_enabled(false) == ESP_ERR_INVALID_STATE);
        CHECK(nn2_board_power_release_i2c() == ESP_ERR_INVALID_STATE);
        CHECK(transfers == previous);
        task = &task_identity;
    }
    nn2_board_power_sample_t sample = {.voltage_uv = 123};
    if (strcmp(mode, "read-failure") == 0) {
        fail_at = calls + 2; /* Voltage succeeds; SOC fails: no partial publication. */
        CHECK(nn2_board_power_read(&sample) == ESP_ERR_TIMEOUT && sample.voltage_uv == 123);
        fail_at = 0;
    }
    CHECK(nn2_board_power_read(&sample) == ESP_OK);
    CHECK(sample.voltage_uv == 4000000 && sample.soc_millipercent == 50500 && sample.bq_status_raw == 0x24);
    if (strcmp(mode, "cleanup-failure") == 0) {
        remove_error = true;
        CHECK(nn2_board_power_release_i2c() == ESP_FAIL && bus_live);
        CHECK(nn2_board_power_acquire_i2c() == ESP_ERR_INVALID_STATE);
        remove_error = false;
    }
    CHECK(nn2_board_power_release_i2c() == ESP_OK && !bus_live);
    int previous = transfers;
    CHECK(nn2_board_power_read(&sample) == ESP_ERR_INVALID_STATE && transfers == previous);
    CHECK(nn2_board_power_set_charge_enabled(false) == ESP_OK && ce == 1);
    CHECK(nn2_board_power_set_charge_enabled(true) == ESP_ERR_INVALID_STATE && ce == 1);
    external_bus = true; /* A future LED driver owns I2C0: no bus or register mutation. */
    CHECK(nn2_board_power_acquire_i2c() == ESP_ERR_INVALID_STATE && transfers == previous);
    external_bus = false;
    nn2_port_battery_t port = nn2_board_battery_port();
    CHECK(port.read(port.ctx, &sample) == NN2_OK && !bus_live && sample.voltage_uv == 4000000);
    CHECK(nn2_board_power_acquire_i2c() == ESP_OK);
    CHECK(nn2_board_power_set_charge_enabled(true) == ESP_OK && ce == 0);
    CHECK(nn2_board_power_release_i2c() == ESP_OK && ce == 0);
    puts("PASS power lifecycle");
    return 0;
}
