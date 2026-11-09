"""Host execution of the ADS fault transitions and the Batch 5A monitor branch.

Only platform calls and the SPI/DRDY frame service are stubbed. The lock hook
runs a complete writer transition just before the reader enters its lock.
"""
from tests.architecture.test_batch5a_stop_diag import INC, ROOT, Native, extract


COMPONENTS = ROOT / "firmware/v4-idf61/components"
ADS = COMPONENTS / "nocti_v4_ads/nocti_v4_ads.c"
HEADER = COMPONENTS / "nocti_v4_ads/include/nocti_v4_ads.h"
HEALTH = COMPONENTS / "nocti_v4_health/include/nocti_v4_health.h"
CONFIG = COMPONENTS / "nocti_v4_common/include/nocti_v4_ads_config.h"

TYPES = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "nocti_v4_stop_diag.h"
typedef int esp_err_t;
typedef int nocti_v4_board_t, nocti_v4_time_t, spi_transaction_t;
typedef int StaticSemaphore_t, StaticQueue_t, portMUX_TYPE;
typedef void *TaskHandle_t, *SemaphoreHandle_t, *QueueHandle_t, *spi_device_handle_t;
typedef unsigned TickType_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define NOCTI_V4_ADS131_DRDY_GPIO 1
#define NOCTI_V4_RTOS_WDT_IDLE_WAIT_MS 1u
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
"""

STUBS = r"""
static ads_runtime_t runtime;
static nocti_v4_ads_t s_ads;
static nocti_v4_health_t health;
static void (*before_lock)(void);
static unsigned locks, gpio_disables, releases;
static esp_err_t init_error, frame_error;
static bool drain_ok, inject_init, s_control_fault, off_called;
static int s_control_lock;
static nocti_v4_stop_diag_t s_control_diag;

static void enter(portMUX_TYPE *lock)
{
    if (lock == &runtime.fault_lock) {
        ++locks;
        if (before_lock) {
            void (*writer)(void) = before_lock;
            before_lock = NULL;
            writer();
        }
    }
    assert(*lock == 0);
    *lock = 1;
}
static void leave(portMUX_TYPE *lock)
{
    assert(*lock == 1);
    *lock = 0;
}
#define portENTER_CRITICAL(lock) enter(lock)
#define portEXIT_CRITICAL(lock) leave(lock)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int xPortGetCoreID(void) { return 1; }
static void nocti_v4_task_wdt_subscribe(void) {}
static void nocti_v4_task_wdt_kick(void) {}
static esp_err_t nocti_v4_time_init(nocti_v4_time_t *t) { (void)t; return ESP_OK; }
static esp_err_t install_drdy_interrupt(ads_runtime_t *r) { (void)r; return ESP_OK; }
static esp_err_t transport_init(ads_runtime_t *r) { (void)r; return ESP_OK; }
static esp_err_t transport_acquire(ads_transport_t *t) { (void)t; return ESP_OK; }
static void transport_release(ads_transport_t *t)
{
    (void)t;
    assert(runtime.fault_lock == 0);
    ++releases;
}
static esp_err_t hardware_reset(ads_runtime_t *r) { (void)r; return ESP_OK; }
static esp_err_t configure_8ksps(ads_runtime_t *r) { (void)r; return ESP_OK; }
static esp_err_t validate_rate(ads_runtime_t *r) { (void)r; return init_error; }
static int nocti_v4_ads_failed_init_needs_teardown(int dark, int ads) { return dark || ads; }
static void quiesce_failed_init(ads_runtime_t *r) { (void)r; assert(!r->ready); }
static int request_dark_exit(ads_runtime_t *r) { (void)r; return 1; }
static int esp_task_wdt_delete(void *t) { (void)t; return ESP_OK; }
static void vTaskDelete(void *t) { (void)t; }
static int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; assert(inject_init); return pdTRUE; }
static int xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    (void)s;
    return ticks != 0u && drain_ok ? pdTRUE : 0;
}
static void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
static int ulTaskNotifyTake(int clear, unsigned ticks) { (void)clear; (void)ticks; return 0; }
static esp_err_t gpio_intr_disable(int pin)
{
    (void)pin;
    assert(!runtime.capture_enabled && runtime.fault_lock == 0);
    ++gpio_disables;
    return ESP_OK;
}
static esp_err_t gpio_intr_enable(int pin) { (void)pin; return ESP_OK; }
static esp_err_t sync_conversions(ads_runtime_t *r) { (void)r; return ESP_OK; }
static esp_err_t nocti_v4_time_now_us(nocti_v4_time_t *t, uint64_t *us)
{
    (void)t;
    *us = 0;
    return ESP_OK;
}
static uint32_t raw_depth(const ads_runtime_t *r) { (void)r; return 0u; }
static uint32_t event_depth(const ads_runtime_t *r) { (void)r; return 0u; }
static uint64_t load_last_drdy_timestamp(const ads_runtime_t *r) { (void)r; return 0; }
static void reset_acquisition_counters(ads_runtime_t *r) { (void)r; }
static void clear_event_ring(ads_runtime_t *r) { (void)r; }
static void clear_raw_ring(ads_runtime_t *r) { (void)r; }
static esp_err_t wait_for_event(ads_runtime_t *r, drdy_event_t *e)
{
    (void)r;
    memset(e, 0, sizeof(*e));
    return ESP_OK;
}
static void process_event(ads_runtime_t *r, const drdy_event_t *e);
"""

CASES = r"""
static void process_event(ads_runtime_t *r, const drdy_event_t *e)
{
    (void)e;
    if (frame_error != ESP_OK) {
        set_fault(r, NOCTI_V4_ADS_FAULT_SPI_READ,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, frame_error);
    } else if (--r->sync_discard_remaining == 0u) {
        __atomic_store_n(&r->lifecycle, NOCTI_V4_ADS_RUNNING_DARK, __ATOMIC_RELEASE);
    }
}
static void setup(bool ready)
{
    memset(&runtime, 0, sizeof(runtime));
    memset(&health, 0, sizeof(health));
    memset(&s_ads, 0, sizeof(s_ads));
    s_ads.runtime = &runtime;
    runtime.public_ads = &s_ads;
    runtime.health = &health;
    runtime.ready = ready;
    runtime.rate_valid = ready;
}
static void snapshot(nocti_v4_ads_lifecycle_t state, esp_err_t error,
                     nocti_v4_ads_fault_code_t code, nocti_v4_ads_fault_origin_t origin)
{
    nocti_v4_ads_status_snapshot_t status;
    nocti_v4_ads_get_status(&s_ads, &status);
    assert(status.lifecycle == state && status.last_error == error);
    assert(status.last_fault_error == error && status.last_fault_code == code);
    assert(status.last_fault_origin == origin);
}

static void restart(void)
{
    drain_ok = true;
    assert(stop_dark_internal(&runtime) == ESP_OK);
    assert(start_dark_internal(&runtime) == frame_error);
}

static void runtime_faults(void)
{
    setup(true);
    assert(start_dark_internal(&runtime) == ESP_OK);
    unsigned previous_locks = locks;
    snapshot(NOCTI_V4_ADS_RUNNING_DARK, ESP_OK,
             NOCTI_V4_ADS_FAULT_NONE, NOCTI_V4_ADS_FAULT_ORIGIN_NONE);
    assert(locks == previous_locks);

    set_fault(&runtime, NOCTI_V4_ADS_FAULT_CRC,
              NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_RESPONSE);
    set_fault(&runtime, NOCTI_V4_ADS_FAULT_DRDY_STALL,
              NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_WATCHDOG, ESP_ERR_TIMEOUT);
    drain_ok = false;
    assert(stop_dark_internal(&runtime) == ESP_ERR_TIMEOUT);
    snapshot(NOCTI_V4_ADS_FAULTED, ESP_ERR_INVALID_RESPONSE,
             NOCTI_V4_ADS_FAULT_CRC, NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT);
    assert(health.dark_session_faults == 1u);
    drain_ok = true;
    assert(stop_dark_internal(&runtime) == ESP_OK);
    snapshot(NOCTI_V4_ADS_STOPPED, ESP_ERR_INVALID_RESPONSE,
             NOCTI_V4_ADS_FAULT_CRC, NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT);
    assert(start_dark_internal(&runtime) == ESP_OK);
    snapshot(NOCTI_V4_ADS_RUNNING_DARK, ESP_OK,
             NOCTI_V4_ADS_FAULT_NONE, NOCTI_V4_ADS_FAULT_ORIGIN_NONE);
}

static void interleavings(void)
{
    set_fault(&runtime, NOCTI_V4_ADS_FAULT_CRC,
              NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_RESPONSE);
    before_lock = restart;
    snapshot(NOCTI_V4_ADS_RUNNING_DARK, ESP_OK,
             NOCTI_V4_ADS_FAULT_NONE, NOCTI_V4_ADS_FAULT_ORIGIN_NONE);
    assert(before_lock == NULL);

    set_fault(&runtime, NOCTI_V4_ADS_FAULT_CRC,
              NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_RESPONSE);
    frame_error = ESP_ERR_TIMEOUT;
    before_lock = restart;
    snapshot(NOCTI_V4_ADS_FAULTED, ESP_ERR_TIMEOUT,
             NOCTI_V4_ADS_FAULT_SPI_READ, NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT);
    assert(before_lock == NULL);
}

static void init_failure(void)
{
    setup(false);
    inject_init = true;
    init_error = ESP_ERR_INVALID_RESPONSE;
    ads_task(&runtime);
    snapshot(NOCTI_V4_ADS_FAULTED, ESP_ERR_INVALID_RESPONSE,
             NOCTI_V4_ADS_FAULT_NONE, NOCTI_V4_ADS_FAULT_ORIGIN_NONE);
    assert(runtime.init_result == ESP_ERR_INVALID_RESPONSE && !runtime.ready);
    assert(start_dark_internal(&runtime) == ESP_ERR_INVALID_STATE);
}

#define NOCTI_V4_SCAN_FAULTED 1
#define NN2_OPTICAL_RUNNING 1
#define NN2_OPTICAL_FAULT 2
typedef struct { int lifecycle; esp_err_t last_error; } nocti_v4_scheduler_status_t;
static int s_scheduler, s_control_state = NN2_OPTICAL_RUNNING;
static int scheduler_state;
static int control_state(void *ctx) { (void)ctx; return s_control_state; }
static int control_off(void *ctx) { (void)ctx; off_called = true; return 0; }
static void nocti_v4_scheduler_get_status(int *s, nocti_v4_scheduler_status_t *out)
{
    (void)s;
    *out = (nocti_v4_scheduler_status_t){scheduler_state, ESP_ERR_INVALID_STATE};
}
"""

MAIN = r"""
int main(void)
{
    runtime_faults();
    interleavings();
    monitor();
    assert(s_control_fault && off_called && s_control_state == NN2_OPTICAL_FAULT);
    assert(s_control_diag.ctrl_origin == NOCTI_V4_CTRL_ORIGIN_RUNTIME_ADS);
    assert(s_control_diag.ctrl_error == ESP_ERR_TIMEOUT);

    memset(&s_control_diag, 0, sizeof(s_control_diag));
    scheduler_state = NOCTI_V4_SCAN_FAULTED;
    s_control_state = NN2_OPTICAL_RUNNING;
    monitor();
    assert(s_control_diag.ctrl_origin == NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED);
    assert(s_control_diag.ctrl_error == ESP_ERR_INVALID_STATE);
    init_failure();
    return 0;
}
"""


class AdsFaultCoherence(Native):
    def test_production_fault_publication_reset_and_monitor(self):
        source = ADS.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")
        health = HEALTH.read_text(encoding="utf-8")
        config = CONFIG.read_text(encoding="utf-8")
        controls = INC.read_text(encoding="utf-8")
        production_types = (
            config[config.index("#define NOCTI_V4_ADS_SAMPLE_RATE_HZ"):
                   config.index("_Static_assert")]
            + health[health.index("typedef struct"):]
            + header[header.index("#define NOCTI_V4_ADS_RAW_STATS_WINDOW_SAMPLES"):
                     header.index("/* Creates the two persistent tasks")]
            + source[source.index("typedef struct {\n    uint64_t timestamp_us;"):
                     source.index("static ads_runtime_t s_runtime;")]
        )
        functions = (
            ("static inline void", "counter_add"),
            ("static void", "publish_fault"),
            ("static void", "set_fault"),
            ("static esp_err_t", "start_dark_internal"),
            ("static esp_err_t", "stop_dark_internal"),
            ("void", "nocti_v4_ads_get_status"),
        )
        code = "\n".join(extract(source, rf"{kind} {name}\([^)]*\)") for kind, name in functions)
        # Exercise the actual task initialization path, stopping before its command loop.
        boot = extract(source, r"static void ads_task\(void \*argument\)").split("    for (;;) {", 1)[0]
        monitor = extract(controls, r"static void control_worker\(void \*ctx\)")
        start = monitor.index("        if (control_state(NULL)==NN2_OPTICAL_RUNNING)")
        end = monitor.index("        bool stop=s_control_stop;", start)
        branch = monitor[start:monitor.rindex("        portENTER_CRITICAL", start, end)]
        self.run_c("\n".join((TYPES, production_types, STUBS, code, boot + "}",
                             CASES, "static void monitor(void) {\n" + branch + "}", MAIN)))
