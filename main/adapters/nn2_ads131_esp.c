#include "nn2_adapter_ads131.h"
#include "nn2_rtos_config.h"
#include "nn2_board_v1.h"
#include "nn2_ads131_build_guard.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

enum { CMD_INIT, CMD_START, CMD_STOP, CMD_READ, CMD_OBSERVE };
typedef struct { uint32_t id, op; uint64_t deadline; } request_t;
typedef struct {
    uint32_t id; nn2_status_t result;
    nn2_core_adc_record_t record;
    nn2_core_adc_observation_t observation;
} reply_t;
typedef struct {
    nn2_ads131_board_t board;
    nn2_ads131_logic_t logic;
    spi_device_handle_t spi;
    spi_transaction_t transaction;
    _Alignas(4) uint8_t tx[NN2_ADS131_BYTES];
    _Alignas(4) uint8_t rx[NN2_ADS131_BYTES];
    bool created, poisoned, hardware_ready, watchdog;
    int32_t last_esp_error;
    TaskHandle_t task, caller;
    StaticTask_t task_storage;
    StackType_t stack[NN2_RTOS_ADS131_STACK_BYTES];
    QueueHandle_t commands, replies;
    StaticQueue_t commands_control, replies_control;
    uint8_t command_storage[sizeof(request_t)], reply_storage[sizeof(reply_t)];
    uint32_t next_id;
} device_t;
_Static_assert(sizeof(device_t) <= 14336u, "Review internal static memory budget");
static device_t device;
static portMUX_TYPE sync_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE edge_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t edge_time;
static uint32_t edge_serial;
static uint64_t drdy_latch(void *ctx, uint32_t *serial)
{
    (void)ctx;
    portENTER_CRITICAL(&edge_lock);
    uint64_t stamp = edge_time; *serial = edge_serial;
    portEXIT_CRITICAL(&edge_lock);
    return stamp;
}
_Static_assert(sizeof(StackType_t) == 1u, "IDF Xtensa stack depth is bytes");
_Static_assert(sizeof(nn2_core_adc_record_t) <= 56u, "Review queue memory budget if record grows");
static TickType_t ticks(uint32_t ms)
{
    return (TickType_t)(((uint64_t)ms * configTICK_RATE_HZ + 999u) / 1000u);
}
static nn2_status_t mapped(device_t *d, esp_err_t error)
{
    d->last_esp_error = error;
    return error == ESP_OK ? NN2_OK : (error == ESP_ERR_TIMEOUT ? NN2_ERR_TIMEOUT : NN2_ERR_IO);
}
static uint64_t now_us(void *ctx) { (void)ctx; return (uint64_t)esp_timer_get_time(); }
static void delay_us(void *ctx, uint32_t us) { (void)ctx; esp_rom_delay_us(us); }
static nn2_status_t reset(void *ctx, bool high)
{
    device_t *d = ctx; return mapped(d, gpio_set_level(d->board.reset, high ? 1 : 0));
}
static nn2_status_t sync_adc(void *ctx)
{
    device_t *d = ctx; esp_err_t low, high;
    /* Requirement: entire critical section <=10us; WCET is NOT RUN. */
    portENTER_CRITICAL(&sync_lock);
    low = gpio_set_level(d->board.reset, 0);
    esp_rom_delay_us(1u);
    high = gpio_set_level(d->board.reset, 1);
    portEXIT_CRITICAL(&sync_lock);
    return mapped(d, low != ESP_OK ? low : high);
}
static nn2_status_t wait_ready(void *ctx, uint32_t ms)
{
    device_t *d = ctx; uint64_t end = now_us(ctx) + (uint64_t)ms * 1000u;
    while (gpio_get_level(d->board.drdy) != 0) {
        if (now_us(ctx) >= end) return NN2_ERR_TIMEOUT;
        vTaskDelay(1u);
    }
    return NN2_OK;
}
static nn2_status_t transfer(void *ctx, const uint8_t *tx, uint8_t *rx)
{
    device_t *d = ctx; spi_transaction_t *completed = NULL; esp_err_t err;
    if (!d->hardware_ready || d->poisoned) return NN2_ERR_IO;
    memcpy(d->tx, tx, sizeof(d->tx));
    memset(&d->transaction, 0, sizeof(d->transaction));
    d->transaction.length = NN2_ADS131_BYTES * 8u;
    d->transaction.tx_buffer = d->tx; d->transaction.rx_buffer = d->rx;
    err = spi_device_queue_trans(d->spi, &d->transaction, ticks(NN2_RTOS_ADS131_SPI_WAIT_MS));
    if (err != ESP_OK) return mapped(d, err);
    err = spi_device_get_trans_result(d->spi, &completed, ticks(NN2_RTOS_ADS131_SPI_WAIT_MS));
    if (err != ESP_OK || completed != &d->transaction) {
        /* Pending driver's storage stays alive and is never reused until reboot. */
        d->poisoned = true; return mapped(d, err == ESP_OK ? ESP_FAIL : err);
    }
    memcpy(rx, d->rx, sizeof(d->rx)); return NN2_OK;
}
static void drdy_isr(void *arg)
{
    BaseType_t wake = pdFALSE;
    portENTER_CRITICAL_ISR(&edge_lock);
    edge_time = (uint64_t)esp_timer_get_time(); ++edge_serial;
    portEXIT_CRITICAL_ISR(&edge_lock);
    xTaskNotifyFromISR((TaskHandle_t)arg, 0u, eIncrement, &wake);
    if (wake) portYIELD_FROM_ISR();
}
static nn2_status_t setup(device_t *d)
{
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << (unsigned)d->board.reset) | (1ULL << (unsigned)d->board.cs),
        .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << (unsigned)d->board.drdy) | (1ULL << (unsigned)d->board.miso),
        .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_DISABLE
    };
    spi_bus_config_t bus = {
        .mosi_io_num = d->board.mosi, .miso_io_num = d->board.miso,
        .sclk_io_num = d->board.sclk, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = NN2_ADS131_BYTES
    };
    spi_device_interface_config_t spi = {
        .clock_speed_hz = (int)d->board.config.spi_hz, .mode = 1,
        .spics_io_num = d->board.cs, .queue_size = 1
    };
    esp_err_t err;
    /* Preload output latches before enabling outputs; no emitter pins. */
    if (gpio_set_level(d->board.reset, 0) != ESP_OK || gpio_set_level(d->board.cs, 1) != ESP_OK ||
        gpio_config(&outputs) != ESP_OK || gpio_config(&inputs) != ESP_OK) return NN2_ERR_IO;
    err = spi_bus_initialize((spi_host_device_t)d->board.host, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return mapped(d, err); /* Dedicated bus; do not accept already-used. */
    err = spi_bus_add_device((spi_host_device_t)d->board.host, &spi, &d->spi);
    if (err != ESP_OK) { (void)spi_bus_free((spi_host_device_t)d->board.host); return mapped(d, err); }
    err = gpio_install_isr_service(0);
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK; /* Existing GPIO ISR service. */
    if (err == ESP_OK) err = gpio_set_intr_type(d->board.drdy, GPIO_INTR_NEGEDGE);
    if (err == ESP_OK) err = gpio_isr_handler_add(d->board.drdy, drdy_isr, xTaskGetCurrentTaskHandle());
    if (err != ESP_OK) {
        (void)spi_bus_remove_device(d->spi); (void)spi_bus_free((spi_host_device_t)d->board.host);
        return mapped(d, err);
    }
    d->hardware_ready = true; return NN2_OK;
}
#include "nn2_ads131_control.inc"
static void owner(void *arg)
{
    device_t *d = arg;
    if (setup(d) != NN2_OK) (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_SPI);
    d->watchdog = esp_task_wdt_add(NULL) == ESP_OK;
    if (!d->watchdog) (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_WATCHDOG);
    for (;;) {
        uint32_t notified = 0u;
        (void)xTaskNotifyWait(0u, UINT32_MAX, &notified, 1u);
        owner_iteration(d, notified & 0x7fffffffu);
    }
}
nn2_status_t nn2_adapter_ads131_create(const nn2_ads131_board_t *board, nn2_port_adc_stream_t *out)
{
    device_t *d = &device;
    if (board != &nn2_board_v1_adc || !out || nn2_ads131_validate(&board->config) != NN2_OK) return NN2_ERR_INVALID_ARG;
    if (d->created) return NN2_ERR_INVALID_STATE;
    d->board = *board; d->caller = xTaskGetCurrentTaskHandle();
    nn2_ads131_io_t io = { .ctx=d, .transfer=transfer, .reset=reset, .sync=sync_adc,
        .delay_us=delay_us, .wait_ready=wait_ready, .now_us=now_us, .drdy_latch=drdy_latch };
    nn2_ads131_logic_bind(&d->logic, io);
    d->commands = xQueueCreateStatic(1u, sizeof(request_t), d->command_storage, &d->commands_control);
    d->replies = xQueueCreateStatic(1u, sizeof(reply_t), d->reply_storage, &d->replies_control);
    if (!d->commands || !d->replies) return NN2_ERR_IO;
    d->task = xTaskCreateStaticPinnedToCore(owner, "nn2_ads131", NN2_RTOS_ADS131_STACK_BYTES,
        d, NN2_RTOS_ADS131_PRIORITY, d->stack, &d->task_storage, NN2_RTOS_ADS131_CORE);
    if (!d->task) return NN2_ERR_IO;
    d->created = true;
    *out = (nn2_port_adc_stream_t){d, init_port, start_port, stop_port, read_port, observe_port};
    return NN2_OK;
}
