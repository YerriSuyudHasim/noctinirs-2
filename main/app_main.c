/* NoctiNIRS 2 composition root (draft v0.1.0). Non-clinical research prototype:
 * raw counts only, no physiological or clinical interpretation. */
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nn2_brownout_check.h"
#include "nn2_core_types.h"
#include "nn2_core_ring.h"
#include "nn2_app_sequencer.h"
#include "nn2_app_safe_boot.h"
#include "nn2_adapter_esp32s3.h"
#include "nn2_adapter_emitter_safe.h"
#include "nn2_boot_esp.h"

static const char *TAG = "nn2_main";

#if !CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC
static nn2_core_ring_storage_t s_ring_storage;
static nn2_core_ring_t *s_ring;
static nn2_app_seq_storage_t s_seq_storage;
static nn2_app_sequencer_t *s_seq;
#endif

#if !CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC
static void fake_demo(void)
{
    ESP_LOGI(TAG, "NoctiNIRS 2 firmware v%s (draft library skeleton, non-clinical)", NN2_VERSION_STRING);

    if (nn2_core_ring_init(&s_ring_storage, &s_ring) != NN2_OK) return;
    nn2_app_seq_config_t cfg = {
        .clock = nn2_adapter_clock_esp_port(),
        .log = nn2_adapter_log_esp_port(),
        .ring = s_ring,
        .channel = 0,
        .settle_us = CONFIG_NN2_LED_SETTLE_US,
    };
    if (nn2_adapter_led_disabled_init(&cfg.led) != NN2_OK ||
        nn2_adapter_adc_fake_init(&cfg.adc) != NN2_OK) {
        ESP_LOGE(TAG, "adapter init failed; halting demo");
        return;
    }
    nn2_status_t st = nn2_app_sequencer_init(&s_seq_storage, &s_seq, &cfg);
    if (st != NN2_OK) {
        ESP_LOGE(TAG, "sequencer init: %s", nn2_core_status_str(st));
        return;
    }

    for (int i = 0; i < CONFIG_NN2_DEMO_CYCLES; i++) {
        st = nn2_app_sequencer_run_cycle(s_seq);
        if (st != NN2_OK) {
            /* Fault: the sequencer already issued the fail-safe off(). Stop the
             * demo; no further on() is issued. Cleanup runs in this task. */
            ESP_LOGE(TAG, "cycle %d failed: %s (state %s); stopping, no further cycles", i,
                     nn2_core_status_str(st), nn2_app_seq_state_str(nn2_app_sequencer_state(s_seq)));
        }
        nn2_core_sample_t s;
        while (nn2_core_ring_pop(s_ring, &s) == NN2_OK) {
            ESP_LOGI(TAG, "sample seq=%" PRIu32 " ch=%u src=%s raw=%" PRIu32 " t_on=%" PRIu64 "us dt=%" PRIu64 "us",
                     s.seq, (unsigned)s.channel, nn2_core_sample_src_str(s.src), s.raw, s.t_led_on_us,
                     s.t_sample_us - s.t_led_on_us);
        }
        if (st != NN2_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); /* blocking (not busy) bounded inter-cycle delay */
    }
    nn2_app_seq_observation_t observation;
    if (nn2_app_sequencer_observe(s_seq, &observation) != NN2_OK) return;
    ESP_LOGI(TAG, "done: cycles_ok=%" PRIu32 " faults=%" PRIu32 " off_failures=%" PRIu32 " dropped=%" PRIu32,
             observation.cycles_ok, observation.faults, observation.off_failures, nn2_core_ring_dropped(s_ring));
}

#endif
#if CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC
#include "nn2_app_adc_diagnostic.h"
#include "nn2_board_v1.h"
#include "esp_app_desc.h"
static void adc_diagnostic(void)
{
    nn2_port_adc_stream_t port;
    nn2_core_adc_observation_t observation = {0};
    const esp_app_desc_t *description = esp_app_get_description();
    ESP_LOGI(TAG, "profile=ADS131_DIAGNOSTIC source=ADC time_kind=read_complete_us emitter_control=ABSENT fixture=REQUIRED source_sha=%s config=MODE0110_CLOCK0F0A_GAIN0000 version=%s elf_sha256=%02x%02x%02x%02x", NN2_SOURCE_SHA, description->version,
        description->app_elf_sha256[0], description->app_elf_sha256[1], description->app_elf_sha256[2], description->app_elf_sha256[3]);
    nn2_status_t s = nn2_adapter_ads131_create(&nn2_board_v1_adc, &port);
    if (s != NN2_OK) { ESP_LOGE(TAG, "owner creation failed: %s", nn2_core_status_str(s)); return; }
    for (unsigned session = 0; session < 2u; ++session) {
        nn2_core_adc_record_t record;
        s = nn2_app_adc_diagnostic_run(&port, 128u, &observation, &record);
        (void)port.observe(port.ctx, &observation, 1000u);
        ESP_LOGI(TAG, "diagnostic result=%s state=%u fault=%u session=%" PRIu32 " produced=%" PRIu32 " consumed=%" PRIu32 " lost=%" PRIu32 " overflow=%" PRIu32 " crc_bad=%" PRIu32 " spi_errors=%" PRIu32 " drdy_timeouts=%" PRIu32 " missed_min=%" PRIu32 " unknown_loss=%" PRIu32 " occupancy=%" PRIu32 " peak=%" PRIu32 " record_bytes=%u",
            nn2_core_status_str(s), (unsigned)observation.state, (unsigned)observation.fault,
            observation.session, observation.produced, observation.consumed, observation.lost,
            observation.overflow, observation.crc_bad, observation.spi_errors, observation.drdy_timeouts,
            observation.adc_missed, observation.loss_unknown, observation.occupancy,
            observation.max_occupancy, (unsigned)sizeof(nn2_core_adc_record_t));
        if (s == NN2_OK)
            ESP_LOGI(TAG, "source=ADC sequence=%" PRIu32 " read_complete_us=%" PRIu64 " status=%04x quality=%" PRIu32 " ch0=%" PRId32 " ch1=%" PRId32 " ch2=%" PRId32 " ch3=%" PRId32,
                record.sequence, record.read_complete_us, (unsigned)record.status, record.quality,
                record.channel[0], record.channel[1], record.channel[2], record.channel[3]);
        if (s != NN2_OK) break; /* No automatic fault recovery. */
    }
}
#endif
static void safe_boot_error(void *ctx, nn2_boot_outcome_t outcome, nn2_status_t error)
{
    (void)ctx;
    ESP_LOGE(TAG, "safe-boot fail-closed: outcome=%s error=%s; emitter path not enabled",
             nn2_app_safe_boot_outcome_str(outcome), nn2_core_status_str(error));
}

/* Reset reason is read and logged before adapter bind, SPIRAM query, or any
 * later profile work. Safe-off then runs before the fake demo or the ADS131
 * diagnostic. A failure returns without starting either path. */
static bool emitter_safe_boot(void)
{
    nn2_reset_reason_t reason = nn2_adapter_read_reset_reason();
    nn2_port_emitter_safe_t port;
    nn2_app_safe_boot_input_t in;
    nn2_app_safe_boot_result_t result;
    nn2_status_t st;

    ESP_LOGI(TAG, "reset_reason=%s (%d)", nn2_app_reset_reason_str(reason), (int)reason);
    ESP_LOGI(TAG, "brownout_det=%d recorded_level=%d", CONFIG_ESP_BROWNOUT_DET,
             CONFIG_ESP_BROWNOUT_DET_LVL);
    st = nn2_adapter_emitter_safe_bind(&port);
    if (st != NN2_OK) {
        ESP_LOGE(TAG, "safe-boot fail-closed: emitter adapter bind failed; emitter path not enabled");
        return false;
    }
    in.reset_reason = reason;
    /* Runtime SPIRAM check owned by issue #49. Compile-time `# CONFIG_SPIRAM is
     * not set` and the app_main `#error` are assigned to PR #34 by that issue
     * and are not duplicated here. PR #34 on this base does not contain them. */
    in.spiram_bytes = nn2_adapter_spiram_bytes();
    st = nn2_app_safe_boot_run(&in, &port, safe_boot_error, NULL, &result);
    if (st != NN2_OK) {
        ESP_LOGE(TAG, "safe-boot fail-closed: reset_held_low=%d attempts=%" PRIu32
                 " emitter_enabled=0 reset=%s",
                 result.reset_held_low ? 1 : 0, result.attempts,
                 nn2_app_reset_reason_str(result.reset_reason));
        return false;
    }
    ESP_LOGI(TAG, "safe-boot complete: RESET# held low; emitter path disabled; reset=%s",
             nn2_app_reset_reason_str(result.reset_reason));
    return true;
}

void app_main(void)
{
    if (!emitter_safe_boot()) {
        return;
    }
#if CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC
    adc_diagnostic();
#else
    fake_demo();
#endif
}
