/* NoctiNIRS 2 composition root (draft v0.1.0). Non-clinical research prototype:
 * raw counts only, no physiological or clinical interpretation. */
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nn2_core_types.h"
#include "nn2_core_ring.h"
#include "nn2_app_sequencer.h"
#include "nn2_adapter_esp32s3.h"

static const char *TAG = "nn2_main";

static nn2_core_ring_t s_ring;
static nn2_app_sequencer_t s_seq;

void app_main(void)
{
    ESP_LOGI(TAG, "NoctiNIRS 2 firmware v%s (draft library skeleton, non-clinical)", NN2_VERSION_STRING);

    nn2_app_seq_config_t cfg = {
        .clock = nn2_adapter_clock_esp_port(),
        .log = nn2_adapter_log_esp_port(),
        .ring = &s_ring,
        .channel = 0,
        .settle_us = CONFIG_NN2_LED_SETTLE_US,
    };
    if (nn2_core_ring_init(&s_ring) != NN2_OK ||
        nn2_adapter_led_esp_init(&cfg.led) != NN2_OK ||
        nn2_adapter_adc_esp_init(&cfg.adc) != NN2_OK) {
        ESP_LOGE(TAG, "adapter init failed; halting demo");
        return;
    }
    nn2_status_t st = nn2_app_sequencer_init(&s_seq, &cfg);
    if (st != NN2_OK) {
        ESP_LOGE(TAG, "sequencer init: %s", nn2_core_status_str(st));
        return;
    }

    for (int i = 0; i < CONFIG_NN2_DEMO_CYCLES; i++) {
        st = nn2_app_sequencer_run_cycle(&s_seq);
        if (st != NN2_OK) {
            ESP_LOGE(TAG, "cycle %d failed: %s (state %s)", i, nn2_core_status_str(st),
                     nn2_app_seq_state_str(nn2_app_sequencer_state(&s_seq)));
            (void)nn2_app_sequencer_reset(&s_seq);
        }
        nn2_core_sample_t s;
        while (nn2_core_ring_pop(&s_ring, &s) == NN2_OK) {
            ESP_LOGI(TAG, "sample seq=%" PRIu32 " ch=%u raw=%" PRIu32 " t_on=%" PRIu64 "us dt=%" PRIu64 "us",
                     s.seq, (unsigned)s.channel, s.raw, s.t_led_on_us, s.t_sample_us - s.t_led_on_us);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "done: cycles_ok=%" PRIu32 " faults=%" PRIu32 " dropped=%" PRIu32,
             s_seq.cycles_ok, s_seq.faults, s_ring.dropped);
}
