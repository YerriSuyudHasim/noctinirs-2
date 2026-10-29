#include "nn2_adapter_ads131.h"
#include "nn2_rtos_config.h"
#include "nn2_board_v1.h"
#include "nn2_ads131_build_guard.h"
#include "nn2_ads131_events.h"
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#if !CONFIG_ESP_TIMER_IN_IRAM || !CONFIG_FREERTOS_IN_IRAM || !CONFIG_SPI_MASTER_IN_IRAM
#error ADC IRAM interrupt requires timer, FreeRTOS and SPI master IRAM configuration
#endif

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
    QueueHandle_t raw;
    StaticQueue_t raw_control;
    uint8_t raw_storage[256u * sizeof(nn2_core_adc_record_t)];
    atomic_uint consumed, read_late;
    uint32_t stop_discarded;
    atomic_bool stream_faulted;
} device_t;
/* V4-sized 128-event / 256-record rings plus existing task/control/history.
 * Explicit 36-KiB diagnostic static ceiling; linked RAM/IRAM remains to be checked. */
_Static_assert(sizeof(device_t) <= 36864u, "Review internal static memory budget");
static DRAM_ATTR device_t device;
static DRAM_ATTR portMUX_TYPE edge_lock = portMUX_INITIALIZER_UNLOCKED;
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
    if (!high) {
        if (gpio_intr_disable(d->board.drdy) != ESP_OK) return NN2_ERR_IO;
        capture_events(d, false);
    }
    nn2_status_t result = mapped(d, gpio_set_level(d->board.reset, high ? 1 : 0));
    if (!high && result == NN2_OK) release_bus(d);
    return result;
}
static nn2_status_t sync_adc(void *ctx)
{
    device_t *d = ctx;
    /* V4: mask the pin interrupt while clearing the event ring and issuing
     * SYNC, then enable capture before unmasking. No ISR work during SYNC. */
    if (gpio_intr_disable(d->board.drdy) != ESP_OK) return NN2_ERR_IO;
    capture_events(d, false);
    if (gpio_set_level(d->board.reset, 0) != ESP_OK) return NN2_ERR_IO;
    esp_rom_delay_us(5u);
    if (gpio_set_level(d->board.reset, 1) != ESP_OK) return NN2_ERR_IO;
    capture_events(d, true);
    esp_err_t err = gpio_intr_enable(d->board.drdy);
    if (err != ESP_OK) capture_events(d, false);
    return mapped(d, err);
}
static nn2_status_t wait_ready(void *ctx, uint32_t ms)
{
    device_t *d = ctx;
    uint32_t baseline, serial;
    uint64_t end = now_us(ctx) + (uint64_t)ms * 1000u;
    (void)drdy_latch(d, &baseline);
    /* Pulse DRDY cannot be reliably sampled by a tick-spaced GPIO poll.
     * Temporarily capture interrupts; initialization does not publish samples. */
    capture_events(d, true);
    esp_err_t err = gpio_intr_enable(d->board.drdy);
    nn2_status_t result = mapped(d, err);
    if (err == ESP_OK) {
        result = NN2_ERR_TIMEOUT;
        do {
            (void)drdy_latch(d, &serial);
            if (serial != baseline) { result = NN2_OK; break; }
            vTaskDelay(1u);
        } while (now_us(ctx) < end);
    }
    err = gpio_intr_disable(d->board.drdy);
    capture_events(d, false);
    return err == ESP_OK ? result : mapped(d, err);
}
static void IRAM_ATTR drdy_isr(void *arg)
{
    device_t *d = arg;
    uint64_t entered_us = (uint64_t)esp_timer_get_time();
    BaseType_t wake = pdFALSE;
    /* Keep the complete ISR call graph in IRAM. Inline scalar ring writes
     * avoid a non-inlined helper or a compiler-generated structure memcpy. */
    portENTER_CRITICAL_ISR(&edge_lock);
    bool enabled = d->events.enabled;
    if (enabled) {
        nn2_ads131_events_t *q = &d->events;
        uint64_t stamp = entered_us;
        q->latest_us = stamp; ++q->serial;
        if (q->head - q->tail == NN2_ADS131_EVENT_CAPACITY) ++q->dropped;
        else {
            nn2_ads131_event_t *slot = &q->items[q->head % NN2_ADS131_EVENT_CAPACITY];
            slot->time_us = stamp; slot->serial = q->serial;
            uint64_t elapsed = (uint64_t)esp_timer_get_time() - stamp;
            slot->isr_publish_us = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
            ++q->head;
        }
    }
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
        .clock_source = SPI_CLK_SRC_DEFAULT, .input_delay_ns = 0, .flags = 0,
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
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    /* An existing service has unknown allocation flags: do not silently
     * claim IRAM safety. This ADC owner installs the sole GPIO ISR service. */
    if (err == ESP_OK) err = gpio_set_intr_type(d->board.drdy, GPIO_INTR_NEGEDGE);
    if (err == ESP_OK) err = gpio_isr_handler_add(d->board.drdy, drdy_isr, d);
    if (err == ESP_OK) err = gpio_intr_disable(d->board.drdy);
    if (err != ESP_OK) {
        (void)spi_bus_remove_device(d->spi); (void)spi_bus_free((spi_host_device_t)d->board.host);
        return mapped(d, err);
    }
    ESP_LOGI("ads131_transport", "candidate=v4-pulse-sync3 timing=split4 mode_reg=0111 clock_reg=0f0a cfg_reg=0600 cpu_mhz=%u mode=1 hz=%lu bits=144 cs_pre=1 cs_post=1 polling=1 dma=0",
        (unsigned)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, (unsigned long)d->board.config.spi_hz);
    d->hardware_ready = true; return NN2_OK;
}
static nn2_status_t publish(void *ctx, const nn2_core_adc_record_t *record, uint32_t *depth)
{
    device_t *d = ctx;
    if (xQueueSend(d->raw, record, 0u) != pdTRUE) return NN2_ERR_IO;
    *depth = (uint32_t)uxQueueMessagesWaiting(d->raw);
    return NN2_OK;
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
static void report_latency_trace(const char *role, const nn2_ads131_trace_t *t)
{
    ESP_LOGI("ads131_latency", "role=%s serial=%lu edge_us=%llu owner_resume_us=%llu service_entry_us=%llu event_pop_us=%llu isr_publish_us=%lu resume_before_edge=%u",
        role, (unsigned long)t->serial, (unsigned long long)t->edge_us,
        (unsigned long long)t->owner_resume_us, (unsigned long long)t->service_entry_us,
        (unsigned long long)t->event_pop_us, (unsigned long)t->isr_publish_us,
        (unsigned)(t->owner_resume_us < t->edge_us));
    ESP_LOGI("ads131_latency_spi", "role=%s serial=%lu transfer_entry_us=%llu driver_call_us=%llu driver_return_us=%llu transfer_end_us=%llu crc_us=%lu period_us=%lu pending=%lu status=%04x phase=%u",
        role, (unsigned long)t->serial, (unsigned long long)t->spi_begin_us,
        (unsigned long long)t->driver_start_us, (unsigned long long)t->driver_started_us,
        (unsigned long long)t->spi_end_us, (unsigned long)t->crc_us,
        (unsigned long)t->period_us, (unsigned long)t->pending,
        (unsigned)t->status, (unsigned)t->phase);
}
static void report_latency_events(device_t *d)
{
    ESP_LOGI("ads131_latency_summary", "session=%lu seen=%lu captured=%lu capacity=%u thresholds_us=start:100,service:30,transport:80,period:188",
        (unsigned long)d->logic.observation.session, (unsigned long)d->logic.latency_seen,
        (unsigned long)d->logic.latency_count, (unsigned)NN2_ADS131_LATENCY_CAPACITY);
    for (uint32_t i = 0u; i < d->logic.latency_count; ++i) {
        const nn2_ads131_latency_event_t *e = &d->logic.latency[i];
        ESP_LOGI("ads131_latency_event", "index=%lu flags=%u serial=%lu previous_valid=%u",
            (unsigned long)i, (unsigned)e->reasons, (unsigned long)e->current.serial,
            (unsigned)e->previous_valid);
        if (e->previous_valid) report_latency_trace("previous", &e->previous);
        report_latency_trace("current", &e->current);
    }
}
static void report_runtime_failure(device_t *d)
{
    const nn2_ads131_runtime_failure_t *f = &d->logic.runtime_failure;
    if (!f->valid || d->runtime_reported) return;
    d->runtime_reported = true;
    static const char *const details[] = {
        "unspecified", "backlog", "service_gap", "edge_missing", "serial_before",
        "edge_future", "age_before_spi", "serial_after_spi", "age_after_spi", "rate_qualification", "raw_ring_overflow"
    };
    const char *detail = f->detail < sizeof(details) / sizeof(details[0])
        ? details[f->detail] : "invalid";
    ESP_LOGE("ads131_runtime_detail", "reason=%s decision_us=%llu edge_age_us=%llu spi_elapsed_us=%u",
        detail, (unsigned long long)f->at_us,
        (unsigned long long)(f->at_us >= f->edge_us ? f->at_us - f->edge_us : 0u),
        (unsigned)f->spi_elapsed_us);
    ESP_LOGE("ads131_runtime", "fault=%u pending=%lu event_dropped=%lu expected_serial=%lu event_serial=%lu observed_serial=%lu edge_us=%llu at_us=%llu last_service_us=%llu cleanup=%d crc_bad=%lu",
        (unsigned)f->fault, (unsigned long)f->pending, (unsigned long)f->dropped,
        (unsigned long)f->expected_serial, (unsigned long)f->event_serial,
        (unsigned long)f->observed_serial, (unsigned long long)f->edge_us,
        (unsigned long long)f->at_us, (unsigned long long)f->last_service_us,
        (int)f->cleanup_result, (unsigned long)d->logic.observation.crc_bad);
    const nn2_ads131_rate_t *r = &d->logic.rate;
    ESP_LOGE("ads131_rate", "samples=%u target=1024 hz=%lu min_period_us=%lu max_period_us=%lu period_gaps=%lu valid=%u active=%u",
        (unsigned)r->samples, (unsigned long)r->hz, (unsigned long)(r->samples > 1u ? r->min_period_us : 0u),
        (unsigned long)r->max_period_us, (unsigned long)r->period_gaps,
        (unsigned)r->valid, (unsigned)r->active);
    uint32_t count = d->logic.trace_head < NN2_ADS131_TRACE_CAPACITY
        ? d->logic.trace_head : NN2_ADS131_TRACE_CAPACITY;
    for (uint32_t i = 0u; i < count; ++i) {
        const nn2_ads131_trace_t *t = &d->logic.trace[(d->logic.trace_head - count + i) % NN2_ADS131_TRACE_CAPACITY];
        ESP_LOGE("ads131_trace", "serial=%lu edge_us=%llu period_us=%lu spi_begin_us=%llu spi_end_us=%llu crc_us=%lu pending=%lu status=%04x phase=%u",
            (unsigned long)t->serial, (unsigned long long)t->edge_us,
            (unsigned long)t->period_us, (unsigned long long)t->spi_begin_us,
            (unsigned long long)t->spi_end_us, (unsigned long)t->crc_us,
            (unsigned long)t->pending, (unsigned)t->status, (unsigned)t->phase);
        report_latency_trace("fault", t);
    }

}
static void report_first_unknown(device_t *d)
{
    const nn2_ads131_unknown_t *u = &d->logic.first_unknown;
    const char *reason = u->reasons == NN2_ADS131_UNKNOWN_PERIOD ? "period"
        : u->reasons == NN2_ADS131_UNKNOWN_FRESHNESS ? "freshness" : "period+freshness";
    ESP_LOGW("ads131_unknown", "reason=%s session=%lu checked_us=%llu observed_serial=%lu previous_valid=%u fresh_mask=%x",
        reason, (unsigned long)u->session, (unsigned long long)u->checked_us,
        (unsigned long)u->observed_serial, (unsigned)u->previous_valid,
        (unsigned)(u->current.status & 0x000fu));
    for (unsigned i = u->previous_valid ? 0u : 1u; i < 2u; ++i) {
        const nn2_ads131_trace_t *t = i ? &u->current : &u->previous;
        ESP_LOGW("ads131_unknown_trace", "role=%s serial=%lu edge_us=%llu period_us=%lu spi_begin_us=%llu spi_end_us=%llu crc_us=%lu pending=%lu status=%04x phase=%u",
            i ? "current" : "previous", (unsigned long)t->serial,
            (unsigned long long)t->edge_us, (unsigned long)t->period_us,
            (unsigned long long)t->spi_begin_us, (unsigned long long)t->spi_end_us,
            (unsigned long)t->crc_us, (unsigned long)t->pending,
            (unsigned)t->status, (unsigned)t->phase);
        report_latency_trace(i ? "unknown_current" : "unknown_previous", t);
    }
}
static void owner(void *arg)
{
    device_t *d = arg;
    d->task = xTaskGetCurrentTaskHandle();
    if (setup(d) != NN2_OK) (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_SPI);
    d->watchdog = esp_task_wdt_add(NULL) == ESP_OK;
    if (!d->watchdog) (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_WATCHDOG);
    uint32_t rate_reported_session = UINT32_MAX;
    uint32_t unknown_reported_session = UINT32_MAX;
    uint32_t latency_reported_session = UINT32_MAX;
    for (;;) {
        uint32_t notified = 0u;
        (void)xTaskNotifyWait(0u, UINT32_MAX, &notified, 1u);
        d->logic.owner_resume_us = now_us(d);
        owner_iteration(d, notified & 0x7fffffffu);
        atomic_store(&d->stream_faulted, d->logic.observation.state == NN2_ADC_FAULT);
        if (d->logic.observation.state != NN2_ADC_RUNNING) {
            uint32_t tail = (uint32_t)uxQueueMessagesWaiting(d->raw);
            if (tail) {
                if (d->logic.observation.state == NN2_ADC_STOPPED) d->stop_discarded += tail;
                else d->logic.observation.lost += tail;
                xQueueReset(d->raw);
            }
        }
        report_startup_failure(d);
        report_runtime_failure(d);
        if (d->logic.observation.state != NN2_ADC_RUNNING && d->logic.first_unknown.valid &&
            unknown_reported_session != d->logic.first_unknown.session) {
            unknown_reported_session = d->logic.first_unknown.session;
            report_first_unknown(d);
        }
        if ((d->logic.observation.state == NN2_ADC_STOPPED ||
             d->logic.observation.state == NN2_ADC_FAULT) &&
            d->logic.observation.session != latency_reported_session &&
            (d->logic.rate.valid || d->logic.runtime_failure.valid)) {
            latency_reported_session = d->logic.observation.session;
            report_latency_events(d);
        }
        /* Never print qualification success while conversions are running. */
        if (d->logic.observation.state == NN2_ADC_STOPPED && d->logic.rate.valid &&
            rate_reported_session != d->logic.observation.session) {
            rate_reported_session = d->logic.observation.session;
            ESP_LOGI("ads131_rate", "samples=%u hz=%lu min_period_us=%lu max_period_us=%lu period_gaps=%lu valid=1",
                (unsigned)d->logic.rate.samples, (unsigned long)d->logic.rate.hz,
                (unsigned long)d->logic.rate.min_period_us,
                (unsigned long)d->logic.rate.max_period_us,
                (unsigned long)d->logic.rate.period_gaps);
            ESP_LOGI("ads131_drain", "produced_total=%lu consumed_total=%lu lost_total=%lu overflow_total=%lu unknown_total=%lu max_depth=%lu stop_discarded_total=%lu read_late_total=%lu",
                (unsigned long)d->logic.observation.produced, (unsigned long)atomic_load(&d->consumed),
                (unsigned long)d->logic.observation.lost, (unsigned long)d->logic.observation.overflow,
                (unsigned long)d->logic.observation.loss_unknown,
                (unsigned long)d->logic.observation.max_occupancy,
                (unsigned long)d->stop_discarded, (unsigned long)atomic_load(&d->read_late));
        }
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
        .take_events=take_events, .publish=publish };
    nn2_ads131_logic_bind(&d->logic, io);
    d->raw = xQueueCreateStatic(256u, sizeof(nn2_core_adc_record_t), d->raw_storage, &d->raw_control);
    atomic_init(&d->consumed, 0u); atomic_init(&d->read_late, 0u);
    atomic_init(&d->stream_faulted, true);
    d->commands = xQueueCreateStatic(1u, sizeof(request_t), d->command_storage, &d->commands_control);
    d->replies = xQueueCreateStatic(1u, sizeof(reply_t), d->reply_storage, &d->replies_control);
    if (!d->raw || !d->commands || !d->replies) return NN2_ERR_IO;
    d->task = xTaskCreateStaticPinnedToCore(owner, "nn2_ads131", NN2_RTOS_ADS131_STACK_BYTES,
        d, NN2_RTOS_ADS131_PRIORITY, d->stack, &d->task_storage, NN2_RTOS_ADS131_CORE);
    if (!d->task) return NN2_ERR_IO;
    d->created = true;
    *out = (nn2_port_adc_stream_t){d, init_port, start_port, stop_port, read_port, observe_port};
    return NN2_OK;
}
