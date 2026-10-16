#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "driver/usb_serial_jtag.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
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
    printf("VERSION name=%s version=%s phase=%s build_id=%s build_datetime=%s %s idf=%s target=esp32s3 git=UNAVAILABLE_NOT_A_GIT_WORKTREE\n",
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

static void print_scan_session_report(void)
{
    if (!s_session_report.valid) {
        printf("SCAN_SESSION_REPORT state=NO_COMPLETED_SESSION\n");
        return;
    }
    const uint64_t duration = s_session_report.stop_timestamp_us >=
        s_session_report.start_timestamp_us ?
        s_session_report.stop_timestamp_us -
        s_session_report.start_timestamp_us : 0u;
    printf("SCAN_SESSION_REPORT session=%" PRIu32 " duration_us=%" PRIu64
           " frames_emitted=%" PRIu32 " warmup_frames=%" PRIu32
           " warmup_failures=%" PRIu32 " target=%" PRIu32
           " partial=%" PRIu32 " invalid_runtime=%" PRIu32
           " aborted=%" PRIu32 " frame_consumer_drain=%s network_drain=%s\n",
           s_session_report.session_epoch, duration,
           s_session_report.frames_emitted, s_session_report.warmup_frames,
           s_session_report.warmup_failures, s_session_report.target_frames,
           s_session_report.partial_frames, s_session_report.invalid_frames,
           s_session_report.aborted_frames,
           nocti_v4_err_name(s_session_report.frame_consumer_drain_result),
           nocti_v4_err_name(s_session_report.network_drain_result));
    printf("SCAN_SESSION_K ON_0_7=%" PRIu32 " ON_8_9=%" PRIu32
           " ON_GE10=%" PRIu32 " OFF_0_7=%" PRIu32
           " OFF_8_9=%" PRIu32 " OFF_GE10=%" PRIu32 "\n",
           s_session_report.on_0_to_7, s_session_report.on_8_to_9,
           s_session_report.on_ge10, s_session_report.off_0_to_7,
           s_session_report.off_8_to_9, s_session_report.off_ge10);
    printf("SCAN_SESSION_ADC adc_ok=%" PRIu32 " missed=%" PRIu32
           " sequence_gap=%" PRIu32 " event_overflow=%" PRIu32
           " raw_overflow=%" PRIu32 " crc_bad=%" PRIu32
           " spi_errors=%" PRIu32 " status_faults=%" PRIu32 "\n",
           s_session_report.adc_ok, s_session_report.adc_missed,
           s_session_report.adc_sequence_gap,
           s_session_report.drdy_event_overflow,
           s_session_report.adc_ring_overflow, s_session_report.crc_bad,
           s_session_report.spi_errors, s_session_report.ads_status_faults);
    printf("SCAN_SESSION_SCHED missed_deadline=%" PRIu32
           " event_overflow=%" PRIu32 " completion_timeout=%" PRIu32
           " stale_completion=%" PRIu32 " scan_faults=%" PRIu32 "\n",
           s_session_report.scheduler_missed_deadline,
           s_session_report.scheduler_event_overflow,
           s_session_report.led_completion_timeout,
           s_session_report.stale_i2c_completion,
           s_session_report.scan_session_faults);
    printf("SCAN_SESSION_NETWORK enqueued=%" PRIu32 " sent=%" PRIu32
           " dropped=%" PRIu32 " send_errors=%" PRIu32
           " console_rejected=%" PRIu32 "\n",
           s_session_report.network_enqueued, s_session_report.network_sent,
           s_session_report.network_dropped, s_session_report.network_errors,
           s_session_report.console_rejected);
}

static void print_health(void)
{
    nocti_v4_ads_sample_stack_watermarks(&s_ads);
    nocti_v4_async_led_sample_stack_watermark(&s_async_led);
    nocti_v4_scheduler_sample_stack_watermark(&s_scheduler);
    nocti_v4_optical_sample_stack_watermarks(&s_optical);
    printf("HEALTH_PCA i2c_init_errors=%" PRIu32 " pca_presence_fail=%" PRIu32
           " pca_write_errors=%" PRIu32 " pca_read_errors=%" PRIu32
           " pca_readback_mismatch=%" PRIu32 " iref_mismatch=%" PRIu32
           " led_on_commands=%" PRIu32 " led_off_commands=%" PRIu32
           " led_all_off_commands=%" PRIu32 " pca_resets=%" PRIu32
           " pca_reinits=%" PRIu32 " safe_off_failures=%" PRIu32
           " invalid_led_command=%" PRIu32
           " unknown_console_command=%" PRIu32
           " console_deferred_during_scan=%" PRIu32
           " console_rejected_during_scan=%" PRIu32 "\n",
           s_health.i2c_init_errors, s_health.pca_presence_fail,
           s_health.pca_write_errors, s_health.pca_read_errors,
           s_health.pca_readback_mismatch, s_health.iref_mismatch,
           s_health.led_on_commands, s_health.led_off_commands,
           s_health.led_all_off_commands, s_health.pca_resets,
           s_health.pca_reinits, s_health.safe_off_failures,
           s_health.invalid_led_command, s_health.unknown_console_command,
           s_health.console_commands_deferred_during_scan,
           s_health.console_commands_rejected_during_scan);
    printf("HEALTH_ADC adc_ok=%" PRIu32 " adc_missed=%" PRIu32
           " adc_missed_sequence=%" PRIu32 " adc_missed_backlog=%" PRIu32
           " adc_missed_other=%" PRIu32 " "
           " adc_sequence_gap=%" PRIu32 " drdy_event_overflow=%" PRIu32
           " adc_ring_overflow=%" PRIu32 " crc_bad=%" PRIu32
           " spi_errors=%" PRIu32 " ads_status_faults=%" PRIu32
           " unexpected_f_resync=%" PRIu32 " dark_sessions_started=%" PRIu32
           " dark_sessions_stopped=%" PRIu32 " dark_session_faults=%" PRIu32
           " raw_samples_consumed=%" PRIu32
           " adc_period_min_us=%" PRIu32 " adc_period_max_us=%" PRIu32
           " adc_period_avg_us=%" PRIu32 " adc_max_gap_us=%" PRIu32
           " spi_duration_min_us=%" PRIu32 " spi_duration_max_us=%" PRIu32
           " spi_duration_avg_us=%" PRIu32
           " ads_task_stack_watermark=%" PRIu32
           " dark_task_stack_watermark=%" PRIu32
           " ads_task_core=%" PRId32 " dark_task_core=%" PRId32 "\n",
           s_health.adc_ok, s_health.adc_missed, s_health.adc_missed_sequence,
           s_health.adc_missed_backlog, s_health.adc_missed_other,
           s_health.adc_sequence_gap,
           s_health.drdy_event_overflow, s_health.adc_ring_overflow,
           s_health.crc_bad, s_health.spi_errors, s_health.ads_status_faults,
           s_health.unexpected_f_resync, s_health.dark_sessions_started,
           s_health.dark_sessions_stopped, s_health.dark_session_faults,
           s_health.raw_samples_consumed, s_health.adc_period_min_us,
           s_health.adc_period_max_us, s_health.adc_period_avg_us,
           s_health.adc_max_gap_us, s_health.spi_duration_min_us,
           s_health.spi_duration_max_us, s_health.spi_duration_avg_us,
           s_health.ads_task_stack_watermark_bytes,
           s_health.dark_consumer_task_stack_watermark_bytes,
           s_health.ads_task_core, s_health.dark_consumer_task_core);
    printf("HEALTH_TIMING_OUTLIERS drdy_lt_100_us=%" PRIu32
           " drdy_100_to_150_us=%" PRIu32 " drdy_150_to_250_us=%" PRIu32
           " drdy_gt_250_us=%" PRIu32 " spi_gt_50_us=%" PRIu32
           " spi_gt_100_us=%" PRIu32 " spi_gt_250_us=%" PRIu32 "\n",
           s_health.drdy_period_lt_100_us, s_health.drdy_period_100_to_150_us,
           s_health.drdy_period_150_to_250_us, s_health.drdy_period_gt_250_us,
           s_health.spi_duration_gt_50_us, s_health.spi_duration_gt_100_us,
           s_health.spi_duration_gt_250_us);
    printf("HEALTH_TASKS ads_core=%" PRId32 " dark_core=%" PRId32
           " led_core=%" PRId32 " scheduler_core=%" PRId32
           " gptimer_callback_core=%" PRId32 " ads_stack=%" PRIu32
           " dark_stack=%" PRIu32 " led_stack=%" PRIu32 " scheduler_stack=%" PRIu32 "\n",
           s_health.ads_task_core, s_health.dark_consumer_task_core,
           s_health.async_led_task_core, s_health.scheduler_task_core,
           s_health.scheduler_gptimer_callback_core,
           s_health.ads_task_stack_watermark_bytes,
           s_health.dark_consumer_task_stack_watermark_bytes,
           s_health.async_led_task_stack_watermark_bytes,
           s_health.scheduler_task_stack_watermark_bytes);
    nocti_v4_async_led_print_health(&s_async_led);
    nocti_v4_scheduler_print_health(&s_scheduler);
    nocti_v4_optical_print_health(&s_optical);
    nocti_v4_network_print_health(&s_network);
}

static void print_adc_status(void)
{
    nocti_v4_ads_status_snapshot_t status;
    nocti_v4_ads_get_status(&s_ads, &status);
    printf("ADC_STATUS interface_initialized=%u hardware_initialized=%u ready=%u rate_valid=%u lifecycle=%s session_epoch=%" PRIu32
           " expected_sps=%" PRIu32 " measured_sps=%" PRIu32
           " validation_samples=%" PRIu32 " validation_period_avg_us=%" PRIu32
           " validation_period_min_us=%" PRIu32 " validation_period_max_us=%" PRIu32
           " raw_depth=%" PRIu32 "/%u event_depth=%" PRIu32 "/%u integrity_fault=%u"
           " ads_task_core=%" PRId32 " dark_task_core=%" PRId32
           " gpio_isr_install_core=%" PRId32 " gpio_isr_callback_core=%" PRId32
           " last_error=%s last_fault_code=%s last_fault_origin=%s last_fault_error=%s\n",
           status.interface_initialized ? 1u : 0u,
           status.hardware_initialized ? 1u : 0u, status.ready ? 1u : 0u,
           status.rate_valid ? 1u : 0u,
           nocti_v4_ads_lifecycle_name(status.lifecycle), status.session_epoch,
           status.configured_sps, status.measured_sps, status.validation_samples,
           status.validation_period_avg_us, status.validation_period_min_us,
           status.validation_period_max_us, status.raw_ring_depth,
           NOCTI_V4_RAW_SAMPLE_RING_CAPACITY, status.drdy_event_ring_depth,
           NOCTI_V4_DRDY_EVENT_RING_CAPACITY,
           status.session_integrity_fault ? 1u : 0u, status.ads_task_core,
           status.dark_consumer_task_core, status.gpio_isr_install_core,
           status.gpio_isr_callback_core, nocti_v4_err_name(status.last_error),
           nocti_v4_ads_fault_code_name(status.last_fault_code),
           nocti_v4_ads_fault_origin_name(status.last_fault_origin),
           nocti_v4_err_name(status.last_fault_error));
}

static void print_adc_debug(void)
{
    nocti_v4_ads_status_snapshot_t status;
    nocti_v4_ads_get_status(&s_ads, &status);
    printf("ADC_DEBUG drdy_isr_count=%" PRIu32 " drdy_events_pushed=%" PRIu32
           " drdy_events_popped=%" PRIu32 " drdy_isr_timestamp_errors=%" PRIu32
           " drdy_event_overflow=%" PRIu32 " ads_frames_read_total=%" PRIu32
           " ads_frames_read_runtime=%" PRIu32 " post_sync_frames_read=%" PRIu32
           " post_sync_frames_discarded=%" PRIu32 " raw_samples_pushed=%" PRIu32
           " raw_samples_consumed=%" PRIu32 " event_depth_current=%" PRIu32
           " event_depth_max=%" PRIu32 " adc_missed_sequence=%" PRIu32
           " adc_missed_backlog=%" PRIu32 " adc_missed_other=%" PRIu32 "\n",
           status.drdy_isr_count, status.drdy_events_pushed,
           status.drdy_events_popped, status.drdy_isr_timestamp_errors,
           status.drdy_event_overflow, status.ads_frames_read_total,
           status.ads_frames_read_runtime, status.post_sync_frames_read,
           status.post_sync_frames_discarded, status.raw_samples_pushed,
           status.raw_samples_consumed, status.drdy_event_ring_depth,
           status.drdy_event_ring_depth_max, status.adc_missed_sequence,
           status.adc_missed_backlog, status.adc_missed_other);
    printf("ADC_DEBUG last_drdy_sequence=%" PRIu32 " last_drdy_timestamp_us=%" PRIu64
           " last_fault_code=%s last_fault_origin=%s last_fault_error=%s"
           " runtime_period_count=%" PRIu32 " runtime_period_us=",
           status.last_drdy_sequence, status.last_drdy_timestamp_us,
           nocti_v4_ads_fault_code_name(status.last_fault_code),
           nocti_v4_ads_fault_origin_name(status.last_fault_origin),
           nocti_v4_err_name(status.last_fault_error), status.runtime_period_count);
    for (uint32_t index = 0u; index < status.runtime_period_count; ++index) {
        printf("%s%" PRIu32, index == 0u ? "" : ",", status.runtime_period_us[index]);
    }
    printf("\n");
    printf("ADC_DEBUG timing_outliers drdy_lt_100_us=%" PRIu32
           " drdy_100_to_150_us=%" PRIu32 " drdy_150_to_250_us=%" PRIu32
           " drdy_gt_250_us=%" PRIu32 " spi_gt_50_us=%" PRIu32
           " spi_gt_100_us=%" PRIu32 " spi_gt_250_us=%" PRIu32 "\n",
           status.drdy_period_lt_100_us, status.drdy_period_100_to_150_us,
           status.drdy_period_150_to_250_us, status.drdy_period_gt_250_us,
           status.spi_duration_gt_50_us, status.spi_duration_gt_100_us,
           status.spi_duration_gt_250_us);
}

static void print_adc_regs(void)
{
    nocti_v4_ads_register_snapshot_t regs;
    nocti_v4_ads_get_registers(&s_ads, &regs);
    printf("ADC_REGS ID=0x%04X STATUS=0x%04X MODE=0x%04X CLOCK=0x%04X GAIN=0x%04X CFG=0x%04X CH_CFG=%04X %04X %04X %04X\n",
           regs.id, regs.status, regs.mode, regs.clock, regs.gain, regs.cfg,
           regs.channel_cfg[0], regs.channel_cfg[1], regs.channel_cfg[2],
           regs.channel_cfg[3]);
}

static void print_adc_raw_stats(void)
{
    nocti_v4_ads_raw_stats_snapshot_t stats;
    nocti_v4_ads_get_raw_stats(&s_ads, &stats);
    if (!stats.valid) {
        printf("ADC_RAW_STATS state=NO_COMPLETE_RUNTIME_WINDOW window_samples=%u\n",
               NOCTI_V4_ADS_RAW_STATS_WINDOW_SAMPLES);
        return;
    }
    printf("ADC_RAW_STATS session=%" PRIu32 " window=%" PRIu32
           " count=%" PRIu32 " source=ADS_RUNTIME_PRE_RAW_RING\n",
           stats.session_epoch, stats.window_sequence, stats.count);
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        printf("ADC_RAW_CHANNEL channel=%u count=%" PRIu32
               " min=%" PRId32 " max=%" PRId32 " mean=%" PRId64 "\n",
               channel, stats.count, stats.minimum[channel],
               stats.maximum[channel], stats.mean[channel]);
    }
}

static void print_timing(void)
{
    nocti_v4_ads_status_snapshot_t status;
    nocti_v4_ads_get_status(&s_ads, &status);
    printf("TIMING drdy_expected_period_us=%u drdy_min_us=%" PRIu32
           " drdy_max_us=%" PRIu32 " drdy_avg_us=%" PRIu32
           " drdy_max_gap_us=%" PRIu32 " spi_min_us=%" PRIu32
           " spi_max_us=%" PRIu32 " spi_avg_us=%" PRIu32
           " validation_sps=%" PRIu32 " validation_period_us=%" PRIu32
           " event_depth_max=%" PRIu32 " raw_depth_max=%" PRIu32 "\n",
           NOCTI_V4_ADS_EXPECTED_PERIOD_US, s_health.adc_period_min_us,
           s_health.adc_period_max_us, s_health.adc_period_avg_us,
           s_health.adc_max_gap_us, s_health.spi_duration_min_us,
           s_health.spi_duration_max_us, s_health.spi_duration_avg_us,
           status.measured_sps, status.validation_period_avg_us,
           status.drdy_event_ring_depth_max, status.raw_ring_depth_max);
    printf("TIMING_HIST drdy_lt_100_us=%" PRIu32 " drdy_100_to_150_us=%" PRIu32
           " drdy_150_to_250_us=%" PRIu32 " drdy_gt_250_us=%" PRIu32
           " spi_gt_50_us=%" PRIu32 " spi_gt_100_us=%" PRIu32
           " spi_gt_250_us=%" PRIu32 "\n",
           status.drdy_period_lt_100_us, status.drdy_period_100_to_150_us,
           status.drdy_period_150_to_250_us, status.drdy_period_gt_250_us,
           status.spi_duration_gt_50_us, status.spi_duration_gt_100_us,
           status.spi_duration_gt_250_us);
}

static void print_status(void)
{
    nocti_v4_async_led_status_t async_status;
    nocti_v4_scheduler_status_t scheduler_status;
    nocti_v4_async_led_get_status(&s_async_led, &async_status);
    nocti_v4_scheduler_get_status(&s_scheduler, &scheduler_status);
    printf("STATUS phase=V4-1_THROUGH_V4-6_HARDWARE_VERIFIED+V4-7_1_WIFI_RT_CORE_ISOLATION_BUILD ads=DARK_PLUS_SCAN_OPTICAL scheduler=ABSOLUTE_20_SLOT_SHORT_NORMAL_OFF async_i2c=RUNTIME_800KHZ_SCAN_ONLY sync_i2c_hz=%u runtime_i2c_hz=%u configured_off_phase_us=%" PRIu32 " active_off_phase_us=%" PRIu32 " scan_warmup_frames=%u quiet_scan_console=1 wifi_runtime=SOFTAP udp_runtime=COMPLETED_FRAME_CONSUMER core_contract=wifi_tcpip_optical_frame_network:0,main_ads_scheduler_led_i2c:1 app_main_core=%" PRId32 " mbll_runtime=NOT_IMPLEMENTED ssr_runtime=NOT_IMPLEMENTED boot_gpio_safe=%u i2c_ready=%u i2c_live_hz=%" PRIu32 " i2c_qdepth=%" PRIu32 " i2c_callbacks_registered=%u pca_io_mode=%s\n",
           NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ,
           NOCTI_V4_I2C_SCAN_RUNTIME_HZ,
           scheduler_status.configured_off_phase_us,
            scheduler_status.active_off_phase_us,
            NOCTI_V4_SCAN_WARMUP_FRAMES,
            s_app_main_core,
            s_board.gpio_ready ? 1u : 0u, s_i2c.initialized ? 1u : 0u,
           s_i2c.clock_hz, s_i2c.queue_depth,
           async_status.callbacks_registered ? 1u : 0u,
           nocti_v4_pca_io_mode_name(async_status.mode));
    if (async_status.mode == PCA_IO_MODE_SYNC) {
        nocti_v4_led_print_status(&s_led);
    } else {
        printf("LED_STATUS sync_access=QUIESCED reason=%s\n",
               nocti_v4_pca_io_mode_name(async_status.mode));
    }
    nocti_v4_async_led_print_status(&s_async_led);
    nocti_v4_scheduler_print_status(&s_scheduler);
    nocti_v4_optical_print_status(&s_optical);
    nocti_v4_network_print_status(&s_network);
    print_adc_status();
    print_health();
}

static void print_help(void)
{
    printf("HELP commands: HELP VERSION STATUS HEALTH TIMING ADC_DEBUG ADC_RAW_STATS ADC_FRAME_DUMP [count:1..4] ADC_CHANNEL_REGS ADC_CH0_SELFTEST RESET_COUNTERS ADC_STATUS ADC_REGS DARK_START DARK_STOP LED_STATUS LED_MAP LED_ALL_OFF LED_ON <source:0..19> LED_OFF LED_IREF <source:0..19> <mA:0..50> LED_READBACK PCA_RESET PCA_REINIT I2C_STATUS I2C_FMP_QUALIFY I2C_FMP_STATUS ASYNC_ENTER ASYNC_STATUS ASYNC_LED_ON_SINGLE <source:0..19> ASYNC_LED_OFF_SINGLE ASYNC_LED_ON_BURST <source:0..19> ASYNC_LED_ALL_OFF_BURST ASYNC_HEALTH ASYNC_TIMING LED_TIMING_DETAIL ASYNC_EXIT SCAN_OFF_PHASE <2200|2500> SCAN_START SCAN_STOP SCAN_STATUS SCAN_SESSION_REPORT SCHED_STATUS SCHED_TIMING DUMP_SLOT_TIMING DUMP_LED_PAYLOADS DUMP_OFF_PAYLOADS SCAN_HEALTH TIMING_PHASE_SWEEP DUMP_OPTICAL_MAP FRAME_STATUS FRAME_HEALTH DUMP_FRAME DUMP_K_ANOMALIES DUMP_CLASSIFY_MISMATCH NETWORK_STATUS NETWORK_HEALTH\n");
    printf("HELP policy: boot/synchronous diagnostics remain 400 kHz; isolated async and SCAN use 800 kbit/s with external 4.7-kOhm pull-ups to 3.3 V and internal pull-ups disabled. SCAN uses 7-byte one-hot ON plus invariant-gated 2-byte normal OFF at 2200 us; STOP/fault/recovery retain 7-byte ALL_OFF and RESET fail-safe. Frame 1 is STARTUP_WARMUP and is not scientific/network output. While RUNNING_SCAN only SCAN_STOP is accepted; all other serial input is silently rejected and counted. Wi-Fi SoftAP NOCTINIRS uses control UDP 3333 and stream UDP 3334; UDP control permits PING, HELLO, INFO, STREAM_START (one V4 packet/frame), STREAM_START_COMPAT (legacy paired+V4), and STREAM_STOP only. MBLL, SSR, and bandpass remain NOT_IMPLEMENTED.\n");
}

static void report_result(const char *command, esp_err_t err)
{
    printf("%s %s result=%s\n", command, err == ESP_OK ? "OK" : "FAIL",
           nocti_v4_err_name(err));
}

static bool reject_if_dark_active(const char *command)
{
    if (!nocti_v4_ads_is_active(&s_ads)) {
        return false;
    }
    printf("%s FAIL result=ESP_ERR_INVALID_STATE reason=DARK_ACTIVE\n", command);
    return true;
}

static bool reject_if_scan_active(const char *command)
{
    if (!nocti_v4_scheduler_is_active(&s_scheduler)) {
        return false;
    }
    printf("%s FAIL result=ESP_ERR_INVALID_STATE reason=SCAN_ACTIVE\n", command);
    return true;
}

static bool reject_if_async_active(const char *command)
{
    if (!nocti_v4_async_led_is_active(&s_async_led)) {
        return false;
    }
    printf("%s FAIL result=ESP_ERR_INVALID_STATE reason=%s\n", command,
           nocti_v4_pca_io_mode_name(nocti_v4_async_led_mode(&s_async_led)));
    return true;
}

static bool reject_if_dark_or_async_active(const char *command)
{
    return reject_if_dark_active(command) || reject_if_async_active(command);
}

static esp_err_t scan_start(void)
{
    if (!nocti_v4_ads_ready(&s_ads) || nocti_v4_ads_is_active(&s_ads) ||
        nocti_v4_scheduler_is_active(&s_scheduler)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = nocti_v4_led_all_off(&s_led);
    if (result == ESP_OK) result = nocti_v4_async_led_enter(&s_async_led);
    if (result == ESP_OK) result = nocti_v4_optical_prepare_scan(&s_optical);
    if (result == ESP_OK) result = nocti_v4_ads_start_dark(&s_ads);
    if (result == ESP_OK) {
        /* From this point through safe stop, suppress framework logs as well
         * as command echo. The one-line SCAN_START acknowledgement is absorbed
         * by the explicit warm-up frame. */
        esp_log_level_set("*", ESP_LOG_NONE);
    }
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
    const esp_err_t network_drain_result =
        nocti_v4_network_end_scan_session(&s_network, 500u);
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

static bool parse_source(const char *command, char **context, uint8_t *out_source)
{
    char *source_text = strtok_r(NULL, " \t\r\n", context);
    uint32_t source = 0u;
    if (!nocti_v4_parse_u32(source_text, &source) || source >= NOCTI_V4_OPTICAL_SOURCE_COUNT) {
        s_health.invalid_led_command++;
        printf("%s FAIL result=ESP_ERR_INVALID_ARG expected_source=0..19\n", command);
        return false;
    }
    *out_source = (uint8_t)source;
    return true;
}

static void execute_command(char *line)
{
    char *context = NULL;
    char *command = strtok_r(line, " \t\r\n", &context);
    if (command == NULL) {
        return;
    }
    if (strcasecmp(command, "HELP") == 0) {
        print_help();
    } else if (strcasecmp(command, "VERSION") == 0) {
        print_version();
    } else if (strcasecmp(command, "STATUS") == 0) {
        print_status();
    } else if (strcasecmp(command, "HEALTH") == 0) {
        print_health();
    } else if (strcasecmp(command, "TIMING") == 0) {
        print_timing();
    } else if (strcasecmp(command, "ADC_STATUS") == 0) {
        print_adc_status();
    } else if (strcasecmp(command, "ADC_DEBUG") == 0) {
        print_adc_debug();
    } else if (strcasecmp(command, "ADC_RAW_STATS") == 0) {
        print_adc_raw_stats();
    } else if (strcasecmp(command, "ADC_FRAME_DUMP") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        uint32_t count = NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES;
        char *count_text = strtok_r(NULL, " \t\r\n", &context);
        if (count_text != NULL &&
            (!nocti_v4_parse_u32(count_text, &count) || count == 0u ||
             count > NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES)) {
            printf("ADC_FRAME_DUMP FAIL result=ESP_ERR_INVALID_ARG expected_count=1..%u\n",
                   NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES);
            return;
        }
        const esp_err_t err = nocti_v4_ads_capture_frame_dump(&s_ads,
                                                               (uint8_t)count);
        report_result(command, err);
        nocti_v4_ads_print_frame_dump(&s_ads);
    } else if (strcasecmp(command, "ADC_CHANNEL_REGS") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        const esp_err_t err = nocti_v4_ads_capture_channel_registers(&s_ads);
        report_result(command, err);
        nocti_v4_ads_print_channel_registers(&s_ads);
    } else if (strcasecmp(command, "ADC_CH0_SELFTEST") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        const esp_err_t err = nocti_v4_ads_run_ch0_selftest(&s_ads);
        report_result(command, err);
        nocti_v4_ads_print_ch0_selftest(&s_ads);
    } else if (strcasecmp(command, "ADC_REGS") == 0) {
        print_adc_regs();
    } else if (strcasecmp(command, "RESET_COUNTERS") == 0) {
        report_result(command, nocti_v4_ads_reset_counters(&s_ads));
    } else if (strcasecmp(command, "DARK_START") == 0) {
        if (reject_if_async_active(command) || reject_if_scan_active(command)) return;
        esp_err_t err = nocti_v4_led_all_off(&s_led);
        if (err == ESP_OK) {
            err = nocti_v4_ads_start_dark(&s_ads);
        }
        report_result(command, err);
    } else if (strcasecmp(command, "DARK_STOP") == 0) {
        if (reject_if_async_active(command) || reject_if_scan_active(command)) return;
        const esp_err_t stop_err = nocti_v4_ads_stop_dark(&s_ads);
        const esp_err_t off_err = nocti_v4_led_all_off(&s_led);
        report_result(command, stop_err != ESP_OK ? stop_err : off_err);
    } else if (strcasecmp(command, "LED_STATUS") == 0) {
        if (nocti_v4_async_led_is_active(&s_async_led)) {
            printf("LED_STATUS sync_access=QUIESCED reason=%s\n",
                   nocti_v4_pca_io_mode_name(nocti_v4_async_led_mode(&s_async_led)));
        } else {
            nocti_v4_led_print_status(&s_led);
        }
        nocti_v4_async_led_print_status(&s_async_led);
    } else if (strcasecmp(command, "LED_MAP") == 0) {
        nocti_v4_led_print_map();
    } else if (strcasecmp(command, "LED_ALL_OFF") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_led_all_off(&s_led));
    } else if (strcasecmp(command, "LED_OFF") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_led_off(&s_led));
    } else if (strcasecmp(command, "LED_READBACK") == 0) {
        if (reject_if_async_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_led_readback(&s_led, true));
    } else if (strcasecmp(command, "PCA_RESET") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_led_reset_hold(&s_led));
    } else if (strcasecmp(command, "PCA_REINIT") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_led_reinit(&s_led));
    } else if (strcasecmp(command, "I2C_STATUS") == 0) {
        nocti_v4_async_led_status_t async_status;
        nocti_v4_async_led_get_status(&s_async_led, &async_status);
        printf("I2C_STATUS ready=%u port=I2C0 sda=%d scl=%d clock_hz=%" PRIu32
               " sync_diagnostic_hz=%u scan_runtime_hz=%u"
               " trans_queue_depth=%" PRIu32 " i2c_callbacks_registered=%u pca_io_mode=%s"
               " external_pullup_ohm=%u pullup_rail_mv=%u internal_pullups=0 last_result=%s\n",
               s_i2c.initialized ? 1u : 0u, NOCTI_V4_I2C_SDA_GPIO,
               NOCTI_V4_I2C_SCL_GPIO, s_i2c.clock_hz,
               NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ,
               NOCTI_V4_I2C_SCAN_RUNTIME_HZ, s_i2c.queue_depth,
               async_status.callbacks_registered ? 1u : 0u,
               nocti_v4_pca_io_mode_name(async_status.mode),
               NOCTI_V4_I2C_EXTERNAL_PULLUP_OHM,
               NOCTI_V4_I2C_PULLUP_RAIL_MV,
               nocti_v4_err_name(s_i2c.last_result));
    } else if (strcasecmp(command, "I2C_FMP_QUALIFY") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_led_run_fast_i2c_qualification(&s_led));
        nocti_v4_led_print_fast_i2c_qualification();
    } else if (strcasecmp(command, "I2C_FMP_STATUS") == 0) {
        nocti_v4_led_print_fast_i2c_qualification();
    } else if (strcasecmp(command, "ASYNC_ENTER") == 0) {
        if (reject_if_dark_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_async_led_enter(&s_async_led));
        nocti_v4_async_led_print_status(&s_async_led);
    } else if (strcasecmp(command, "ASYNC_EXIT") == 0) {
        if (reject_if_dark_active(command) || reject_if_scan_active(command)) return;
        report_result(command, nocti_v4_async_led_exit(&s_async_led));
        nocti_v4_async_led_print_status(&s_async_led);
    } else if (strcasecmp(command, "ASYNC_STATUS") == 0) {
        nocti_v4_async_led_print_status(&s_async_led);
    } else if (strcasecmp(command, "ASYNC_HEALTH") == 0) {
        nocti_v4_async_led_print_health(&s_async_led);
    } else if (strcasecmp(command, "ASYNC_TIMING") == 0) {
        nocti_v4_async_led_print_timing(&s_async_led);
    } else if (strcasecmp(command, "LED_TIMING_DETAIL") == 0) {
        nocti_v4_async_led_print_timing_detail(&s_async_led);
    } else if (strcasecmp(command, "ASYNC_LED_ON_SINGLE") == 0 ||
               strcasecmp(command, "ASYNC_LED_ON_BURST") == 0) {
        if (reject_if_dark_active(command) || reject_if_scan_active(command)) return;
        uint8_t source = 0u;
        if (!parse_source(command, &context, &source)) return;
        const esp_err_t err = strcasecmp(command, "ASYNC_LED_ON_SINGLE") == 0 ?
            nocti_v4_async_led_on_single(&s_async_led, source) :
            nocti_v4_async_led_on_burst(&s_async_led, source);
        report_result(command, err);
        nocti_v4_async_led_print_last_result(&s_async_led);
    } else if (strcasecmp(command, "ASYNC_LED_OFF_SINGLE") == 0 ||
               strcasecmp(command, "ASYNC_LED_ALL_OFF_BURST") == 0) {
        if (reject_if_dark_active(command) || reject_if_scan_active(command)) return;
        const esp_err_t err = strcasecmp(command, "ASYNC_LED_OFF_SINGLE") == 0 ?
            nocti_v4_async_led_off_single(&s_async_led) :
            nocti_v4_async_led_all_off_burst(&s_async_led);
        report_result(command, err);
        nocti_v4_async_led_print_last_result(&s_async_led);
    } else if (strcasecmp(command, "SCAN_OFF_PHASE") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        char *phase_text = strtok_r(NULL, " \t\r\n", &context);
        uint32_t phase_us = 0u;
        if (!nocti_v4_parse_u32(phase_text, &phase_us) ||
            !NOCTI_V4_OFF_PHASE_IS_SUPPORTED(phase_us)) {
            printf("SCAN_OFF_PHASE FAIL result=ESP_ERR_INVALID_ARG expected_us=2200_or_2500\n");
            return;
        }
        const esp_err_t err = nocti_v4_scheduler_set_off_phase_us(&s_scheduler,
                                                                   phase_us);
        report_result(command, err);
        nocti_v4_scheduler_print_status(&s_scheduler);
    } else if (strcasecmp(command, "SCAN_START") == 0) {
        const esp_err_t err = scan_start();
        /* One bounded transition acknowledgement is permitted and absorbed by
         * frame 1's explicit STARTUP_WARMUP policy. Do not append the large
         * scheduler status dump after RUNNING_SCAN has become authoritative. */
        report_result(command, err);
        if (err != ESP_OK) nocti_v4_scheduler_print_status(&s_scheduler);
    } else if (strcasecmp(command, "SCAN_STOP") == 0) {
        report_result(command, scan_stop());
        nocti_v4_scheduler_print_status(&s_scheduler);
    } else if (strcasecmp(command, "SCAN_SESSION_REPORT") == 0) {
        print_scan_session_report();
    } else if (strcasecmp(command, "SCAN_STATUS") == 0 ||
               strcasecmp(command, "SCHED_STATUS") == 0) {
        nocti_v4_scheduler_print_status(&s_scheduler);
    } else if (strcasecmp(command, "SCHED_TIMING") == 0) {
        nocti_v4_scheduler_print_timing(&s_scheduler);
    } else if (strcasecmp(command, "DUMP_SLOT_TIMING") == 0) {
        nocti_v4_scheduler_dump_slot_timing(&s_scheduler);
    } else if (strcasecmp(command, "DUMP_LED_PAYLOADS") == 0) {
        nocti_v4_async_led_print_payloads(&s_async_led);
    } else if (strcasecmp(command, "DUMP_OFF_PAYLOADS") == 0) {
        nocti_v4_async_led_print_off_payloads(&s_async_led);
    } else if (strcasecmp(command, "SCAN_HEALTH") == 0) {
        nocti_v4_scheduler_print_health(&s_scheduler);
        nocti_v4_optical_print_health(&s_optical);
    } else if (strcasecmp(command, "DUMP_OPTICAL_MAP") == 0) {
        nocti_v4_optical_dump_map();
    } else if (strcasecmp(command, "FRAME_STATUS") == 0) {
        nocti_v4_optical_print_status(&s_optical);
    } else if (strcasecmp(command, "FRAME_HEALTH") == 0) {
        nocti_v4_optical_print_health(&s_optical);
    } else if (strcasecmp(command, "DUMP_FRAME") == 0) {
        nocti_v4_optical_dump_latest_frame(&s_optical);
    } else if (strcasecmp(command, "DUMP_K_ANOMALIES") == 0) {
        nocti_v4_optical_dump_k_anomalies(&s_optical);
    } else if (strcasecmp(command, "DUMP_CLASSIFY_MISMATCH") == 0) {
        nocti_v4_optical_dump_classify_mismatches(&s_optical);
    } else if (strcasecmp(command, "TIMING_PHASE_SWEEP") == 0) {
        nocti_v4_optical_print_phase_sweep(&s_optical);
    } else if (strcasecmp(command, "NETWORK_STATUS") == 0) {
        nocti_v4_network_print_status(&s_network);
    } else if (strcasecmp(command, "NETWORK_HEALTH") == 0) {
        nocti_v4_network_print_health(&s_network);
    } else if (strcasecmp(command, "LED_ON") == 0 ||
               strcasecmp(command, "LED_IREF") == 0) {
        if (reject_if_dark_or_async_active(command) || reject_if_scan_active(command)) return;
        uint8_t source = 0u;
        if (!parse_source(command, &context, &source)) return;
        if (strcasecmp(command, "LED_ON") == 0) {
            report_result(command, nocti_v4_led_on(&s_led, source));
            return;
        }
        char *ma_text = strtok_r(NULL, " \t\r\n", &context);
        uint32_t milliamps = 0u;
        if (!nocti_v4_parse_u32(ma_text, &milliamps) || milliamps > 50u) {
            s_health.invalid_led_command++;
            printf("LED_IREF FAIL result=ESP_ERR_INVALID_ARG expected_mA=0..50\n");
            return;
        }
        report_result(command, nocti_v4_led_set_iref_ma(&s_led, source,
                                                        (uint16_t)milliamps));
    } else {
        s_health.unknown_console_command++;
        printf("UNKNOWN_COMMAND command=%s; run HELP\n", command);
    }
}

static esp_err_t console_input_init(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    return usb_serial_jtag_driver_install(&config);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void print_prompt(void)
{
    if (nocti_v4_scheduler_is_active(&s_scheduler)) return;
    printf("v4> ");
    fflush(stdout);
}

static bool line_is_scan_stop(const char *line, size_t length)
{
    size_t begin = 0u;
    while (begin < length && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
    while (length > begin && (line[length - 1u] == ' ' ||
           line[length - 1u] == '\t')) --length;
    static const char expected[] = "SCAN_STOP";
    if (length - begin != sizeof(expected) - 1u) return false;
    for (size_t index = 0u; index < sizeof(expected) - 1u; ++index) {
        char c = line[begin + index];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != expected[index]) return false;
    }
    return true;
}

static bool line_has_nonwhitespace(const char *line, size_t length)
{
    for (size_t index = 0u; index < length; ++index) {
        if (line[index] != ' ' && line[index] != '\t') return true;
    }
    return false;
}

static void process_console_line(char *line, size_t length)
{
    line[length] = '\0';
    if (nocti_v4_scheduler_is_active(&s_scheduler) &&
        !line_is_scan_stop(line, length)) {
        if (line_has_nonwhitespace(line, length)) {
            ++s_health.console_commands_rejected_during_scan;
        }
        return;
    }
    execute_command(line);
    print_prompt();
}

static void console_run(void)
{
    char line[160];
    size_t length = 0u;
    bool ignore_lf_after_cr = false;

    print_prompt();
    for (;;) {
        uint8_t character = 0u;
        const int received = usb_serial_jtag_read_bytes(&character, 1u, portMAX_DELAY);
        if (received != 1) {
            ESP_LOGE("nocti_v4_console", "USB Serial/JTAG input stopped result=%d", received);
            return;
        }

        const bool scan_active = nocti_v4_scheduler_is_active(&s_scheduler);
        if (character == '\r') {
            if (!scan_active) {
                putchar('\n');
                fflush(stdout);
            }
            process_console_line(line, length);
            length = 0u;
            ignore_lf_after_cr = true;
            continue;
        }
        if (character == '\n') {
            if (ignore_lf_after_cr) {
                ignore_lf_after_cr = false;
                continue;
            }
            if (!scan_active) {
                putchar('\n');
                fflush(stdout);
            }
            process_console_line(line, length);
            length = 0u;
            continue;
        }

        ignore_lf_after_cr = false;
        if (character == '\b' || character == 0x7fu) {
            if (length > 0u) {
                --length;
                if (!scan_active) {
                    fputs("\b \b", stdout);
                    fflush(stdout);
                }
            }
            continue;
        }
        if (character >= 0x20u && character <= 0x7eu) {
            if (length < (sizeof(line) - 1u)) {
                line[length++] = (char)character;
                if (!scan_active) {
                    putchar((int)character);
                    fflush(stdout);
                }
            } else {
                if (!scan_active) {
                    putchar('\a');
                    fflush(stdout);
                }
            }
        }
    }
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
               " pca_i2c_irq_core=%d led_core=1 led_priority=22"
               " scheduler_core=1 scheduler_priority=21"
               " ads_core=1 ads_priority=20"
               " scheduled_led_completion=STATIC_SPSC_NONBLOCKING"
               " i2c_isr_cache_safe=1"
               " wifi_driver_core=0 optical_core=0 frame_core=0 network_core=0\n",
               s_app_main_core, NOCTI_V4_APP_MAIN_CORE);
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
        nocti_v4_network_init(&s_network, &s_timebase, &s_health) :
        ESP_ERR_INVALID_STATE;
    if (network_init == ESP_OK) {
        network_init = nocti_v4_optical_bind_completed_frame_sink(
            &s_optical, nocti_v4_network_frame_sink, &s_network);
    }
    report_result("BOOT_V4_7_NETWORK_INIT", network_init);
    print_status();
    print_help();
    err = console_input_init();
    if (err != ESP_OK) {
        printf("CONSOLE_FAIL stage=USB_SERIAL_JTAG_DRIVER result=%s; command console is unavailable\n",
               nocti_v4_err_name(err));
        return;
    }
    console_run();




}
