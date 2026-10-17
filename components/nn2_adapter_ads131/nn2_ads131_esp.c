#include "nn2_adapter_ads131.h"

#include <string.h>

#include "sdkconfig.h"

#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
#error CONFIG_SPIRAM must stay off on the ADS131 SPI path
#endif
#if defined(CONFIG_ESPTOOLPY_OCT_FLASH) && CONFIG_ESPTOOLPY_OCT_FLASH
#error octal flash must stay off on the ADS131 SPI path
#endif
#if defined(CONFIG_SPIRAM_MODE_OCT) && CONFIG_SPIRAM_MODE_OCT
#error octal PSRAM must stay off on the ADS131 SPI path
#endif

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nn2_rtos_config.h"

static const char *TAG = "nn2_ads131";

_Static_assert(sizeof(StackType_t) == 1u,
               "Xtensa stack element is one byte; depth is the byte count");

/* The DRDY handler is left in flash. IRAM at the previous head is
   16384/16384 with nothing remaining, so IRAM_ATTR would not link.
   The handler only notifies the task. It does not touch SPI. */
typedef struct {
    nn2_ads131_pins_t pins;
    nn2_ads131_logic_t logic;
    nn2_adc_raw_frame_t storage[NN2_ADS131_RING_DEPTH_DEFAULT];
    TaskHandle_t task;
    StaticTask_t tcb;
    StackType_t stack[NN2_RTOS_STACK_ADS131_BYTES];
    spi_device_handle_t spi;
    bool spi_ready;
    bool isr_armed;
    bool task_created;
} nn2_ads131_dev_t;

static nn2_ads131_dev_t s_dev;

static void nn2_ads131_drdy_isr(void *arg)
{
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t wake = pdFALSE;

    vTaskNotifyGiveFromISR(task, &wake);
    if (wake == pdTRUE) {
        portYIELD_FROM_ISR(wake);
    }
}

static nn2_status_t nn2_from_esp(esp_err_t err)
{
    return err == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

static nn2_status_t nn2_ads131_config_pins(const nn2_ads131_pins_t *pins)
{
    const nn2_ads131_sig_t signals[] = {
        NN2_ADS131_SIG_CS,   NN2_ADS131_SIG_SCLK, NN2_ADS131_SIG_MOSI,
        NN2_ADS131_SIG_MISO, NN2_ADS131_SIG_DRDY, NN2_ADS131_SIG_SYNC};
    const int gpios[] = {pins->cs,   pins->sclk,      pins->mosi,
                         pins->miso, pins->drdy,      pins->sync_reset};
    size_t i;

    for (i = 0u; i < sizeof(signals) / sizeof(signals[0]); i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << (unsigned)gpios[i],
            .mode = nn2_ads131_pin_dir(signals[i]) == NN2_PIN_INPUT
                        ? GPIO_MODE_INPUT
                        : GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) {
            return nn2_from_esp(err);
        }
    }
    return nn2_from_esp(gpio_set_level(pins->sync_reset, 0));
}

static nn2_status_t port_safe_state(void *ctx)
{
    nn2_ads131_dev_t *dev = ctx;
    nn2_status_t status;

    if (dev == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    status = nn2_ads131_logic_safe_state(&dev->logic);
    if (status != NN2_OK) {
        return status;
    }
    if (dev->isr_armed) {
        (void)gpio_isr_handler_remove(dev->pins.drdy);
        dev->isr_armed = false;
    }
    status = nn2_ads131_config_pins(&dev->pins);
    if (status != NN2_OK) {
        return status;
    }
    return nn2_from_esp(gpio_set_level(dev->pins.sync_reset, 0));
}

static nn2_status_t port_init(void *ctx, const nn2_adc_config_t *cfg)
{
    nn2_ads131_dev_t *dev = ctx;

    if (dev == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    return nn2_ads131_logic_init(&dev->logic, cfg, dev->storage,
                                 NN2_ADS131_RING_DEPTH_DEFAULT);
}

static nn2_status_t nn2_ads131_spi_init(nn2_ads131_dev_t *dev)
{
    spi_bus_config_t bus = {
        .mosi_io_num = dev->pins.mosi,
        .miso_io_num = dev->pins.miso,
        .sclk_io_num = dev->pins.sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = NN2_ADS131_FRAME_BYTES,
    };
    spi_device_interface_config_t iface = {
        .clock_speed_hz = (int)dev->logic.cfg.spi_hz,
        .mode = 1,
        .spics_io_num = dev->pins.cs,
        .queue_size = 1,
    };
    esp_err_t err;

    if (dev->spi_ready) {
        return NN2_OK;
    }
    if (dev->logic.cfg.spi_hz > NN2_ADS131_SPI_HZ_MAX) {
        return NN2_ERR_INVALID_ARG;
    }
    err = spi_bus_initialize((spi_host_device_t)dev->pins.spi_host, &bus,
                             SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return nn2_from_esp(err);
    }
    err = spi_bus_add_device((spi_host_device_t)dev->pins.spi_host, &iface,
                             &dev->spi);
    if (err != ESP_OK) {
        return nn2_from_esp(err);
    }
    dev->spi_ready = true;
    return NN2_OK;
}

static nn2_status_t port_start(void *ctx)
{
    nn2_ads131_dev_t *dev = ctx;
    nn2_status_t status;
    esp_err_t err;

    if (dev == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (nn2_ads131_logic_allow_task(&dev->logic) != NN2_OK || dev->task == 0) {
        return NN2_ERR_STATE;
    }
    status = nn2_ads131_spi_init(dev);
    if (status != NN2_OK) {
        return status;
    }
    status = nn2_from_esp(gpio_set_level(dev->pins.sync_reset, 0));
    if (status != NN2_OK) {
        return status;
    }
    esp_rom_delay_us(NN2_ADS131_RESET_LOW_US);
    status = nn2_from_esp(gpio_set_level(dev->pins.sync_reset, 1));
    if (status != NN2_OK) {
        return status;
    }
    esp_rom_delay_us(NN2_ADS131_TREGACQ_US);

    /* Active-low falling edge. Pending Agent 04 datasheet confirmation. */
    err = gpio_set_intr_type(dev->pins.drdy, GPIO_INTR_NEGEDGE);
    if (err != ESP_OK) {
        (void)gpio_set_level(dev->pins.sync_reset, 0);
        return nn2_from_esp(err);
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        (void)gpio_set_level(dev->pins.sync_reset, 0);
        return nn2_from_esp(err);
    }
    err = gpio_isr_handler_add(dev->pins.drdy, nn2_ads131_drdy_isr, dev->task);
    if (err != ESP_OK) {
        (void)gpio_set_level(dev->pins.sync_reset, 0);
        return nn2_from_esp(err);
    }
    dev->isr_armed = true;
    return nn2_ads131_logic_begin_run(&dev->logic);
}

static nn2_status_t port_stop(void *ctx)
{
    return port_safe_state(ctx);
}

static void nn2_ads131_read_once(nn2_ads131_dev_t *dev)
{
    uint8_t tx[NN2_ADS131_FRAME_BYTES];
    uint8_t rx[NN2_ADS131_FRAME_BYTES];
    spi_transaction_t trans;
    esp_err_t err;

    memset(tx, 0, sizeof(tx));
    memset(&trans, 0, sizeof(trans));
    trans.length = (size_t)NN2_ADS131_FRAME_BYTES * 8u;
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;
    err = spi_device_polling_transmit(dev->spi, &trans);
    if (err != ESP_OK) {
        (void)nn2_ads131_logic_on_spi_error(&dev->logic);
        return;
    }
    (void)nn2_ads131_logic_on_frame(&dev->logic, rx,
                                    (uint64_t)esp_timer_get_time());
}

static void nn2_ads131_task(void *arg)
{
    nn2_ads131_dev_t *dev = arg;

    for (;;) {
        uint32_t ticks = nn2_ads131_notify_timeout_ticks(
            dev->logic.cfg.data_rate_hz, (uint32_t)configTICK_RATE_HZ);
        uint32_t got;

        if (ticks == 0u) {
            ticks = 1u;
        }
        got = ulTaskNotifyTake(pdFALSE, ticks);
        if (!dev->logic.running) {
            continue;
        }
        if (got == 0u) {
            (void)nn2_ads131_logic_on_timeout(&dev->logic);
            continue;
        }
        nn2_ads131_read_once(dev);
    }
}

static nn2_status_t port_read_raw(void *ctx, nn2_adc_raw_frame_t *out)
{
    nn2_ads131_dev_t *dev = ctx;

    if (dev == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    return nn2_ads131_logic_pop(&dev->logic, out);
}

static nn2_status_t port_get_counters(void *ctx, nn2_adc_counters_t *out)
{
    nn2_ads131_dev_t *dev = ctx;

    if (dev == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    return nn2_ads131_logic_counters(&dev->logic, out);
}

nn2_status_t nn2_adapter_ads131_bind(nn2_port_adc_t *port,
                                     const nn2_ads131_pins_t *pins)
{
    if (port == 0 || pins == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (s_dev.task_created) {
        return NN2_ERR_STATE;
    }
    if (nn2_ads131_validate_pins(pins) != NN2_OK) {
        return NN2_ERR_INVALID_ARG;
    }
    memset(&s_dev, 0, sizeof(s_dev));
    s_dev.pins = *pins;
    port->ctx = &s_dev;
    port->safe_state = port_safe_state;
    port->init = port_init;
    port->start = port_start;
    port->stop = port_stop;
    port->read_raw = port_read_raw;
    port->get_counters = port_get_counters;
    return NN2_OK;
}

nn2_status_t nn2_adapter_ads131_create_task(void *ctx)
{
    nn2_ads131_dev_t *dev = ctx;
    nn2_status_t status;

    if (dev != &s_dev) {
        return NN2_ERR_INVALID_ARG;
    }
    status = nn2_ads131_logic_allow_task(&dev->logic);
    if (status != NN2_OK) {
        return status;
    }
    if (dev->task_created) {
        return NN2_ERR_STATE;
    }
    dev->task = xTaskCreateStaticPinnedToCore(
        nn2_ads131_task, NN2_TASK_NAME_ADS131, NN2_RTOS_STACK_ADS131_BYTES, dev,
        NN2_RTOS_PRIO_ADS131_TBD35, dev->stack, &dev->tcb,
        NN2_RTOS_CORE_ADS131_TBD35);
    if (dev->task == 0) {
        return NN2_ERR_STATE;
    }
    dev->task_created = true;
    ESP_LOGI(TAG, "task created; SYNC/RESET held low; sampling not started");
    return NN2_OK;
}
