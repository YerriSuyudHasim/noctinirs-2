/* FAKE ADC adapter (0.1.x scope: fake/disabled adapters only, Architect F1 on PR #15).
 * Touches no pin and no ADC peripheral. Every value it delivers is synthetic,
 * NOT a measurement, and is tagged NN2_SAMPLE_SRC_FAKE. */
#include "nn2_adapter_esp32s3.h"
#include "esp_log.h"

static const char *TAG = "nn2_adc_fake";

#define NN2_ADC_FAKE_RAW 0u /* deterministic synthetic value */

static nn2_status_t adc_fake_read(void *ctx, uint8_t channel, uint32_t *raw_out)
{
    (void)ctx;
    (void)channel;
    if (raw_out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    *raw_out = NN2_ADC_FAKE_RAW;
    return NN2_OK;
}

nn2_status_t nn2_adapter_adc_fake_init(nn2_port_adc_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "FAKE ADC adapter: synthetic raw=%u, src=FAKE, not a measurement; no pin or ADC touched",
             (unsigned)NN2_ADC_FAKE_RAW);
    out->read = adc_fake_read;
    out->ctx = NULL;
    out->source = NN2_SAMPLE_SRC_FAKE;
    return NN2_OK;
}
