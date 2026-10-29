#include "nn2_adapter_ads131.h"
#include "nn2_rtos_config.h"
#include "nn2_board_v1.h"
#include "nn2_ads131_build_guard.h"
#include "nn2_ads131_events.h"
#include <string.h>
#include "esp_log.h"
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
    esp_err_t startup_spi_error;
    bool startup_reported, runtime_reported, bus_acquired;
    TaskHandle_t task, caller;
    StaticTask_t task_storage;
    StackType_t stack[NN2_RTOS_ADS131_STACK_BYTES];
    QueueHandle_t commands, replies;
    StaticQueue_t commands_control, replies_control;
    uint8_t command_storage[sizeof(request_t)], reply_storage[sizeof(reply_t)];
    uint32_t next_id;
    nn2_ads131_events_t events;
} device_t;
_Static_assert(sizeof(device_t) <= 12288u, "Review internal static memory budget");
static device_t device;
static portMUX_TYPE edge_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t drdy_latch(void *ctx, uint32_t *serial)
{
    device_t *d = ctx;
    portENTER_CRITICAL(&edge_lock);
    uint64_t stamp = d->events.latest_us; *serial = d->events.serial;
    portEXIT_CRITICAL(&edge_lock);
    return stamp;
}
static uint32_t take_events(void *ctx, nn2_ads131_event_t *first, uint32_t *dropped)
{
    device_t *d = ctx;
    portENTER_CRITICAL(&edge_lock);
    uint32_t count = nn2_ads131_events_take(&d->events, first, dropped);
    portEXIT_CRITICAL(&edge_lock);
    return count;
}
static void capture_events(device_t *d, bool enabled)
{
    portENTER_CRITICAL(&edge_lock);
    d->events.enabled = enabled;
    if (!enabled) nn2_ads131_events_clear(&d->events);
    portEXIT_CRITICAL(&edge_lock);
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
#include "nn2_ads131_transport.inc"
static nn2_status_t reset(void *ctx, bool high)
{
    device_t *d = ctx;
    if (!high) capture_events(d, false);
    nn2_status_t result = mapped(d, gpio_set_level(d->board.reset, high ? 1 : 0));
    if (!high && result == NN2_OK) release_bus(d);
    return result;
}
static nn2_status_t sync_adc(void *ctx)
{
    device_t *d = ctx; esp_err_t low, high;
    /* Publish capture before unmasking the first post-SYNC interrupt. No
     * preemptible release-to-enable gap may silently fill the device FIFO.
     * Requirement: entire critical section <=10us; WCET is NOT RUN. */
    portENTER_CRITICAL(&edge_lock);
    d->events.enabled = false; nn2_ads131_events_clear(&d->events);
    low = gpio_set_level(d->board.reset, 0);
    esp_rom_delay_us(1u);
    high = gpio_set_level(d->board.reset, 1);
    d->events.enabled = low == ESP_OK && high == ESP_OK;
    portEXIT_CRITICAL(&edge_lock);
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
static void drdy_isr(void *arg)
{
    device_t *d = arg;
    BaseType_t wake = pdFALSE;
    portENTER_CRITICAL_ISR(&edge_lock);
    bool enabled = d->events.enabled;
    if (enabled) nn2_ads131_events_push(&d->events, (uint64_t)esp_timer_get_time());
    portEXIT_CRITICAL_ISR(&edge_lock);
    if (enabled) xTaskNotifyFromISR(d->task, 1u, eSetBits, &wake);
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
        .command_bits = 0, .address_bits = 0, .dummy_bits = 0,
        .duty_cycle_pos = 128, .cs_ena_pretrans = 1, .cs_ena_posttrans = 1,
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
    if (err == ESP_OK) err = gpio_isr_handler_add(d->board.drdy, drdy_isr, d);
    if (err != ESP_OK) {
        (void)spi_bus_remove_device(d->spi); (void)spi_bus_free((spi_host_device_t)d->board.host);
        return mapped(d, err);
    }
    ESP_LOGI("ads131_transport", "candidate=v4-poll-events mode=1 hz=%lu bits=144 cs_pre=1 cs_post=1 polling=1 dma=0",
        (unsigned long)d->board.config.spi_hz);
    d->hardware_ready = true; return NN2_OK;
}
#include "nn2_ads131_control.inc"
/* Fault-only, after acquisition has stopped. No successful/sample-path logs.
 * Snapshot is frozen before reset; an incomplete SPI transfer has no valid RX. */
static void report_startup_failure(device_t *d)
{
    const nn2_ads131_startup_failure_t *f = &d->logic.startup_failure;
    if (!f->valid || d->startup_reported) return;
    d->startup_reported = true;
    ESP_LOGE("ads131_startup", "stage=%u transaction=%lu addr=%02x cmd=%04x previous=%04x fault=%u result=%d cleanup=%d spi=%d tx_valid=%u rx_valid=%u",
        (unsigned)f->stage, (unsigned long)f->transaction, (unsigned)f->address,
        (unsigned)f->command, (unsigned)f->previous_command, (unsigned)f->fault,
        (int)f->result, (int)f->cleanup_result, (int)d->startup_spi_error,
        (unsigned)f->tx_valid, (unsigned)f->rx_valid);
    if (f->tx_valid) ESP_LOG_BUFFER_HEX_LEVEL("ads131_startup_tx", f->tx, sizeof(f->tx), ESP_LOG_ERROR);
    if (f->rx_valid) {
        ESP_LOG_BUFFER_HEX_LEVEL("ads131_startup_rx", f->rx, sizeof(f->rx), ESP_LOG_ERROR);
        ESP_LOGE("ads131_startup", "crc_calculated=%04x crc_received=%04x crc_match=%u response_padding=%02x crc_padding=%02x",
            (unsigned)f->crc_calculated, (unsigned)f->crc_received, (unsigned)f->crc_match,
            (unsigned)f->response_padding, (unsigned)f->crc_padding);
    }
}
static void report_runtime_failure(device_t *d)
{
    const nn2_ads131_runtime_failure_t *f = &d->logic.runtime_failure;
    if (!f->valid || d->runtime_reported) return;
    d->runtime_reported = true;
    ESP_LOGE("ads131_runtime", "fault=%u pending=%lu event_dropped=%lu expected_serial=%lu event_serial=%lu observed_serial=%lu edge_us=%llu at_us=%llu last_service_us=%llu cleanup=%d crc_bad=%lu",
        (unsigned)f->fault, (unsigned long)f->pending, (unsigned long)f->dropped,
        (unsigned long)f->expected_serial, (unsigned long)f->event_serial,
        (unsigned long)f->observed_serial, (unsigned long long)f->edge_us,
        (unsigned long long)f->at_us, (unsigned long long)f->last_service_us,
        (int)f->cleanup_result, (unsigned long)d->logic.observation.crc_bad);
}
static void owner(void *arg)
{
    device_t *d = arg;
    d->task = xTaskGetCurrentTaskHandle();
    if (setup(d) != NN2_OK) (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_SPI);
    d->watchdog = esp_task_wdt_add(NULL) == ESP_OK;
    if (!d->watchdog) (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_WATCHDOG);
    for (;;) {
        uint32_t notified = 0u;
        (void)xTaskNotifyWait(0u, UINT32_MAX, &notified, 1u);
        owner_iteration(d, notified & 0x7fffffffu);
        report_startup_failure(d);
        report_runtime_failure(d);
    }
}
nn2_status_t nn2_adapter_ads131_create(const nn2_ads131_board_t *board, nn2_port_adc_stream_t *out)
{
    device_t *d = &device;
    if (board != &nn2_board_v1_adc || !out || nn2_ads131_validate(&board->config) != NN2_OK) return NN2_ERR_INVALID_ARG;
    if (d->created) return NN2_ERR_INVALID_STATE;
    d->board = *board; d->caller = xTaskGetCurrentTaskHandle();
    nn2_ads131_io_t io = { .ctx=d, .transfer=transfer, .reset=reset, .sync=sync_adc,
        .delay_us=delay_us, .wait_ready=wait_ready, .now_us=now_us, .drdy_latch=drdy_latch,
        .take_events=take_events };
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
