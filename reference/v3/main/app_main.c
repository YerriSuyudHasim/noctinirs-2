#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v3_adc.h"
#include "nocti_v3_board.h"
#include "nocti_v3_common.h"
#include "nocti_v3_health.h"
#include "nocti_v3_led.h"
#include "nocti_v3_optical.h"
#include "nocti_v3_stream.h"
#include "nocti_v3_timebase.h"
#include "nocti_v3_version.h"
#include "nocti_v3_wifi.h"

static const char *TAG = "nocti_v3_main";
static nocti_v3_health_t s_health;
static nocti_v3_timebase_t s_timebase;
static nocti_v3_board_t s_board;
static nocti_v3_adc_t s_adc;
static nocti_v3_led_t s_led;
static nocti_v3_optical_t s_optical;
static nocti_v3_stream_t s_stream;
static nocti_v3_wifi_t s_wifi;
static TaskHandle_t s_health_task;

/*
 * Serial diagnostics contract
 * ---------------------------
 * V3 does not enter a runtime quiet mode. Logging is allowed only from
 * task context and never from DRDY ISR, GPTimer callback, I2C callback,
 * per-sample acquisition, or normal per-slot paths.
 *
 * These checks deliberately stop the build when a stale sdkconfig would
 * compile out INFO logs or route the application console away from the
 * ESP32-S3 native USB Serial/JTAG peripheral on GPIO19/GPIO20.
 */
_Static_assert(CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_INFO,
               "NoctiNIRSv3 requires CONFIG_LOG_DEFAULT_LEVEL >= INFO");
_Static_assert(CONFIG_LOG_MAXIMUM_LEVEL >= ESP_LOG_INFO,
               "NoctiNIRSv3 requires CONFIG_LOG_MAXIMUM_LEVEL >= INFO");

#ifndef CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
#error "NoctiNIRSv3 requires CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y"
#endif
#ifndef CONFIG_LOG_MASTER_LEVEL
#error "NoctiNIRSv3 requires CONFIG_LOG_MASTER_LEVEL=y"
#endif
#ifndef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "NoctiNIRSv3 requires CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y"
#endif
#ifndef CONFIG_ESP_CONSOLE_SECONDARY_NONE
#error "NoctiNIRSv3 requires CONFIG_ESP_CONSOLE_SECONDARY_NONE=y"
#endif

static void configure_serial_logging(void)
{
    static const char *const diagnostic_tags[] = {
        "main_task",
        "nocti_v3_main",
        "nocti_v3_board",
        "nocti_v3_timebase",
        "nocti_v3_adc",
        "nocti_v3_ads131",
        "nocti_v3_led",
        "nocti_v3_optical",
        "nocti_v3_stream",
        "nocti_v3_wifi",
        "wifi",
        "wifi_init",
        "phy_init",
        "esp_netif_lwip",
        "esp_netif_handlers",
    };

    /* Master, default/wildcard, and explicit tag levels are restored. This is
     * intentionally redundant for diagnostics: a stale global or per-tag
     * runtime setting must not silently hide startup and Wi-Fi state logs.
     */
    esp_log_set_level_master(ESP_LOG_INFO);
    esp_log_level_set("*", ESP_LOG_INFO);
    for (size_t i = 0u;
         i < sizeof(diagnostic_tags) / sizeof(diagnostic_tags[0]);
         ++i) {
        esp_log_level_set(diagnostic_tags[i], ESP_LOG_INFO);
    }
}

static void record_error(nocti_v3_fault_code_t fault, esp_err_t error)
{
    nocti_v3_health_record_fault(&s_health, fault, (int32_t)error);
    ESP_LOGE(TAG, "startup fault=%s error=%s",
             nocti_v3_fault_name(fault), esp_err_to_name(error));
}

static void health_logger_task(void *argument)
{
    (void)argument;
    const int task_core = xPortGetCoreID();
    for (;;) {
        const uint32_t stack_watermark = uxTaskGetStackHighWaterMark(NULL);
        nocti_v3_health_set_logger_task(&s_health, task_core, stack_watermark);

        nocti_v3_health_snapshot_t system_health;
        nocti_v3_adc_health_snapshot_t adc_health;
        nocti_v3_led_health_snapshot_t led_health;
        nocti_v3_optical_health_snapshot_t optical_health;
        nocti_v3_health_snapshot(&s_health, &system_health);
        nocti_v3_adc_health_snapshot(&s_adc, &adc_health);
        nocti_v3_led_health_snapshot(&s_led, &led_health);
        nocti_v3_optical_health_snapshot(&s_optical, &optical_health);

        const bool clean = !system_health.sensor_fault &&
            adc_health.adc_missed == 0u &&
            adc_health.adc_sequence_gap == 0u &&
            adc_health.drdy_event_overflow == 0u &&
            adc_health.adc_ring_overflow == 0u &&
            adc_health.crc_bad == 0u &&
            adc_health.spi_errors == 0u &&
            led_health.led_errors == 0u &&
            optical_health.sched_missed == 0u &&
            optical_health.sched_abort == 0u;

        if (clean) {
            ESP_LOGI(TAG,
                "health state=%s mode=%s session=%lu frames=%llu valid=%llu partial=%llu invalid=%llu adc_ok=%llu missed=0 seq_gap=0 crc_bad=0 spi_err=0 led_err=0 sched_missed=0 sched_abort=0 ap=%u stations=%lu net_ready=%u peer=%u stream=%u net_drop=%llu stream_err=%llu stacks=%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu cores=%d/%d/%d/%d/%d/%d/%d/%d/%d",
                nocti_v3_state_name(system_health.lifecycle),
                nocti_v3_mode_name(system_health.mode),
                (unsigned long)optical_health.session_epoch,
                (unsigned long long)optical_health.frames_total,
                (unsigned long long)optical_health.frames_valid,
                (unsigned long long)optical_health.partial_frames,
                (unsigned long long)optical_health.invalid_frames,
                (unsigned long long)adc_health.adc_ok,
                system_health.wifi_ap_started ? 1u : 0u,
                (unsigned long)system_health.wifi_station_count,
                system_health.network_ready ? 1u : 0u,
                system_health.udp_peer_active ? 1u : 0u,
                system_health.stream_enabled ? 1u : 0u,
                (unsigned long long)optical_health.network_drop,
                (unsigned long long)optical_health.stream_errors,
                (unsigned long)adc_health.adc_task_stack_watermark_bytes,
                (unsigned long)optical_health.scheduler_stack_watermark_bytes,
                (unsigned long)led_health.led_task_stack_watermark_bytes,
                (unsigned long)optical_health.processing_stack_watermark_bytes,
                (unsigned long)led_health.recovery_task_stack_watermark_bytes,
                (unsigned long)optical_health.frame_sink_stack_watermark_bytes,
                (unsigned long)system_health.udp_task_stack_watermark_bytes,
                (unsigned long)system_health.stream_task_stack_watermark_bytes,
                (unsigned long)stack_watermark,
                adc_health.adc_task_core,
                optical_health.scheduler_task_core,
                led_health.led_task_core,
                optical_health.processing_task_core,
                led_health.recovery_task_core,
                optical_health.frame_sink_task_core,
                system_health.udp_task_core,
                system_health.stream_task_core,
                task_core);
        } else {
            ESP_LOGW(TAG,
                "health state=%s mode=%s session=%lu fault=%s sensor_fault=%u ap=%u stations=%lu net_ready=%u peer=%u stream=%u adc_ok=%llu missed=%llu seq_gap=%llu event_ovf=%llu raw_ovf=%llu crc_bad=%llu spi_err=%llu led_err=%llu i2c_nack=%llu i2c_timeout=%llu sched_missed=%llu sched_abort=%llu partial=%llu invalid=%llu underk=%llu/%llu frame_ovf=%llu net_drop=%llu stream_err=%llu",
                nocti_v3_state_name(system_health.lifecycle),
                nocti_v3_mode_name(system_health.mode),
                (unsigned long)optical_health.session_epoch,
                nocti_v3_fault_name(system_health.last_fault),
                system_health.sensor_fault ? 1u : 0u,
                system_health.wifi_ap_started ? 1u : 0u,
                (unsigned long)system_health.wifi_station_count,
                system_health.network_ready ? 1u : 0u,
                system_health.udp_peer_active ? 1u : 0u,
                system_health.stream_enabled ? 1u : 0u,
                (unsigned long long)adc_health.adc_ok,
                (unsigned long long)adc_health.adc_missed,
                (unsigned long long)adc_health.adc_sequence_gap,
                (unsigned long long)adc_health.drdy_event_overflow,
                (unsigned long long)adc_health.adc_ring_overflow,
                (unsigned long long)adc_health.crc_bad,
                (unsigned long long)adc_health.spi_errors,
                (unsigned long long)led_health.led_errors,
                (unsigned long long)led_health.i2c_nack,
                (unsigned long long)led_health.i2c_timeout,
                (unsigned long long)optical_health.sched_missed,
                (unsigned long long)optical_health.sched_abort,
                (unsigned long long)optical_health.partial_frames,
                (unsigned long long)optical_health.invalid_frames,
                (unsigned long long)optical_health.underk_on,
                (unsigned long long)optical_health.underk_off,
                (unsigned long long)optical_health.completed_frame_ring_overflow,
                (unsigned long long)optical_health.network_drop,
                (unsigned long long)optical_health.stream_errors);
        }

        vTaskDelay(pdMS_TO_TICKS(NOCTI_V3_OPTICAL_HEALTH_PERIOD_MS));
    }
}

void app_main(void)
{
    configure_serial_logging();
    nocti_v3_health_init(&s_health);
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "%s firmware=%s phase=%s target=ESP32-S3 ESP-IDF=5.5.1",
             NOCTI_V3_FW_NAME, NOCTI_V3_FW_VERSION_STRING,
             NOCTI_V3_BUILD_PHASE);
    ESP_LOGI(TAG,
             "serial logging enabled configured_default=%d runtime_default=%d maximum=%d master=%d main_tag=%d wifi_tag=%d console=USB_SERIAL_JTAG gpio_dm=19 gpio_dp=20 secondary=NONE runtime_quiet=DISABLED",
             (int)CONFIG_LOG_DEFAULT_LEVEL, (int)esp_log_get_default_level(),
             (int)CONFIG_LOG_MAXIMUM_LEVEL, (int)esp_log_get_level_master(),
             (int)esp_log_level_get(TAG),
             (int)esp_log_level_get("nocti_v3_wifi"));
    ESP_LOGI(TAG,
             "P5 contract: decoupled UDP stream-v1 transport + non-real-time commands; no MBLL/SSR/UI");

    ESP_LOGI(TAG, "INIT[1/8] validating centralized timing configuration");
    const bool timing_valid = nocti_v3_timing_runtime_validate();
    ESP_LOGI(TAG, "INIT[1/8] timing validation %s", timing_valid ? "OK" : "FAILED");
    if (!timing_valid) record_error(NOCTI_V3_FAULT_CONFIG,
                                    ESP_ERR_INVALID_STATE);

    ESP_LOGI(TAG, "INIT[2/8] board GPIO/I2C/SPI fail-safe setup");
    nocti_v3_board_config_t board_config;
    nocti_v3_board_config_defaults(&board_config);
    const esp_err_t board_error = nocti_v3_board_init(&s_board, &board_config);
    if (board_error != ESP_OK) {
        record_error(NOCTI_V3_FAULT_CONFIG, board_error);
    } else {
        ESP_LOGI(TAG, "INIT[2/8] board setup OK gpio_safe=%u i2c_ready=%u",
                 s_board.gpio_safe ? 1u : 0u, s_board.i2c_ready ? 1u : 0u);
    }

    /* ADC task creates the common GPTimer and DRDY ISR on core 1. */
    ESP_LOGI(TAG, "INIT[3/8] ADS131M04 + common GPTimer startup validation");
    const esp_err_t adc_error = nocti_v3_adc_init(
        &s_adc, &s_board, &s_timebase, &s_health);
    if (adc_error != ESP_OK) {
        ESP_LOGE(TAG, "INIT[3/8] ADS131M04 startup validation FAILED: %s",
                 esp_err_to_name(adc_error));
    } else {
        ESP_LOGI(TAG, "INIT[3/8] ADS131M04 startup validation OK");
    }

    /* LED task is pinned to core 1. It allocates one I2C bus before START,
     * performs synchronous init/readback with callbacks deregistered, then
     * registers the nonblocking runtime callback on the same persistent handle.
     */
    ESP_LOGI(TAG, "INIT[4/8] PCA9956B synchronous validation + async runtime path");
    const esp_err_t led_error = nocti_v3_led_init(
        &s_led, &s_board, &s_timebase, &s_health);
    if (led_error != ESP_OK) {
        record_error(NOCTI_V3_FAULT_I2C_INIT, led_error);
    } else {
        ESP_LOGI(TAG, "INIT[4/8] PCA9956B path OK runtime_ready=%u",
                 nocti_v3_led_runtime_ready(&s_led) ? 1u : 0u);
    }

    ESP_LOGI(TAG, "INIT[5/8] optical scheduler/processing/frame rings");
    const esp_err_t optical_error = nocti_v3_optical_init(
        &s_optical, &s_adc, &s_led, &s_timebase, &s_health);
    if (optical_error != ESP_OK) {
        record_error(NOCTI_V3_FAULT_SCHEDULER, optical_error);
    } else {
        ESP_LOGI(TAG, "INIT[5/8] optical task graph OK");
    }

    nocti_v3_health_set_startup(&s_health, timing_valid,
        nocti_v3_timebase_ready(&s_timebase), s_board.gpio_safe,
        s_board.i2c_ready, nocti_v3_led_is_fail_safe(&s_led) ||
                           nocti_v3_led_runtime_ready(&s_led));

    ESP_LOGI(TAG, "INIT[6/8] command and completed-frame stream interface");
    const esp_err_t stream_error = nocti_v3_stream_init(
        &s_stream, &s_board, &s_timebase, &s_adc, &s_led, &s_optical,
        &s_health);
    if (stream_error != ESP_OK) {
        record_error(NOCTI_V3_FAULT_CONFIG, stream_error);
    } else {
        ESP_LOGI(TAG, "INIT[6/8] stream interface OK");
    }

    ESP_LOGI(TAG, "INIT[7/8] periodic health logger task");
    const BaseType_t health_created = xTaskCreatePinnedToCore(
        health_logger_task, "nocti_v3_health",
        NOCTI_V3_STACK_HEALTH_BYTES, NULL,
        NOCTI_V3_PRIO_HEALTH, &s_health_task,
        NOCTI_V3_CORE_NETWORK);
    if (health_created != pdPASS) {
        record_error(NOCTI_V3_FAULT_CONFIG, ESP_ERR_NO_MEM);
    } else {
        ESP_LOGI(TAG, "INIT[7/8] health logger task created core=%d priority=%u period_ms=%u",
                 NOCTI_V3_CORE_NETWORK, (unsigned)NOCTI_V3_PRIO_HEALTH,
                 (unsigned)NOCTI_V3_OPTICAL_HEALTH_PERIOD_MS);
    }

    ESP_LOGI(TAG, "INIT[8/8] Wi-Fi SoftAP + UDP control/stream tasks");
    nocti_v3_wifi_config_t wifi_config;
    nocti_v3_wifi_config_defaults(&wifi_config);
    const esp_err_t wifi_error = nocti_v3_wifi_init(
        &s_wifi, &wifi_config, &s_stream, &s_health);
    if (wifi_error != ESP_OK) {
        record_error(NOCTI_V3_FAULT_WIFI_INIT, wifi_error);
    } else {
        ESP_LOGI(TAG, "INIT[8/8] Wi-Fi/UDP initialization OK");
    }

    nocti_v3_adc_health_snapshot_t adc_health;
    nocti_v3_led_health_snapshot_t led_health;
    nocti_v3_optical_health_snapshot_t optical_health;
    nocti_v3_adc_health_snapshot(&s_adc, &adc_health);
    nocti_v3_led_health_snapshot(&s_led, &led_health);
    nocti_v3_optical_health_snapshot(&s_optical, &optical_health);
    ESP_LOGI(TAG,
             "ADS SPI3 mode=1 clock=%u frame=18B target=8000 measured=%lu ready=%u core=%d",
             (unsigned)NOCTI_V3_ADS_SPI_CLOCK_HZ,
             (unsigned long)adc_health.measured_sps,
             adc_health.ready ? 1u : 0u, adc_health.adc_task_core);
    ESP_LOGI(TAG,
             "PCA async ready=%u clock=%lu qdepth=%lu change_on_STOP=1 LEDOUT_payload=7B I2C_init_core=%d",
             led_health.runtime_ready ? 1u : 0u,
             (unsigned long)s_board.config.i2c_clock_hz,
             (unsigned long)s_board.i2c_queue_depth,
             led_health.i2c_init_core);
    ESP_LOGI(TAG,
             "scheduler core=%d processing_core=%d frame_sink_core=%d priority=%u slots=20 slot_us=5000 frame_hz=10 K_target=10/10 K_min=8/8 representative=mean_latest_contiguous_K",
             optical_health.scheduler_task_core,
             optical_health.processing_task_core,
             optical_health.frame_sink_task_core,
             (unsigned)NOCTI_V3_PRIO_SCHEDULER);
    ESP_LOGI(TAG,
             "UDP control=%u core=%d priority=%u; stream=%u core=%d priority=%u; health core=%d priority=%u nonblocking_drop_policy=1",
             (unsigned)wifi_config.udp_control_port,
             (int)wifi_config.control_task_core,
             (unsigned)wifi_config.control_task_priority,
             (unsigned)wifi_config.udp_stream_port,
             (int)wifi_config.stream_task_core,
             (unsigned)wifi_config.stream_task_priority,
             NOCTI_V3_CORE_NETWORK,
             (unsigned)NOCTI_V3_PRIO_HEALTH);
    ESP_LOGI(TAG,
             "commands HELP VERSION LOGSTATUS STATUS HEALTH TIMING REGS DARK SCAN START STOP RESET_COUNTERS DUMP_SLOT_TIMING CURRENT SLOT");
    ESP_LOGI(TAG, "============================================================");
}
