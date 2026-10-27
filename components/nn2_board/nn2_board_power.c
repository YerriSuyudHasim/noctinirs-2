#include "nn2_board_power.h"
#include "nn2_board.h"
#include "nn2_board_wiring.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Derived from the current schematic, not the historical firmware pin map.
 * Wiring/provenance and the synchronous bus handoff: docs/BOARD_POWER.md. */
#define POWER_SDA NN2_BOARD_SDA_GPIO
#define POWER_SCL NN2_BOARD_SCL_GPIO
#define POWER_CE GPIO_NUM_8
#define POWER_TIMEOUT_MS 20
#define POWER_CLOCK_HZ 100000u
#define GAUGE_ADDRESS 0x36u
#define BQ_ADDRESS 0x6Bu

static TaskHandle_t s_owner;
static bool s_ready;
static bool s_ce_ready;
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_gauge;
static i2c_master_dev_handle_t s_bq;

static bool is_owner(void)
{
    return s_owner != NULL && xTaskGetCurrentTaskHandle() == s_owner;
}

static esp_err_t read_register(i2c_master_dev_handle_t device, uint8_t reg,
                               uint8_t *out, size_t size)
{
    return i2c_master_transmit_receive(device, &reg, 1, out, size, POWER_TIMEOUT_MS);
}

static esp_err_t write_bq(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(s_bq, bytes, sizeof(bytes), POWER_TIMEOUT_MS);
}

static esp_err_t check_devices(void)
{
    uint8_t version[2], part;
    esp_err_t err = read_register(s_gauge, 0x08, version, sizeof(version));
    if (err != ESP_OK) return err;
    err = read_register(s_bq, 0x0A, &part, 1);
    if (err != ESP_OK) return err;
    if (version[0] != 0 || (version[1] & 0xF0u) != 0x10u || (part & 0xE0u) != 0xC0u) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

/* Keep any handle whose deletion fails: never hide live resources or reset a
 * shared bus as error recovery. Caller must not start the LED phase on error. */
esp_err_t nn2_board_power_release_i2c(void)
{
    if (!is_owner()) return ESP_ERR_INVALID_STATE;
    esp_err_t err;
    if (s_gauge != NULL) {
        err = i2c_master_bus_rm_device(s_gauge);
        if (err != ESP_OK) return err;
        s_gauge = NULL;
    }
    if (s_bq != NULL) {
        err = i2c_master_bus_rm_device(s_bq);
        if (err != ESP_OK) return err;
        s_bq = NULL;
    }
    if (s_bus != NULL) {
        err = i2c_del_master_bus(s_bus);
        if (err != ESP_OK) return err;
        s_bus = NULL;
    }
    return ESP_OK;
}

static esp_err_t open_bus(void)
{
    if (s_bus != NULL) return ESP_ERR_INVALID_STATE;
    /* Allocate I2C0 explicitly: SDK allocation rejects an occupied controller.
     * Never adopt an existing bus, which could already be asynchronous. */
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = POWER_SDA,
        .scl_io_num = POWER_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GAUGE_ADDRESS,
        .scl_speed_hz = POWER_CLOCK_HZ,
        .scl_wait_us = 1000,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_gauge);
    if (err == ESP_OK) {
        device_config.device_address = BQ_ADDRESS;
        err = i2c_master_bus_add_device(s_bus, &device_config, &s_bq);
    }
    if (err == ESP_OK) err = check_devices();
    if (err != ESP_OK) {
        esp_err_t cleanup = nn2_board_power_release_i2c();
        return cleanup == ESP_OK ? err : cleanup;
    }
    return ESP_OK;
}

esp_err_t nn2_board_power_acquire_i2c(void)
{
    if (!is_owner() || !s_ready) return ESP_ERR_INVALID_STATE;
    return open_bus();
}

esp_err_t nn2_board_power_set_charge_enabled(bool enabled)
{
    if (!is_owner() || !s_ce_ready) return ESP_ERR_INVALID_STATE;
    /* Disable needs no bus. Enable never lowers CE after a failed transaction. */
    esp_err_t err = gpio_set_level(POWER_CE, 1);
    if (err != ESP_OK || !enabled) return err;
    if (!s_ready || s_bq == NULL || s_gauge == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t reg5;
    err = read_register(s_bq, 0x05, &reg5, 1);
    if (err != ESP_OK) return err;
    /* Preserve charge termination and safety timer; reject disabled safety timer.
     * Only the HOST watchdog is disabled, so LED acquisition needs no I2C kicks. */
    if ((reg5 & 0x08u) == 0) return ESP_ERR_INVALID_STATE;
    const uint8_t expected5 = (uint8_t)(reg5 & ~0x30u);
    if (expected5 != reg5) {
        err = write_bq(0x05, expected5);
        if (err != ESP_OK) return err;
    }
    uint8_t reg1;
    err = read_register(s_bq, 0x01, &reg1, 1);
    if (err != ESP_OK) return err;
    /* Clear reset/watchdog-reset strobes and OTG, enable CHG_CONFIG.
     * Preserve SYS_MIN and the reserved bit. Never replay command bits. */
    const uint8_t expected1 = (uint8_t)((reg1 & 0x0Fu) | 0x10u);
    if (expected1 != reg1) {
        err = write_bq(0x01, expected1);
        if (err != ESP_OK) return err;
    }
    /* Owner: single-cell LiPo, 512 mA nominal, charge voltage <=4200 mV.
     * REG02: ICHG=0 ->512 mA, FORCE_20PCT=0; preserve BCOLD.
     * REG04: 42 steps ->4176 mV, leaving headroom below 4200 mV; preserve BATLOWV/VRECHG.
     * Setpoints have device tolerance; these are not measured hard limits. */
    uint8_t reg2, reg4;
    err = read_register(s_bq, 0x02, &reg2, 1);
    if (err != ESP_OK) return err;
    err = read_register(s_bq, 0x04, &reg4, 1);
    if (err != ESP_OK) return err;
    const uint8_t expected2 = (uint8_t)(reg2 & 0x02u);
    const uint8_t expected4 = (uint8_t)((reg4 & 0x03u) | (42u << 2));
    if (reg2 != expected2) {
        err = write_bq(0x02, expected2);
        if (err != ESP_OK) return err;
    }
    if (reg4 != expected4) {
        err = write_bq(0x04, expected4);
        if (err != ESP_OK) return err;
    }
    err = read_register(s_bq, 0x02, &reg2, 1);
    if (err != ESP_OK) return err;
    err = read_register(s_bq, 0x04, &reg4, 1);
    if (err != ESP_OK) return err;
    if (reg2 != expected2 || reg4 != expected4) return ESP_ERR_INVALID_RESPONSE;
    err = read_register(s_bq, 0x05, &reg5, 1);
    if (err != ESP_OK) return err;
    err = read_register(s_bq, 0x01, &reg1, 1);
    if (err != ESP_OK) return err;
    if (reg5 != expected5 || reg1 != expected1) return ESP_ERR_INVALID_RESPONSE;
    return gpio_set_level(POWER_CE, 0);
}

esp_err_t nn2_board_power_read(nn2_board_power_sample_t *sample)
{
    if (sample == NULL) return ESP_ERR_INVALID_ARG;
    if (!is_owner() || !s_ready || s_gauge == NULL || s_bq == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t vcell[2], soc[2], status;
    esp_err_t err = read_register(s_gauge, 0x02, vcell, sizeof(vcell));
    if (err != ESP_OK) return err;
    err = read_register(s_gauge, 0x04, soc, sizeof(soc));
    if (err != ESP_OK) return err;
    err = read_register(s_bq, 0x08, &status, 1);
    if (err != ESP_OK) return err;
    const uint16_t raw_vcell = (uint16_t)((uint16_t)vcell[0] << 8 | vcell[1]);
    const uint16_t raw_soc = (uint16_t)((uint16_t)soc[0] << 8 | soc[1]);
    *sample = (nn2_board_power_sample_t) {
        .vcell_raw = raw_vcell,
        .soc_raw = raw_soc,
        /* MAX17048: 78.125 uV/LSB and 1/256 percent/LSB, round to nearest.
         * Keep raw SOC even above 100%; do not invent a plausibility clamp. */
        .voltage_uv = ((uint32_t)raw_vcell * 625u + 4u) / 8u,
        .soc_millipercent = ((uint32_t)raw_soc * 1000u + 128u) / 256u,
        .bq_status_raw = status,
    };
    return ESP_OK;
}

esp_err_t nn2_board_power_init(bool enable_on_boot)
{
    /* Startup is serialized by app_main; no other task may race the first call. */
    if (s_owner != NULL) return ESP_ERR_INVALID_STATE;
    s_owner = xTaskGetCurrentTaskHandle();
    esp_err_t err = nn2_board_init_safe();
    if (err != ESP_OK) return err;
    err = gpio_set_level(POWER_CE, 1);
    if (err != ESP_OK) return err;
    const gpio_config_t ce_config = {
        .pin_bit_mask = UINT64_C(1) << POWER_CE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&ce_config);
    if (err != ESP_OK) return err;
    s_ce_ready = true;
    err = gpio_set_level(POWER_CE, 1);
    if (err != ESP_OK) return err;
    err = open_bus();
    if (err != ESP_OK) return err;
    s_ready = true;
    if (enable_on_boot) err = nn2_board_power_set_charge_enabled(true);
    if (err != ESP_OK) {
        s_ready = false;
        esp_err_t off = gpio_set_level(POWER_CE, 1);
        esp_err_t cleanup = nn2_board_power_release_i2c();
        if (off != ESP_OK) return off;
        if (cleanup != ESP_OK) return cleanup;
    }
    return err;
}

static nn2_status_t sample_and_release(void *ctx, nn2_port_battery_sample_t *out)
{
    (void)ctx;
    if (out == NULL) return NN2_ERR_INVALID_ARG;
    if (!is_owner() || !s_ready) return NN2_ERR_INVALID_STATE;
    /* Do not borrow even our own already-open bus: each monitor callback owns
     * exactly one complete lifecycle, so overlapping uses are explicit errors. */
    esp_err_t err = nn2_board_power_acquire_i2c();
    if (err != ESP_OK) return err == ESP_ERR_TIMEOUT ? NN2_ERR_TIMEOUT : NN2_ERR_IO;
    nn2_board_power_sample_t sample;
    err = nn2_board_power_read(&sample);
    esp_err_t cleanup = nn2_board_power_release_i2c();
    if (cleanup != ESP_OK) return NN2_ERR_IO;
    if (err != ESP_OK) return err == ESP_ERR_TIMEOUT ? NN2_ERR_TIMEOUT : NN2_ERR_IO;
    *out = sample;
    return NN2_OK;
}

nn2_port_battery_t nn2_board_battery_port(void)
{
    return (nn2_port_battery_t) {.read = sample_and_release};
}
