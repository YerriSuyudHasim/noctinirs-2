/* ESP-IDF 6.1 PCA9956B safe-off adapter.
 *
 * Called from app_main before any application task, queue, timer, or Wi-Fi
 * is created. bind() itself does not touch a pin. The first hardware call is
 * set_reset_asserted, which drives IO18 low.
 *
 * The synchronous I2C master driver waits up to NN2_PCA9956B_I2C_TIMEOUT_MS.
 * This file never passes -1. trans_queue_depth is 0 so the async path, which
 * waits with portMAX_DELAY, is not used. i2c_new_master_bus still takes its
 * own bus lock with portMAX_DELAY once during init, immediately after giving
 * that lock; that is inside ESP-IDF, not an NN2 retry. No task is created
 * here. The task watchdog is not subscribed, reset, or disabled.
 *
 * MODE1/SLEEP is not written. There is no register write other than IREFALL,
 * PWMALL, and LEDOUT0-5, and those accept only the off value 0x00.
 * The 2 ms release wait uses esp_rom_delay_us, not vTaskDelay.
 */
#include "nn2_emitter_safe_hw.h"
#include "nn2_adapter_emitter_safe.h"
#include "nn2_reset_led_pin.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"

#if defined(NN2_PCA9956B_SDA_GPIO) && defined(NN2_PCA9956B_SCL_GPIO)
#if NN2_PCA9956B_SDA_GPIO == NN2_RESET_LED_GPIO || NN2_PCA9956B_SCL_GPIO == NN2_RESET_LED_GPIO
#error HW-TBD (Agent 04): I2C pin collides with RESET_LED IO18
#endif
#if NN2_PCA9956B_SDA_GPIO == NN2_PCA9956B_SCL_GPIO
#error HW-TBD (Agent 04): NN2_PCA9956B_SDA_GPIO and NN2_PCA9956B_SCL_GPIO must differ
#endif
#endif

/* The header #error stops a real target build when any symbol is missing.
 * Keep the rest of this file out of that translation so the diagnostic is
 * the HW-TBD list, not a cascade of undeclared identifiers. */
#if defined(NN2_PCA9956B_I2C_PORT) && defined(NN2_PCA9956B_SDA_GPIO) && \
    defined(NN2_PCA9956B_SCL_GPIO) && defined(NN2_PCA9956B_ADDR_7BIT) && \
    defined(NN2_PCA9956B_I2C_TIMEOUT_MS) && defined(NN2_PCA9956B_I2C_SCL_HZ) && \
    defined(NN2_PCA9956B_I2C_SCL_WAIT_US) && defined(NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT) && \
    defined(NN2_PCA9956B_I2C_INTERNAL_PULLUP) && defined(NN2_PCA9956B_REG_LEDOUT0) && \
    defined(NN2_PCA9956B_REG_LEDOUT_STRIDE) && defined(NN2_PCA9956B_REG_IREF0) && \
    defined(NN2_PCA9956B_REG_IREF_STRIDE) && defined(NN2_IO18_RELEASE_DRIVES_HIGH)

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static nn2_status_t from_esp(esp_err_t err)
{
    if (err == ESP_OK) {
        return NN2_OK;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return NN2_ERR_TIMEOUT;
    }
    return NN2_ERR_IO;
}

static nn2_status_t drive_low(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << NN2_RESET_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err;
    (void)gpio_set_level((gpio_num_t)NN2_RESET_LED_GPIO, 0);
    err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return from_esp(err);
    }
    return from_esp(gpio_set_level((gpio_num_t)NN2_RESET_LED_GPIO, 0));
}

static nn2_status_t release_reset(void)
{
#if NN2_IO18_RELEASE_DRIVES_HIGH == 1
    esp_err_t err = gpio_set_direction((gpio_num_t)NN2_RESET_LED_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        return from_esp(err);
    }
    return from_esp(gpio_set_level((gpio_num_t)NN2_RESET_LED_GPIO, 1));
#else
    /* High-Z. Issue #49 states R46 is a 10 kOhm pull-up on RESET_LED.
     * Internal pulls stay off so they do not fight that resistor.
     * Agent 04 chooses this path by setting NN2_IO18_RELEASE_DRIVES_HIGH to 0. */
    esp_err_t err = gpio_set_direction((gpio_num_t)NN2_RESET_LED_GPIO, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        return from_esp(err);
    }
    err = gpio_pullup_dis((gpio_num_t)NN2_RESET_LED_GPIO);
    if (err != ESP_OK) {
        return from_esp(err);
    }
    return from_esp(gpio_pulldown_dis((gpio_num_t)NN2_RESET_LED_GPIO));
#endif
}

static nn2_status_t set_reset_asserted(void *ctx, bool asserted)
{
    (void)ctx;
    if (asserted) {
        return drive_low();
    }
    return release_reset();
}

static nn2_status_t ensure_bus(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = NN2_PCA9956B_I2C_PORT,
        .sda_io_num = (gpio_num_t)NN2_PCA9956B_SDA_GPIO,
        .scl_io_num = (gpio_num_t)NN2_PCA9956B_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = (uint8_t)NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = NN2_PCA9956B_I2C_INTERNAL_PULLUP,
            .allow_pd = 0,
        },
    };
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)NN2_PCA9956B_ADDR_7BIT,
        .scl_speed_hz = (uint32_t)NN2_PCA9956B_I2C_SCL_HZ,
        .scl_wait_us = (uint32_t)NN2_PCA9956B_I2C_SCL_WAIT_US,
        .flags.disable_ack_check = 0,
    };
    esp_err_t err;

    if (s_dev) {
        return NN2_OK;
    }
    err = i2c_new_master_bus(&bus, &s_bus);
    if (err != ESP_OK) {
        s_bus = NULL;
        return from_esp(err);
    }
    err = i2c_master_bus_add_device(s_bus, &dev, &s_dev);
    if (err != ESP_OK) {
        (void)i2c_del_master_bus(s_bus);
        s_bus = NULL;
        s_dev = NULL;
        return from_esp(err);
    }
    return NN2_OK;
}

static nn2_status_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2];
    nn2_status_t st = ensure_bus();
    if (st != NN2_OK) {
        return st;
    }
    buf[0] = reg;
    buf[1] = value;
    return from_esp(i2c_master_transmit(s_dev, buf, sizeof buf, NN2_PCA9956B_I2C_TIMEOUT_MS));
}

static nn2_status_t read_reg(uint8_t reg, uint8_t *value)
{
    nn2_status_t st = ensure_bus();
    if (st != NN2_OK) {
        return st;
    }
    return from_esp(i2c_master_transmit_receive(s_dev, &reg, 1u, value, 1u,
        NN2_PCA9956B_I2C_TIMEOUT_MS));
}

static nn2_status_t indexed_reg(unsigned base, unsigned stride, uint8_t index, uint8_t *reg)
{
    unsigned addr = base + stride * (unsigned)index;
    if (addr > 255u) {
        return NN2_ERR_INVALID_ARG;
    }
    *reg = (uint8_t)addr;
    return NN2_OK;
}

static nn2_status_t write_off(uint8_t reg, uint8_t value)
{
    if (value != NN2_PORT_EMITTER_SAFE_REG_OFF) {
        return NN2_ERR_NOT_SUPPORTED;
    }
    return write_reg(reg, value);
}

static nn2_status_t write_irefall(void *ctx, uint8_t value)
{
    (void)ctx;
    return write_off((uint8_t)NN2_PORT_EMITTER_SAFE_REG_IREFALL, value);
}

static nn2_status_t write_pwmall(void *ctx, uint8_t value)
{
    (void)ctx;
    return write_off((uint8_t)NN2_PORT_EMITTER_SAFE_REG_PWMALL, value);
}

static nn2_status_t write_ledout(void *ctx, uint8_t index, uint8_t value)
{
    uint8_t reg;
    nn2_status_t st;
    (void)ctx;
    if (index >= NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT) {
        return NN2_ERR_INVALID_ARG;
    }
    st = indexed_reg(NN2_PCA9956B_REG_LEDOUT0, NN2_PCA9956B_REG_LEDOUT_STRIDE, index, &reg);
    if (st != NN2_OK) {
        return st;
    }
    return write_off(reg, value);
}

static nn2_status_t read_iref(void *ctx, uint8_t index, uint8_t *value)
{
    uint8_t reg;
    nn2_status_t st;
    (void)ctx;
    if (!value || index >= NN2_PORT_EMITTER_SAFE_IREF_COUNT) {
        return NN2_ERR_INVALID_ARG;
    }
    st = indexed_reg(NN2_PCA9956B_REG_IREF0, NN2_PCA9956B_REG_IREF_STRIDE, index, &reg);
    if (st != NN2_OK) {
        return st;
    }
    return read_reg(reg, value);
}

static nn2_status_t read_ledout(void *ctx, uint8_t index, uint8_t *value)
{
    uint8_t reg;
    nn2_status_t st;
    (void)ctx;
    if (!value || index >= NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT) {
        return NN2_ERR_INVALID_ARG;
    }
    st = indexed_reg(NN2_PCA9956B_REG_LEDOUT0, NN2_PCA9956B_REG_LEDOUT_STRIDE, index, &reg);
    if (st != NN2_OK) {
        return st;
    }
    return read_reg(reg, value);
}

static nn2_status_t delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    if (ms == 0u || ms > 1000u) {
        return NN2_ERR_INVALID_ARG;
    }
    esp_rom_delay_us(ms * 1000u);
    return NN2_OK;
}

nn2_status_t nn2_adapter_emitter_safe_bind(nn2_port_emitter_safe_t *out)
{
    if (!out) {
        return NN2_ERR_INVALID_ARG;
    }
    out->set_reset_asserted = set_reset_asserted;
    out->write_irefall = write_irefall;
    out->write_pwmall = write_pwmall;
    out->write_ledout = write_ledout;
    out->read_iref = read_iref;
    out->read_ledout = read_ledout;
    out->delay_ms = delay_ms;
    out->ctx = NULL;
    return NN2_OK;
}

#endif /* all HW-TBD symbols defined */
