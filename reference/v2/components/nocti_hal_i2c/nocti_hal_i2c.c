// ============================================================
// File: components/nocti_hal_i2c/nocti_hal_i2c.c
// ESP-IDF 5.5.x phased synchronous/asynchronous I2C HAL
// ============================================================

#include "nocti_hal_i2c.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nocti_log.h"
#include "nocti_timebase.h"

static const char *TAG = "nocti_hal_i2c";

static inline bool is_addr_7bit_valid(uint8_t address)
{
    return address <= 0x7Fu;
}

static inline bool port_ok(i2c_port_t port)
{
    return port == I2C_NUM_0 || port == I2C_NUM_1;
}

static inline bool handle_ok(const nocti_hal_i2c_t *h)
{
    return h != NULL && h->installed && h->mutex != NULL &&
           h->bus_handle != NULL;
}

static inline uint32_t pick_timeout_ms(const nocti_hal_i2c_t *h,
                                       uint32_t timeout_ms)
{
    uint32_t timeout = timeout_ms != 0u ? timeout_ms : h->default_timeout_ms;
    return timeout != 0u ? timeout : 1u;
}

bool nocti_hal_i2c_is_async(const nocti_hal_i2c_t *h)
{
    return handle_ok(h) && h->trans_queue_depth > 0u;
}

static void dev_cache_reset(nocti_hal_i2c_t *h)
{
    if (!h) return;
    for (int i = 0; i < NOCTI_HAL_I2C_MAX_DEV_CACHE; i++) {
        h->dev_cache[i].addr_7bit = 0u;
        h->dev_cache[i].dev_handle = NULL;
        h->dev_cache[i].in_use = false;
    }
}

static esp_err_t dev_cache_remove_all_checked(nocti_hal_i2c_t *h)
{
    if (!h || !h->bus_handle) return ESP_ERR_INVALID_ARG;

    esp_err_t first_error = ESP_OK;
    for (int i = 0; i < NOCTI_HAL_I2C_MAX_DEV_CACHE; i++) {
        if (!h->dev_cache[i].in_use || !h->dev_cache[i].dev_handle) {
            continue;
        }

        const esp_err_t err = i2c_master_bus_rm_device(
            (i2c_master_dev_handle_t)h->dev_cache[i].dev_handle);
        if (err == ESP_OK) {
            h->dev_cache[i].addr_7bit = 0u;
            h->dev_cache[i].dev_handle = NULL;
            h->dev_cache[i].in_use = false;
        } else if (first_error == ESP_OK) {
            first_error = err;
        }
    }
    return first_error;
}

static esp_err_t dev_cache_get_or_add(nocti_hal_i2c_t *h,
                                      uint8_t addr_7bit,
                                      i2c_master_dev_handle_t *out_dev)
{
    if (!handle_ok(h) || !out_dev || !is_addr_7bit_valid(addr_7bit)) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < NOCTI_HAL_I2C_MAX_DEV_CACHE; i++) {
        if (h->dev_cache[i].in_use &&
            h->dev_cache[i].addr_7bit == addr_7bit) {
            *out_dev =
                (i2c_master_dev_handle_t)h->dev_cache[i].dev_handle;
            return ESP_OK;
        }
    }

    i2c_device_config_t dev_cfg;
    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr_7bit;
    dev_cfg.scl_speed_hz = h->clk_speed_hz;
    dev_cfg.scl_wait_us = h->scl_wait_us;

    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(
        (i2c_master_bus_handle_t)h->bus_handle, &dev_cfg, &dev);
    if (err != ESP_OK) return err;

    for (int i = 0; i < NOCTI_HAL_I2C_MAX_DEV_CACHE; i++) {
        if (!h->dev_cache[i].in_use) {
            h->dev_cache[i].addr_7bit = addr_7bit;
            h->dev_cache[i].dev_handle = (void *)dev;
            h->dev_cache[i].in_use = true;
            *out_dev = dev;
            return ESP_OK;
        }
    }

    (void)i2c_master_bus_rm_device(dev);
    return ESP_ERR_NO_MEM;
}

static void fill_bus_config(const nocti_hal_i2c_t *h,
                            uint32_t queue_depth,
                            i2c_master_bus_config_t *out_cfg)
{
    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->i2c_port = h->i2c_port;
    out_cfg->sda_io_num = h->gpio_sda;
    out_cfg->scl_io_num = h->gpio_scl;
    out_cfg->clk_source = I2C_CLK_SRC_DEFAULT;
    out_cfg->glitch_ignore_cnt = h->glitch_ignore_cnt;
    out_cfg->intr_priority = h->intr_priority;
    out_cfg->trans_queue_depth = queue_depth;
    out_cfg->flags.enable_internal_pullup =
        h->enable_internal_pullups ? 1u : 0u;
}

static esp_err_t create_bus(nocti_hal_i2c_t *h, uint32_t queue_depth)
{
    i2c_master_bus_config_t bus_cfg;
    fill_bus_config(h, queue_depth, &bus_cfg);

    i2c_master_bus_handle_t bus = NULL;
    const esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) return err;

    h->bus_handle = (void *)bus;
    h->trans_queue_depth = queue_depth;
    h->installed = true;
    h->async_context = NULL;
    dev_cache_reset(h);
    return ESP_OK;
}

// Release SDA if a previous reset left a slave holding the line low. This is
// used only for the initial controller creation, never during a mode switch.
static void i2c_gpio_recover(int gpio_scl, int gpio_sda)
{
    if (gpio_scl < 0 || gpio_sda < 0) return;

    gpio_config_t io = {0};
    io.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io.pin_bit_mask = (1ULL << gpio_scl) | (1ULL << gpio_sda);
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    (void)gpio_config(&io);

    (void)gpio_set_level(gpio_scl, 1);
    (void)gpio_set_level(gpio_sda, 1);
    esp_rom_delay_us(5u);

    if (gpio_get_level(gpio_sda) == 0) {
        for (int i = 0; i < 9; i++) {
            (void)gpio_set_level(gpio_scl, 0);
            esp_rom_delay_us(5u);
            (void)gpio_set_level(gpio_scl, 1);
            esp_rom_delay_us(5u);
        }
        (void)gpio_set_level(gpio_sda, 0);
        esp_rom_delay_us(5u);
        (void)gpio_set_level(gpio_scl, 1);
        esp_rom_delay_us(5u);
        (void)gpio_set_level(gpio_sda, 1);
        esp_rom_delay_us(5u);
    }
}

esp_err_t nocti_hal_i2c_init(nocti_hal_i2c_t *h,
                             const nocti_hal_i2c_cfg_t *cfg)
{
    if (!h || !cfg || !port_ok(cfg->i2c_port) ||
        cfg->clk_speed_hz == 0u || cfg->gpio_sda < 0 || cfg->gpio_scl < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->trans_queue_depth > 256u) return ESP_ERR_INVALID_ARG;

    memset(h, 0, sizeof(*h));
    h->i2c_port = cfg->i2c_port;
    h->gpio_sda = cfg->gpio_sda;
    h->gpio_scl = cfg->gpio_scl;
    h->clk_speed_hz = cfg->clk_speed_hz;
    h->enable_internal_pullups = cfg->enable_internal_pullups;
    h->delete_driver_on_deinit = cfg->delete_driver_on_deinit;
    h->default_timeout_ms = cfg->default_timeout_ms != 0u ?
        cfg->default_timeout_ms : NOCTI_HAL_I2C_DEFAULT_TIMEOUT_MS;
    h->scl_wait_us = cfg->scl_wait_us != 0u ?
        cfg->scl_wait_us : NOCTI_HAL_I2C_DEFAULT_SCL_WAIT_US;
    h->glitch_ignore_cnt = 7u;
    h->intr_priority = 0;

    h->mutex = xSemaphoreCreateMutex();
    if (!h->mutex) {
        memset(h, 0, sizeof(*h));
        return ESP_ERR_NO_MEM;
    }

    i2c_gpio_recover(h->gpio_scl, h->gpio_sda);

    const esp_err_t err = create_bus(h, cfg->trans_queue_depth);
    if (err != ESP_OK) {
        vSemaphoreDelete(h->mutex);
        memset(h, 0, sizeof(*h));
        return err;
    }

    NOCTI_LOGI(TAG,
               "init ok port=%d clk=%u def_to=%u qdepth=%u mode=%s scl_wait_us=%u",
               (int)h->i2c_port,
               (unsigned)h->clk_speed_hz,
               (unsigned)h->default_timeout_ms,
               (unsigned)h->trans_queue_depth,
               h->trans_queue_depth == 0u ? "sync" : "async",
               (unsigned)h->scl_wait_us);
    return ESP_OK;
}

esp_err_t nocti_hal_i2c_deinit(nocti_hal_i2c_t *h)
{
    if (!h) return ESP_ERR_INVALID_ARG;

    if (h->installed && h->bus_handle) {
        if (h->async_context != NULL) {
            NOCTI_LOGE(TAG,
                       "deinit refused: asynchronous callback context still registered");
            return ESP_ERR_INVALID_STATE;
        }

        (void)i2c_master_bus_reset(
            (i2c_master_bus_handle_t)h->bus_handle);
        (void)dev_cache_remove_all_checked(h);
        if (h->delete_driver_on_deinit) {
            (void)i2c_del_master_bus(
                (i2c_master_bus_handle_t)h->bus_handle);
        }
    }

    if (h->mutex) {
        vSemaphoreDelete(h->mutex);
    }
    memset(h, 0, sizeof(*h));
    return ESP_OK;
}

esp_err_t nocti_hal_i2c_reconfigure_queue_depth(nocti_hal_i2c_t *h,
                                                 uint32_t new_queue_depth)
{
    if (!handle_ok(h) || new_queue_depth > 256u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (h->async_context != NULL) {
        NOCTI_LOGE(TAG,
                   "queue-depth change refused: async context is still registered");
        return ESP_ERR_INVALID_STATE;
    }
    if (new_queue_depth == h->trans_queue_depth) return ESP_OK;

    const uint32_t timeout_ms = pick_timeout_ms(h, h->default_timeout_ms);
    esp_err_t err = nocti_hal_i2c_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    const uint32_t old_queue_depth = h->trans_queue_depth;
    i2c_master_bus_handle_t old_bus =
        (i2c_master_bus_handle_t)h->bus_handle;

    // No transfer is allowed to be active here. Resetting the controller gives
    // the device-removal path a known idle hardware state and discards any
    // stale asynchronous queue bookkeeping left after an error.
    (void)i2c_master_bus_reset(old_bus);

    err = dev_cache_remove_all_checked(h);
    if (err != ESP_OK) {
        (void)i2c_master_bus_reset(old_bus);
        err = dev_cache_remove_all_checked(h);
    }
    if (err != ESP_OK) {
        nocti_hal_i2c_unlock(h);
        return err;
    }

    err = i2c_del_master_bus(old_bus);
    if (err != ESP_OK) {
        nocti_hal_i2c_unlock(h);
        return err;
    }

    h->bus_handle = NULL;
    h->installed = false;
    h->trans_queue_depth = 0u;
    dev_cache_reset(h);

    err = create_bus(h, new_queue_depth);
    if (err != ESP_OK) {
        const esp_err_t restore_err = create_bus(h, old_queue_depth);
        if (restore_err != ESP_OK) {
            NOCTI_LOGE(TAG,
                       "I2C mode switch failed (%s) and old mode restore failed (%s)",
                       esp_err_to_name(err), esp_err_to_name(restore_err));
        }
        nocti_hal_i2c_unlock(h);
        return err;
    }

    NOCTI_LOGI(TAG,
               "bus recreated: qdepth %u -> %u, mode=%s",
               (unsigned)old_queue_depth,
               (unsigned)new_queue_depth,
               new_queue_depth == 0u ? "sync" : "async");

    nocti_hal_i2c_unlock(h);
    return ESP_OK;
}

esp_err_t nocti_hal_i2c_lock(nocti_hal_i2c_t *h, uint32_t timeout_ms)
{
    if (!handle_ok(h)) return ESP_ERR_INVALID_ARG;

    const TickType_t ticks = pdMS_TO_TICKS(pick_timeout_ms(h, timeout_ms));
    if (xSemaphoreTake(h->mutex, ticks == 0 ? 1 : ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void nocti_hal_i2c_unlock(nocti_hal_i2c_t *h)
{
    if (!handle_ok(h)) return;
    (void)xSemaphoreGive(h->mutex);
}

static esp_err_t require_sync_bus(const nocti_hal_i2c_t *h,
                                  const char *operation)
{
    if (!handle_ok(h)) return ESP_ERR_INVALID_ARG;
    if (h->trans_queue_depth == 0u) return ESP_OK;

    NOCTI_LOGE(TAG,
               "%s rejected on async bus qdepth=%u; switch bus to sync first",
               operation ? operation : "generic I2C operation",
               (unsigned)h->trans_queue_depth);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t nocti_hal_i2c_probe(nocti_hal_i2c_t *h,
                              uint8_t dev_addr_7bit,
                              uint32_t timeout_ms)
{
    if (!is_addr_7bit_valid(dev_addr_7bit)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = require_sync_bus(h, "probe");
    if (err != ESP_OK) return err;

    err = nocti_hal_i2c_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    err = i2c_master_probe((i2c_master_bus_handle_t)h->bus_handle,
                           dev_addr_7bit,
                           (int)pick_timeout_ms(h, timeout_ms));
    nocti_hal_i2c_unlock(h);
    return err;
}

esp_err_t nocti_hal_i2c_write(nocti_hal_i2c_t *h,
                              uint8_t dev_addr_7bit,
                              const uint8_t *data,
                              size_t len,
                              uint32_t timeout_ms)
{
    if (!is_addr_7bit_valid(dev_addr_7bit) ||
        (len > 0u && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0u) return ESP_OK;

    esp_err_t err = require_sync_bus(h, "write");
    if (err != ESP_OK) return err;

    err = nocti_hal_i2c_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    i2c_master_dev_handle_t dev = NULL;
    err = dev_cache_get_or_add(h, dev_addr_7bit, &dev);
    if (err == ESP_OK) {
        err = i2c_master_transmit(dev,
                                  data,
                                  len,
                                  (int)pick_timeout_ms(h, timeout_ms));
    }

    nocti_hal_i2c_unlock(h);
    return err;
}

esp_err_t nocti_hal_i2c_read(nocti_hal_i2c_t *h,
                             uint8_t dev_addr_7bit,
                             uint8_t *data,
                             size_t len,
                             uint32_t timeout_ms)
{
    if (!is_addr_7bit_valid(dev_addr_7bit) ||
        (len > 0u && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0u) return ESP_OK;

    esp_err_t err = require_sync_bus(h, "read");
    if (err != ESP_OK) return err;

    err = nocti_hal_i2c_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    i2c_master_dev_handle_t dev = NULL;
    err = dev_cache_get_or_add(h, dev_addr_7bit, &dev);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev,
                                 data,
                                 len,
                                 (int)pick_timeout_ms(h, timeout_ms));
    }

    nocti_hal_i2c_unlock(h);
    return err;
}

esp_err_t nocti_hal_i2c_write_then_read(nocti_hal_i2c_t *h,
                                        uint8_t dev_addr_7bit,
                                        const uint8_t *tx,
                                        size_t tx_len,
                                        uint8_t *rx,
                                        size_t rx_len,
                                        uint32_t timeout_ms)
{
    if (!is_addr_7bit_valid(dev_addr_7bit) ||
        (tx_len > 0u && tx == NULL) ||
        (rx_len > 0u && rx == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tx_len == 0u && rx_len == 0u) return ESP_OK;
    if (tx_len == 0u) {
        return nocti_hal_i2c_read(h, dev_addr_7bit, rx, rx_len, timeout_ms);
    }
    if (rx_len == 0u) {
        return nocti_hal_i2c_write(h, dev_addr_7bit, tx, tx_len, timeout_ms);
    }

    esp_err_t err = require_sync_bus(h, "write_then_read");
    if (err != ESP_OK) return err;

    err = nocti_hal_i2c_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    i2c_master_dev_handle_t dev = NULL;
    err = dev_cache_get_or_add(h, dev_addr_7bit, &dev);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(
            dev,
            tx,
            tx_len,
            rx,
            rx_len,
            (int)pick_timeout_ms(h, timeout_ms));
    }

    nocti_hal_i2c_unlock(h);
    return err;
}

esp_err_t nocti_hal_i2c_write_reg8(nocti_hal_i2c_t *h,
                                   uint8_t dev_addr_7bit,
                                   uint8_t reg,
                                   const uint8_t *data,
                                   size_t len,
                                   uint32_t timeout_ms)
{
    if (len > 64u || (len > 0u && !data)) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[65];
    buffer[0] = reg;
    if (len > 0u) memcpy(&buffer[1], data, len);
    return nocti_hal_i2c_write(h,
                               dev_addr_7bit,
                               buffer,
                               len + 1u,
                               timeout_ms);
}

esp_err_t nocti_hal_i2c_read_reg8(nocti_hal_i2c_t *h,
                                  uint8_t dev_addr_7bit,
                                  uint8_t reg,
                                  uint8_t *data,
                                  size_t len,
                                  uint32_t timeout_ms)
{
    if (len > 0u && !data) return ESP_ERR_INVALID_ARG;
    return nocti_hal_i2c_write_then_read(h,
                                         dev_addr_7bit,
                                         &reg,
                                         1u,
                                         data,
                                         len,
                                         timeout_ms);
}

esp_err_t nocti_hal_i2c_write_reg16(nocti_hal_i2c_t *h,
                                    uint8_t dev_addr_7bit,
                                    uint16_t reg,
                                    const uint8_t *data,
                                    size_t len,
                                    uint32_t timeout_ms)
{
    if (len > 64u || (len > 0u && !data)) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[66];
    buffer[0] = (uint8_t)((reg >> 8u) & 0xFFu);
    buffer[1] = (uint8_t)(reg & 0xFFu);
    if (len > 0u) memcpy(&buffer[2], data, len);
    return nocti_hal_i2c_write(h,
                               dev_addr_7bit,
                               buffer,
                               len + 2u,
                               timeout_ms);
}

esp_err_t nocti_hal_i2c_read_reg16(nocti_hal_i2c_t *h,
                                   uint8_t dev_addr_7bit,
                                   uint16_t reg,
                                   uint8_t *data,
                                   size_t len,
                                   uint32_t timeout_ms)
{
    if (len > 0u && !data) return ESP_ERR_INVALID_ARG;

    uint8_t address[2] = {
        (uint8_t)((reg >> 8u) & 0xFFu),
        (uint8_t)(reg & 0xFFu),
    };
    return nocti_hal_i2c_write_then_read(h,
                                         dev_addr_7bit,
                                         address,
                                         sizeof(address),
                                         data,
                                         len,
                                         timeout_ms);
}

esp_err_t nocti_hal_i2c_read_u8(nocti_hal_i2c_t *h,
                                uint8_t dev_addr_7bit,
                                uint8_t reg,
                                uint8_t *val,
                                uint32_t timeout_ms)
{
    if (!val) return ESP_ERR_INVALID_ARG;
    return nocti_hal_i2c_read_reg8(h,
                                   dev_addr_7bit,
                                   reg,
                                   val,
                                   1u,
                                   timeout_ms);
}

esp_err_t nocti_hal_i2c_write_u8(nocti_hal_i2c_t *h,
                                 uint8_t dev_addr_7bit,
                                 uint8_t reg,
                                 uint8_t val,
                                 uint32_t timeout_ms)
{
    return nocti_hal_i2c_write_reg8(h,
                                    dev_addr_7bit,
                                    reg,
                                    &val,
                                    1u,
                                    timeout_ms);
}

static bool IRAM_ATTR nocti_hal_i2c_async_done_cb(
    i2c_master_dev_handle_t i2c_dev,
    const i2c_master_event_data_t *evt_data,
    void *arg)
{
    (void)i2c_dev;
    nocti_hal_i2c_async_reg8_t *ctx =
        (nocti_hal_i2c_async_reg8_t *)arg;
    if (!ctx || !evt_data) return false;

    ctx->last_event = evt_data->event;
    if (ctx->callback_core_id < 0) {
        ctx->callback_core_id = (int8_t)xPortGetCoreID();
    }
    nocti_time_us_t completion_us = 0;
    ctx->completion_time_valid =
        nocti_timebase_try_now_us(&completion_us);
    ctx->completion_us = ctx->completion_time_valid
        ? (int64_t)completion_us
        : 0;
    ctx->transaction_active = false;

    BaseType_t higher_priority_task_woken = pdFALSE;
    if (ctx->done_sem) {
        (void)xSemaphoreGiveFromISR(ctx->done_sem,
                                    &higher_priority_task_woken);
    }
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t async_event_to_err(i2c_master_event_t event)
{
    switch (event) {
        case I2C_EVENT_DONE:
            return ESP_OK;
        case I2C_EVENT_NACK:
            return ESP_ERR_INVALID_RESPONSE;
        case I2C_EVENT_TIMEOUT:
            return ESP_ERR_TIMEOUT;
        default:
            return ESP_FAIL;
    }
}

esp_err_t nocti_hal_i2c_async_reg8_init(nocti_hal_i2c_t *h,
                                        uint8_t dev_addr_7bit,
                                        nocti_hal_i2c_async_reg8_t *ctx)
{
    if (!handle_ok(h) || !ctx || !is_addr_7bit_valid(dev_addr_7bit)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (h->trans_queue_depth == 0u) return ESP_ERR_INVALID_STATE;
    if (h->async_context != NULL) return ESP_ERR_INVALID_STATE;

    memset(ctx, 0, sizeof(*ctx));
    ctx->init_core_id = (int8_t)xPortGetCoreID();
    ctx->callback_core_id = -1;
    ctx->done_sem = xSemaphoreCreateBinary();
    if (!ctx->done_sem) return ESP_ERR_NO_MEM;

    esp_err_t err = nocti_hal_i2c_lock(h, h->default_timeout_ms);
    if (err != ESP_OK) {
        vSemaphoreDelete(ctx->done_sem);
        memset(ctx, 0, sizeof(*ctx));
        return err;
    }

    i2c_master_dev_handle_t dev = NULL;
    err = dev_cache_get_or_add(h, dev_addr_7bit, &dev);
    if (err == ESP_OK) {
        const i2c_master_event_callbacks_t callbacks = {
            .on_trans_done = nocti_hal_i2c_async_done_cb,
        };
        err = i2c_master_register_event_callbacks(dev, &callbacks, ctx);
    }

    if (err == ESP_OK) {
        ctx->owner = h;
        ctx->dev_handle = (void *)dev;
        ctx->last_event = I2C_EVENT_ALIVE;
        ctx->registered = true;
        h->async_context = ctx;
    }

    nocti_hal_i2c_unlock(h);

    if (err != ESP_OK) {
        vSemaphoreDelete(ctx->done_sem);
        memset(ctx, 0, sizeof(*ctx));
    }
    return err;
}

esp_err_t nocti_hal_i2c_async_write_wait(
    nocti_hal_i2c_async_reg8_t *ctx,
    const uint8_t *data,
    size_t len,
    uint32_t timeout_ms,
    int64_t *out_completion_us)
{
    if (!ctx || !ctx->registered || !ctx->owner || !ctx->dev_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0u || len > sizeof(ctx->tx_buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->poisoned) return ESP_ERR_INVALID_STATE;

    nocti_hal_i2c_t *h = ctx->owner;
    const uint32_t to_ms = pick_timeout_ms(h, timeout_ms);
    esp_err_t err = nocti_hal_i2c_lock(h, to_ms);
    if (err != ESP_OK) return err;

    if (ctx->transaction_active) {
        nocti_hal_i2c_unlock(h);
        return ESP_ERR_INVALID_STATE;
    }

    // Copy before submit; the caller's stack buffer may disappear while the
    // ESP-IDF ISR is still shifting bytes.
    memcpy(ctx->tx_buffer, data, len);
    ctx->tx_length = len;
    (void)xSemaphoreTake(ctx->done_sem, 0);
    ctx->last_event = I2C_EVENT_ALIVE;
    ctx->completion_us = 0;
    ctx->completion_time_valid = false;
    ctx->transaction_active = true;

    err = i2c_master_transmit(
        (i2c_master_dev_handle_t)ctx->dev_handle,
        ctx->tx_buffer,
        ctx->tx_length,
        (int)to_ms);
    if (err != ESP_OK) {
        ctx->transaction_active = false;
        nocti_hal_i2c_unlock(h);
        return err;
    }

    TickType_t wait_ticks = pdMS_TO_TICKS(to_ms);
    if (wait_ticks == 0) wait_ticks = 1;
    if (xSemaphoreTake(ctx->done_sem, wait_ticks) != pdTRUE) {
        // i2c_master_bus_wait_all_done() is not a reliable barrier for the
        // direct-start async path in ESP-IDF 5.5.x. Reset the controller and
        // poison the context instead of reusing a buffer whose ownership is
        // uncertain. Higher-level recovery recreates the bus.
        (void)i2c_master_bus_reset(
            (i2c_master_bus_handle_t)h->bus_handle);
        ctx->transaction_active = false;
        ctx->poisoned = true;
        nocti_hal_i2c_unlock(h);
        return ESP_ERR_TIMEOUT;
    }

    if (out_completion_us) {
        *out_completion_us = ctx->completion_us;
    }
    err = async_event_to_err(ctx->last_event);
    if (err == ESP_OK && !ctx->completion_time_valid) {
        // A successful bus transaction without a valid master-clock timestamp
        // cannot be used as a physical optical edge.
        err = ESP_ERR_INVALID_STATE;
    }
    nocti_hal_i2c_unlock(h);
    return err;
}

esp_err_t nocti_hal_i2c_async_reg8_write_wait(
    nocti_hal_i2c_async_reg8_t *ctx,
    uint8_t reg,
    uint8_t value,
    uint32_t timeout_ms,
    int64_t *out_completion_us)
{
    const uint8_t transaction[2] = {reg, value};
    return nocti_hal_i2c_async_write_wait(ctx,
                                          transaction,
                                          sizeof(transaction),
                                          timeout_ms,
                                          out_completion_us);
}

esp_err_t nocti_hal_i2c_async_reg8_deinit(
    nocti_hal_i2c_async_reg8_t *ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    if (!ctx->registered) {
        if (ctx->done_sem) vSemaphoreDelete(ctx->done_sem);
        memset(ctx, 0, sizeof(*ctx));
        return ESP_OK;
    }

    nocti_hal_i2c_t *h = ctx->owner;
    if (!handle_ok(h)) return ESP_ERR_INVALID_STATE;

    esp_err_t err = nocti_hal_i2c_lock(h, h->default_timeout_ms);
    if (err != ESP_OK) return err;

    if (ctx->transaction_active || ctx->poisoned) {
        (void)i2c_master_bus_reset(
            (i2c_master_bus_handle_t)h->bus_handle);
        ctx->transaction_active = false;
    }

    const i2c_master_event_callbacks_t callbacks = {
        .on_trans_done = NULL,
    };
    err = i2c_master_register_event_callbacks(
        (i2c_master_dev_handle_t)ctx->dev_handle, &callbacks, NULL);
    if (err == ESP_OK && h->async_context == ctx) {
        h->async_context = NULL;
    }

    nocti_hal_i2c_unlock(h);

    if (err == ESP_OK) {
        vSemaphoreDelete(ctx->done_sem);
        memset(ctx, 0, sizeof(*ctx));
    }
    return err;
}
