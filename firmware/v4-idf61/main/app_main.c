#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR != 6 || ESP_IDF_VERSION_MINOR != 1
#error "This migration candidate targets ESP-IDF 6.1 only"
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v4_async_led.h"
#include "nocti_v4_board.h"
#include "nocti_v4_ads.h"
#include "nocti_v4_common.h"
#include "nocti_v4_health.h"
#include "nocti_v4_i2c.h"
#include "nocti_v4_led.h"
#include "nocti_v4_network.h"
#include "nocti_v4_scheduler.h"
#include "nocti_v4_optical.h"
#include "nocti_v4_time.h"
#include "sdkconfig.h"

#if !defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1)
#error "V4-7.2 requires app_main on core 1 so PCA I2C interrupts avoid Wi-Fi core 0"
#endif
#if !defined(CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0)
#error "V4-7.2 requires the ESP-IDF Wi-Fi task to remain on core 0"
#endif
#if !defined(CONFIG_I2C_ISR_IRAM_SAFE)
#error "V4-7.2 requires the PCA I2C ISR/callback path to remain cache-safe"
#endif

#define NOCTI_V4_APP_MAIN_CORE 1

static nocti_v4_health_t s_health;
static nocti_v4_board_t s_board;
static nocti_v4_i2c_t s_i2c;
static nocti_v4_led_t s_led;
static nocti_v4_time_t s_timebase;
static nocti_v4_ads_t s_ads;
static nocti_v4_async_led_t s_async_led;
static nocti_v4_scheduler_t s_scheduler;
static nocti_v4_optical_t s_optical;
static nocti_v4_network_t s_network;
static int32_t s_app_main_core = -1;
typedef struct {
    bool active;
    bool valid;
    uint32_t session_epoch;
    uint64_t start_timestamp_us;
    uint64_t stop_timestamp_us;
    uint32_t network_enqueued_at_start;
    uint32_t network_sent_at_start;
    uint32_t network_dropped_at_start;
    uint32_t network_errors_at_start;
    uint32_t console_rejected_at_start;
    esp_err_t frame_consumer_drain_result;
    esp_err_t network_drain_result;
    uint32_t frames_emitted;
    uint32_t warmup_frames;
    uint32_t warmup_failures;
    uint32_t target_frames;
    uint32_t partial_frames;
    uint32_t invalid_frames;
    uint32_t aborted_frames;
    uint32_t on_0_to_7;
    uint32_t on_8_to_9;
    uint32_t on_ge10;
    uint32_t off_0_to_7;
    uint32_t off_8_to_9;
    uint32_t off_ge10;
    uint32_t network_enqueued;
    uint32_t network_sent;
    uint32_t network_dropped;
    uint32_t network_errors;
    uint32_t console_rejected;
    uint32_t adc_ok;
    uint32_t adc_missed;
    uint32_t adc_sequence_gap;
    uint32_t drdy_event_overflow;
    uint32_t adc_ring_overflow;
    uint32_t crc_bad;
    uint32_t spi_errors;
    uint32_t ads_status_faults;
    uint32_t scheduler_missed_deadline;
    uint32_t scheduler_event_overflow;
    uint32_t led_completion_timeout;
    uint32_t stale_i2c_completion;
    uint32_t scan_session_faults;
} frozen_scan_session_report_t;

static frozen_scan_session_report_t s_session_report;

static void print_version(void)
{
#if CONFIG_NOCTI_V4_NN2_COMMUNICATION
    printf("CONTROL_MODE baseline=V4_IDF61_NN2_CTRL protocol=ADR0004/1 UDP3333\n");
#endif
    printf("MIGRATION source=V4-7.2 sdk=6.1 candidate=UNQUALIFIED default_fs=10 supported_fs=2,4,5,8,10; fs=UDP set-fs while stopped; console=removed\n");
    printf("VERSION name=%s version=%s phase=%s build_id=%s build_datetime=%s %s idf=%s target=esp32s3 git=SEE_LOCAL_MIGRATION_MANIFEST\n",
           NOCTI_V4_FW_NAME, NOCTI_V4_FW_VERSION, NOCTI_V4_BUILD_PHASE,
           NOCTI_V4_BUILD_ID, __DATE__, __TIME__, IDF_VER);
}

static void freeze_scan_session_report(esp_err_t frame_drain,
                                       esp_err_t network_drain)
{
    if (!s_session_report.active) return;
    (void)nocti_v4_time_now_us(&s_timebase,
                               &s_session_report.stop_timestamp_us);
    s_session_report.frame_consumer_drain_result = frame_drain;
    s_session_report.network_drain_result = network_drain;
    /* frames_total is the number scientifically completed. A completed-frame
     * ring overflow means ownership was not emitted to the diagnostic/network
     * fan-out, so keep the frozen "emitted" field exact even under failure. */
    s_session_report.frames_emitted =
        s_health.frames_total >= s_health.completed_frame_ring_overflow ?
        s_health.frames_total - s_health.completed_frame_ring_overflow : 0u;
    s_session_report.warmup_frames = s_health.frames_startup_warmup;
    s_session_report.warmup_failures = s_health.frames_startup_warmup_failed;
    s_session_report.target_frames = s_health.frames_target_valid;
    s_session_report.partial_frames = s_health.frames_partial;
    s_session_report.invalid_frames = s_health.frames_invalid_runtime;
    s_session_report.aborted_frames = s_health.frames_aborted_by_stop;
    s_session_report.on_0_to_7 = s_health.candidate_on_0_to_7;
    s_session_report.on_8_to_9 = s_health.candidate_on_8_to_9;
    s_session_report.on_ge10 = s_health.candidate_on_10_to_12 +
                               s_health.candidate_on_gt_12;
    s_session_report.off_0_to_7 = s_health.candidate_off_0_to_7;
    s_session_report.off_8_to_9 = s_health.candidate_off_8_to_9;
    s_session_report.off_ge10 = s_health.candidate_off_10_to_12 +
                                s_health.candidate_off_gt_12;
    s_session_report.network_enqueued = s_health.udp_frames_enqueued -
        s_session_report.network_enqueued_at_start;
    s_session_report.network_sent = s_health.udp_frames_sent -
        s_session_report.network_sent_at_start;
    s_session_report.network_dropped = s_health.udp_frames_dropped -
        s_session_report.network_dropped_at_start;
    s_session_report.network_errors = s_health.udp_send_errors -
        s_session_report.network_errors_at_start;
    s_session_report.console_rejected =
        s_health.console_commands_rejected_during_scan -
        s_session_report.console_rejected_at_start;
    s_session_report.adc_ok = s_health.adc_ok;
    s_session_report.adc_missed = s_health.adc_missed;
    s_session_report.adc_sequence_gap = s_health.adc_sequence_gap;
    s_session_report.drdy_event_overflow = s_health.drdy_event_overflow;
    s_session_report.adc_ring_overflow = s_health.adc_ring_overflow;
    s_session_report.crc_bad = s_health.crc_bad;
    s_session_report.spi_errors = s_health.spi_errors;
    s_session_report.ads_status_faults = s_health.ads_status_faults;
    s_session_report.scheduler_missed_deadline =
        s_health.scheduler_missed_deadline;
    s_session_report.scheduler_event_overflow =
        s_health.scheduler_event_overflow;
    s_session_report.led_completion_timeout = s_health.led_completion_timeout;
    s_session_report.stale_i2c_completion = s_health.stale_i2c_completion;
    s_session_report.scan_session_faults = s_health.scan_session_faults;
    s_session_report.active = false;
    s_session_report.valid = true;
}

static void report_result(const char *command, esp_err_t err)
{
    printf("%s %s result=%s\n", command, err == ESP_OK ? "OK" : "FAIL",
           nocti_v4_err_name(err));
}

static esp_err_t scan_start(void);
static esp_err_t scan_stop(void);
#include "v4_nn2_control.inc"

static esp_err_t scan_start(void)
{
    if (!control_start_allowed()) return ESP_ERR_INVALID_STATE;
    if (!nocti_v4_ads_ready(&s_ads) || nocti_v4_ads_is_active(&s_ads) ||
        nocti_v4_scheduler_is_active(&s_scheduler)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = nocti_v4_led_all_off(&s_led);
    if (result==ESP_OK && !control_start_allowed()) result=ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nocti_v4_async_led_enter(&s_async_led);
    if (result==ESP_OK && !control_start_allowed()) result=ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nocti_v4_optical_prepare_scan(&s_optical);
    if (result==ESP_OK && !control_start_allowed()) result=ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nocti_v4_ads_start_dark(&s_ads);
    if (result == ESP_OK) {
        /* From this point through safe stop, suppress framework logs as well
         * as command echo. The one-line SCAN_START acknowledgement is absorbed
         * by the explicit warm-up frame. */
        esp_log_level_set("*", ESP_LOG_NONE);
    }
    if (result==ESP_OK && !control_start_allowed()) result=ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nocti_v4_scheduler_start(&s_scheduler);
    if (result == ESP_OK) {
        nocti_v4_scheduler_status_t scheduler_status;
        nocti_v4_scheduler_get_status(&s_scheduler, &scheduler_status);
        result = nocti_v4_optical_begin_scan(&s_optical, scheduler_status.session_epoch);
        if (result == ESP_OK) {
            memset(&s_session_report, 0, sizeof(s_session_report));
            s_session_report.active = true;
            s_session_report.session_epoch = scheduler_status.session_epoch;
            s_session_report.start_timestamp_us = scheduler_status.frame_epoch_us;
            s_session_report.network_enqueued_at_start = s_health.udp_frames_enqueued;
            s_session_report.network_sent_at_start = s_health.udp_frames_sent;
            s_session_report.network_dropped_at_start = s_health.udp_frames_dropped;
            s_session_report.network_errors_at_start = s_health.udp_send_errors;
            s_session_report.console_rejected_at_start =
                s_health.console_commands_rejected_during_scan;
            nocti_v4_network_begin_scan_session(&s_network,
                                                 scheduler_status.session_epoch);
            return ESP_OK;
        }
    }
    (void)nocti_v4_scheduler_stop(&s_scheduler);
    nocti_v4_optical_begin_stop(&s_optical);
    if (nocti_v4_ads_is_active(&s_ads)) (void)nocti_v4_ads_stop_dark(&s_ads);
    (void)nocti_v4_optical_finish_stop(&s_optical);
    if (nocti_v4_async_led_is_active(&s_async_led)) (void)nocti_v4_async_led_exit(&s_async_led);
    esp_log_level_set("*", ESP_LOG_INFO);
    return result;
}

static esp_err_t scan_stop(void)
{
    const esp_err_t scheduler_result = nocti_v4_scheduler_stop(&s_scheduler);
    nocti_v4_optical_begin_stop(&s_optical);
    const esp_err_t ads_result = nocti_v4_ads_is_active(&s_ads) ?
        nocti_v4_ads_stop_dark(&s_ads) : ESP_OK;
    const esp_err_t optical_result = nocti_v4_optical_finish_stop(&s_optical);
    const esp_err_t frame_drain_result =
        nocti_v4_optical_wait_frame_consumer_idle(&s_optical, 500u);
    const esp_err_t network_drain_result = CONFIG_NOCTI_V4_NN2_COMMUNICATION ? ESP_OK :
        nocti_v4_network_end_scan_session(&s_network, 500u);
    control_allow_safe_restore();
    const esp_err_t async_result = nocti_v4_async_led_is_active(&s_async_led) ?
        nocti_v4_async_led_exit(&s_async_led) : ESP_OK;
    freeze_scan_session_report(frame_drain_result, network_drain_result);
    esp_log_level_set("*", ESP_LOG_INFO);
    if (scheduler_result != ESP_OK) return scheduler_result;
    if (ads_result != ESP_OK) return ads_result;
    if (optical_result != ESP_OK) return optical_result;
    /* Network and diagnostic-consumer drains are bounded and retained in the
     * frozen report, but they cannot make safe LED shutdown depend on Wi-Fi. */
    return async_result;
}

void app_main(void)
{
    s_app_main_core = (int32_t)xPortGetCoreID();
    esp_log_level_set("*", ESP_LOG_INFO);
    nocti_v4_health_reset(&s_health);
    print_version();
    printf("BOOT phase=SAFE_GPIO: asserting PCA RESET# before I2C/PCA initialization; LED physical state=UNVERIFIED until external observation.\n");

    esp_err_t err = nocti_v4_board_early_safe_init(&s_board);
    if (err != ESP_OK) {
        printf("BOOT_FAIL stage=SAFE_GPIO result=%s; optical safety is UNKNOWN\n", nocti_v4_err_name(err));
    } else if (s_app_main_core != NOCTI_V4_APP_MAIN_CORE) {
        err = ESP_ERR_INVALID_STATE;
        printf("BOOT_FAIL stage=RT_CORE_CONTRACT result=%s app_main_core=%" PRId32
               " expected_core=%d; RESET# remains asserted=%u\n",
               nocti_v4_err_name(err), s_app_main_core,
               NOCTI_V4_APP_MAIN_CORE,
               s_board.pca_reset_asserted ? 1u : 0u);
    } else {
        printf("BOOT_RT_CORE_CONTRACT OK app_main_core=%" PRId32
               " ads_prio=%u led_prio=%u sched_prio=%u"
               " ads_budget_us=%u console=removed control=UDP3333"
               " frames=unsupported\n",
               s_app_main_core, (unsigned)NOCTI_V4_RTOS_PRIO_ADS,
               (unsigned)NOCTI_V4_RTOS_PRIO_LED, (unsigned)NOCTI_V4_RTOS_PRIO_SCHED,
               (unsigned)NOCTI_V4_RTOS_BUDGET_ADS_SERVICE_US);
        err = nocti_v4_i2c_init_sync(&s_i2c, &s_health);
        if (err != ESP_OK) {
            printf("BOOT_FAIL stage=I2C_SYNC_QDEPTH_0 result=%s; RESET# remains asserted=%u\n",
                   nocti_v4_err_name(err), s_board.pca_reset_asserted ? 1u : 0u);
        } else {
            err = nocti_v4_led_init(&s_led, &s_board, &s_i2c, &s_health);
            report_result("BOOT_PCA_SYNC_INIT", err);
            if (err != ESP_OK) {
                printf("BOOT_PCA_SYNC_INIT failure leaves RESET# asserted=%u safe_off=%s\n",
                       s_board.pca_reset_asserted ? 1u : 0u,
                       nocti_v4_safe_off_state_name(s_led.safe_off_state));
            }
        }
    }
    if (err == ESP_OK) {
        err = nocti_v4_ads_init(&s_ads, &s_board, &s_timebase, &s_health);
        report_result("BOOT_ADS_DARK_INIT", err);
    } else {
        printf("BOOT_ADS_DARK_INIT SKIPPED reason=PCA_SAFE_OFF_NOT_VERIFIED\n");
    }
    const esp_err_t async_init = s_i2c.initialized && s_led.initialized ?
        nocti_v4_async_led_init(&s_async_led, &s_board, &s_i2c, &s_led,
                                 &s_timebase, &s_health) : ESP_ERR_INVALID_STATE;
    report_result("BOOT_ASYNC_LED_TEST_INIT", async_init);
    esp_err_t scheduler_init = async_init == ESP_OK && nocti_v4_ads_ready(&s_ads) ?
        nocti_v4_scheduler_init(&s_scheduler, &s_timebase, &s_async_led, &s_health) :
        ESP_ERR_INVALID_STATE;
    if (scheduler_init == ESP_OK) {
        scheduler_init = nocti_v4_optical_init(&s_optical, &s_ads, &s_scheduler,
                                                &s_timebase, &s_health);
    }
    report_result("BOOT_V4_5_SCHEDULER_INIT", scheduler_init);
    esp_err_t network_init = scheduler_init == ESP_OK ?
#if CONFIG_NOCTI_V4_NN2_COMMUNICATION
        control_init() :
#else
        nocti_v4_network_init(&s_network, &s_timebase, &s_health) :
#endif
        ESP_ERR_INVALID_STATE;
    if (network_init == ESP_OK && !CONFIG_NOCTI_V4_NN2_COMMUNICATION) {
        network_init = nocti_v4_optical_bind_completed_frame_sink(
            &s_optical, nocti_v4_network_frame_sink, &s_network);
    }
    report_result("BOOT_V4_7_NETWORK_INIT", network_init);
    printf("CONTROL udp=3333 commands=info,get-config,start,stop,status,health,timing; USB command console removed\n");
}
