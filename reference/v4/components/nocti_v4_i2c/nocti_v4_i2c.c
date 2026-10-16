#include "nocti_v4_i2c.h"

#include <string.h>

static esp_err_t remember(nocti_v4_i2c_t *i2c, esp_err_t result)
{
    if (i2c != NULL) {
        i2c->last_result = result;
    }
    return result;
}

esp_err_t nocti_v4_i2c_init_at_speed(nocti_v4_i2c_t *i2c,
                                      nocti_v4_health_t *health,
                                      uint32_t queue_depth,
                                      uint32_t clock_hz)
{
    if (i2c == NULL || health == NULL || i2c->initialized || i2c->bus != NULL ||
        i2c->pca != NULL || clock_hz == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(i2c, 0, sizeof(*i2c));
    i2c->clock_hz = clock_hz;
    i2c->queue_depth = queue_depth;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = NOCTI_V4_I2C_SDA_GPIO,
        .scl_io_num = NOCTI_V4_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = queue_depth,
        .flags = { .enable_internal_pullup = 0 },
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c->bus);
    if (err != ESP_OK) {
        health->i2c_init_errors++;
        return remember(i2c, err);
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = NOCTI_V4_PCA_ADDRESS_7BIT,
        .scl_speed_hz = clock_hz,
        .scl_wait_us = 1000u,
    };
    err = i2c_master_bus_add_device(i2c->bus, &device_config, &i2c->pca);
    if (err != ESP_OK) {
        health->i2c_init_errors++;
        (void)i2c_del_master_bus(i2c->bus);
        i2c->bus = NULL;
        return remember(i2c, err);
    }
    i2c->initialized = true;
    return remember(i2c, ESP_OK);
}

esp_err_t nocti_v4_i2c_init(nocti_v4_i2c_t *i2c, nocti_v4_health_t *health,
                             uint32_t queue_depth)
{
    return nocti_v4_i2c_init_at_speed(i2c, health, queue_depth,
                                       NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ);
}

esp_err_t nocti_v4_i2c_init_sync(nocti_v4_i2c_t *i2c, nocti_v4_health_t *health)
{
    return nocti_v4_i2c_init_at_speed(i2c, health,
                                       NOCTI_V4_I2C_TRANS_QUEUE_DEPTH,
                                       NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ);
}

esp_err_t nocti_v4_i2c_deinit(nocti_v4_i2c_t *i2c)
{
    if (i2c == NULL || !i2c->initialized || i2c->bus == NULL || i2c->pca == NULL) {
        return remember(i2c, ESP_ERR_INVALID_STATE);
    }
    esp_err_t result = i2c_master_bus_rm_device(i2c->pca);
    if (result != ESP_OK) {
        return remember(i2c, result);
    }
    i2c->pca = NULL;
    result = i2c_del_master_bus(i2c->bus);
    if (result != ESP_OK) {
        return remember(i2c, result);
    }
    i2c->bus = NULL;
    i2c->initialized = false;
    i2c->clock_hz = 0u;
    i2c->queue_depth = 0u;
    return remember(i2c, ESP_OK);
}

esp_err_t nocti_v4_i2c_write_reg(nocti_v4_i2c_t *i2c, uint8_t reg, uint8_t value,
                                  uint32_t timeout_ms, nocti_v4_health_t *health)
{
    if (i2c == NULL || health == NULL || !i2c->initialized || i2c->queue_depth != 0u) {
        return remember(i2c, ESP_ERR_INVALID_STATE);
    }
    const uint8_t tx[2] = {reg, value};
    const esp_err_t err = i2c_master_transmit(i2c->pca, tx, sizeof(tx), (int)timeout_ms);
    if (err != ESP_OK) {
        health->pca_write_errors++;
    }
    return remember(i2c, err);
}

esp_err_t nocti_v4_i2c_read_reg(nocti_v4_i2c_t *i2c, uint8_t reg, uint8_t *value,
                                 uint32_t timeout_ms, nocti_v4_health_t *health)
{
    if (i2c == NULL || health == NULL || value == NULL || !i2c->initialized ||
        i2c->queue_depth != 0u) {
        return remember(i2c, ESP_ERR_INVALID_STATE);
    }
    const esp_err_t err = i2c_master_transmit_receive(i2c->pca, &reg, 1u, value, 1u,
                                                       (int)timeout_ms);
    if (err != ESP_OK) {
        health->pca_read_errors++;
    }
    return remember(i2c, err);
}
