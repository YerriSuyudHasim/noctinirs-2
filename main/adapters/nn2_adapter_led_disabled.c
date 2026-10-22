/* DISABLED LED/emitter adapter (0.1.x scope: fake/disabled adapters only,
 * Architect F1 on PR #15). No-op that only logs: it never configures,
 * reads, or drives any GPIO and includes no GPIO driver header. */
#include "nn2_adapter_esp32s3.h"
#include "esp_log.h"

static const char *TAG = "nn2_led_disabled";

static nn2_status_t led_on_disabled(void *ctx, uint8_t channel)
{
    (void)ctx;
    ESP_LOGD(TAG, "LED on (disabled no-op, ch %u)", (unsigned)channel);
    return NN2_OK;
}

static nn2_status_t led_off_disabled(void *ctx, uint8_t channel)
{
    (void)ctx;
    ESP_LOGD(TAG, "LED off (disabled no-op, ch %u)", (unsigned)channel);
    return NN2_OK;
}

nn2_status_t nn2_adapter_led_disabled_init(nn2_port_led_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "LED/emitter adapter DISABLED: no-op, no GPIO configured or driven");
    out->on = led_on_disabled;
    out->off = led_off_disabled;
    out->ctx = NULL;
    return NN2_OK;
}
