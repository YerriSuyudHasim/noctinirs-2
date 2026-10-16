#include "nn2_adapter_esp32s3.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "nn2_adc";

#if CONFIG_NN2_ADC_ENABLE
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc;

static nn2_status_t adc_read(void *ctx, uint8_t channel, uint32_t *raw_out)
{
    (void)ctx;
    (void)channel; /* single configured channel in this draft */
    if (raw_out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, (adc_channel_t)CONFIG_NN2_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_read: %s", esp_err_to_name(err));
        return NN2_ERR_IO;
    }
    *raw_out = (uint32_t)(raw < 0 ? 0 : raw);
    return NN2_OK;
}

nn2_status_t nn2_adapter_adc_esp_init(nn2_port_adc_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return NN2_ERR_IO;
    }
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    err = adc_oneshot_config_channel(s_adc, (adc_channel_t)CONFIG_NN2_ADC_CHANNEL, &ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        return NN2_ERR_IO;
    }
    ESP_LOGI(TAG, "ADC1 oneshot enabled, channel %d", CONFIG_NN2_ADC_CHANNEL);
    out->read = adc_read;
    out->ctx = NULL;
    return NN2_OK;
}

#else /* !CONFIG_NN2_ADC_ENABLE */

static nn2_status_t adc_read_stub(void *ctx, uint8_t channel, uint32_t *raw_out)
{
    (void)ctx;
    (void)channel;
    if (raw_out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    *raw_out = 0u;
    return NN2_OK;
}

nn2_status_t nn2_adapter_adc_esp_init(nn2_port_adc_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "ADC disabled (CONFIG_NN2_ADC_ENABLE=n): stub returns raw=0, no pin touched");
    out->read = adc_read_stub;
    out->ctx = NULL;
    return NN2_OK;
}
#endif
