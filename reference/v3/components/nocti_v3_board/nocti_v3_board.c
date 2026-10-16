#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_board.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nocti_v3_board";

static nocti_v3_board_pins_t default_pins(void)
{
    return (nocti_v3_board_pins_t) {
        .i2c_sda = 15, .i2c_scl = 16,
        .ads1299_drdy = 10, .ads1299_miso = 11, .ads1299_sclk = 12,
        .ads1299_cs = 13, .ads1299_start = 14, .ads1299_reset = 21,
        .ads1299_pwdn = 47, .ads1299_mosi = 48,
        .ads131_sync_reset = 6, .ads131_cs = 7, .ads131_drdy = 35,
        .ads131_sclk = 36, .ads131_miso = 37, .ads131_mosi = 38,
        .bq_ce = 8, .pca_reset = 18, .usb_dp = 20, .usb_dm = 19,
    };
}

void nocti_v3_board_config_defaults(nocti_v3_board_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->i2c_port = I2C_NUM_0;
    config->i2c_clock_hz = NOCTI_V3_I2C_CLOCK_SELECTED_HZ;
    config->enable_internal_pullups = false;
    config->pca9956b_address_7bit = 0x01u;
    config->max17048_address_7bit = 0x36u;
    config->bq24295_address_7bit = 0x6Bu;
    config->ads1299_spi_host = SPI2_HOST;
    config->ads131_spi_host = SPI3_HOST;
    config->ads1299_spi_clock_hz = 8000000u;
    config->ads131_spi_clock_hz = NOCTI_V3_ADS_SPI_CLOCK_HZ;
    config->ads1299_spi_mode = 1u;
    config->ads131_spi_mode = NOCTI_V3_ADS_SPI_MODE;
}

static esp_err_t configure_safe_outputs(nocti_v3_board_t *board)
{
    const nocti_v3_board_pins_t *p = &board->pins;
    const uint64_t output_mask =
        (1ULL << p->pca_reset) | (1ULL << p->ads131_sync_reset) |
        (1ULL << p->ads131_cs) | (1ULL << p->ads1299_cs) |
        (1ULL << p->ads1299_start) | (1ULL << p->ads1299_reset) |
        (1ULL << p->ads1299_pwdn) | (1ULL << p->bq_ce);
    const gpio_config_t output = {
        .pin_bit_mask = output_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&output);
    if (err != ESP_OK) return err;
    if ((err = gpio_set_level(p->pca_reset, 0)) != ESP_OK) return err;
    board->pca_reset_asserted = true;
    if ((err = gpio_set_level(p->ads131_sync_reset, 0)) != ESP_OK) return err;
    if ((err = gpio_set_level(p->ads131_cs, 1)) != ESP_OK) return err;
    if ((err = gpio_set_level(p->ads1299_cs, 1)) != ESP_OK) return err;
    if ((err = gpio_set_level(p->ads1299_start, 0)) != ESP_OK) return err;
    if ((err = gpio_set_level(p->ads1299_reset, 0)) != ESP_OK) return err;
    if ((err = gpio_set_level(p->ads1299_pwdn, 0)) != ESP_OK) return err;
    if ((err = gpio_set_level(p->bq_ce, 1)) != ESP_OK) return err;

    const gpio_config_t input = {
        .pin_bit_mask = (1ULL << p->ads1299_drdy) | (1ULL << p->ads131_drdy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&input);
    if (err == ESP_OK) board->gpio_safe = true;
    return err;
}

esp_err_t nocti_v3_board_early_safe_gpio(nocti_v3_board_t *board,
                                          const nocti_v3_board_config_t *config)
{
    if (board == NULL || config == NULL) return ESP_ERR_INVALID_ARG;
    memset(board, 0, sizeof(*board));
    board->pins = default_pins();
    board->config = *config;
    board->i2c_init_core = -1;
    return configure_safe_outputs(board);
}

esp_err_t nocti_v3_board_init(nocti_v3_board_t *board,
                              const nocti_v3_board_config_t *config)
{
    if (board == NULL || config == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nocti_v3_board_early_safe_gpio(board, config);
    if (err != ESP_OK) return err;
    if (config->i2c_clock_hz == 0u ||
        (config->i2c_clock_hz != NOCTI_V3_I2C_CLOCK_DEFAULT_HZ &&
         config->i2c_clock_hz != NOCTI_V3_I2C_CLOCK_EXPERIMENT_HZ) ||
        config->pca9956b_address_7bit > 0x7Fu ||
        config->ads131_spi_host != SPI3_HOST || config->ads131_spi_mode != 1u) {
        return ESP_ERR_INVALID_ARG;
    }
    board->isr_service_ready = false;
    ESP_LOGI(TAG,
             "board=%s safe=1 PCA_RESET#=LOW I2C_deferred_to_LED_task clock=%lu external_pullups=%u addr=0x%02X",
             NOCTI_V3_BOARD_NAME,
             (unsigned long)board->config.i2c_clock_hz,
             board->config.enable_internal_pullups ? 0u : 1u,
             (unsigned)board->config.pca9956b_address_7bit);
    ESP_LOGI(TAG,
             "ADS131 retained validated V2 pin map DRDY=GPIO35 SCLK=GPIO36 MISO=GPIO37 MOSI=GPIO38; software PSRAM disabled");
    return ESP_OK;
}

esp_err_t nocti_v3_board_i2c_create(nocti_v3_board_t *board,
                                    uint32_t queue_depth)
{
    if (board == NULL || !board->gpio_safe) return ESP_ERR_INVALID_STATE;
    if (queue_depth > 256u) return ESP_ERR_INVALID_ARG;
    if (board->i2c_ready || board->i2c_bus != NULL || board->pca_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = board->config.i2c_port,
        .sda_io_num = board->pins.i2c_sda,
        .scl_io_num = board->pins.i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = queue_depth,
        .flags = {
            .enable_internal_pullup = board->config.enable_internal_pullups ? 1u : 0u,
        },
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &board->i2c_bus);
    if (err != ESP_OK) return err;
    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = board->config.pca9956b_address_7bit,
        .scl_speed_hz = board->config.i2c_clock_hz,
        .scl_wait_us = NOCTI_V3_I2C_SCL_WAIT_US,
    };
    err = i2c_master_bus_add_device(board->i2c_bus, &dev_config,
                                    &board->pca_device);
    if (err != ESP_OK) {
        (void)i2c_del_master_bus(board->i2c_bus);
        board->i2c_bus = NULL;
        return err;
    }
    board->i2c_ready = true;
    board->pca_device_registered = true;
    board->i2c_queue_depth = queue_depth;
    board->i2c_init_core = xPortGetCoreID();
    ESP_LOGI(TAG, "I2C phase=%s qdepth=%lu core=%d clock=%lu",
             queue_depth == 0u ? "SYNC" : "ASYNC",
             (unsigned long)queue_depth, board->i2c_init_core,
             (unsigned long)board->config.i2c_clock_hz);
    return ESP_OK;
}

esp_err_t nocti_v3_board_i2c_wait_all_done(nocti_v3_board_t *board,
                                           uint32_t timeout_ms)
{
    if (board == NULL || !board->i2c_ready || board->i2c_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_bus_wait_all_done(board->i2c_bus, (int)timeout_ms);
}

esp_err_t nocti_v3_board_i2c_destroy(nocti_v3_board_t *board)
{
    if (board == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t first = ESP_OK;
    if (board->i2c_bus != NULL) {
        (void)i2c_master_bus_reset(board->i2c_bus);
    }
    if (board->pca_device != NULL) {
        const esp_err_t err = i2c_master_bus_rm_device(board->pca_device);
        if (err != ESP_OK) first = err;
        else board->pca_device = NULL;
    }
    if (board->i2c_bus != NULL && board->pca_device == NULL) {
        const esp_err_t err = i2c_del_master_bus(board->i2c_bus);
        if (err != ESP_OK && first == ESP_OK) first = err;
        else if (err == ESP_OK) board->i2c_bus = NULL;
    }
    if (first == ESP_OK) {
        board->i2c_ready = false;
        board->pca_device_registered = false;
        board->i2c_queue_depth = 0u;
        board->i2c_init_core = -1;
    }
    return first;
}


esp_err_t nocti_v3_board_i2c_reconfigure_queue_depth(nocti_v3_board_t *board,
                                                      uint32_t queue_depth)
{
    if (board == NULL || !board->gpio_safe || queue_depth > 256u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (board->i2c_ready && board->i2c_queue_depth == queue_depth) {
        return ESP_OK;
    }

    const uint32_t old_depth = board->i2c_queue_depth;
    esp_err_t err = ESP_OK;
    if (board->i2c_ready || board->i2c_bus != NULL || board->pca_device != NULL) {
        err = nocti_v3_board_i2c_destroy(board);
        if (err != ESP_OK) return err;
    }

    err = nocti_v3_board_i2c_create(board, queue_depth);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C bus recreated qdepth=%lu->%lu mode=%s",
                 (unsigned long)old_depth,
                 (unsigned long)queue_depth,
                 queue_depth == 0u ? "SYNC" : "ASYNC");
    }
    return err;
}

esp_err_t nocti_v3_board_deinit(nocti_v3_board_t *board)
{
    if (board == NULL) return ESP_ERR_INVALID_ARG;
    (void)nocti_v3_board_assert_pca_reset(board);
    return nocti_v3_board_i2c_destroy(board);
}

esp_err_t nocti_v3_board_assert_pca_reset(nocti_v3_board_t *board)
{
    if (board == NULL || !board->gpio_safe) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = gpio_set_level((gpio_num_t)board->pins.pca_reset, 0);
    if (err == ESP_OK) board->pca_reset_asserted = true;
    return err;
}

esp_err_t nocti_v3_board_release_pca_reset(nocti_v3_board_t *board)
{
    if (board == NULL || !board->gpio_safe) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = gpio_set_level((gpio_num_t)board->pins.pca_reset, 1);
    if (err == ESP_OK) board->pca_reset_asserted = false;
    return err;
}

esp_err_t nocti_v3_board_set_ads131_sync_reset(nocti_v3_board_t *board,
                                                bool high)
{
    if (board == NULL || !board->gpio_safe) return ESP_ERR_INVALID_STATE;
    return gpio_set_level((gpio_num_t)board->pins.ads131_sync_reset, high ? 1 : 0);
}
esp_err_t nocti_v3_board_assert_ads131_reset(nocti_v3_board_t *board)
{ return nocti_v3_board_set_ads131_sync_reset(board, false); }
esp_err_t nocti_v3_board_release_ads131_reset(nocti_v3_board_t *board)
{ return nocti_v3_board_set_ads131_sync_reset(board, true); }

bool nocti_v3_board_led_fail_safe(const nocti_v3_board_t *board)
{ return board != NULL && board->gpio_safe && board->pca_reset_asserted; }
const nocti_v3_board_pins_t *nocti_v3_board_pins(const nocti_v3_board_t *board)
{ return board != NULL ? &board->pins : NULL; }
const nocti_v3_board_config_t *nocti_v3_board_config(const nocti_v3_board_t *board)
{ return board != NULL ? &board->config : NULL; }
