#include "esp_log.h"

#include "nn2_adapter_ads131.h"
#include "nn2_app_safe.h"
#include "nn2_board_v1.h"

static const char *TAG = "noctinirs2";

void app_main(void)
{
    nn2_port_adc_t port;
    nn2_status_t status;

    ESP_LOGI(TAG, "NoctiNIRS 2 firmware v0.1.0");
    if (!nn2_app_adc_hold_required(NN2_APP_BOOT)) {
        ESP_LOGE(TAG, "BOOT does not require the ADS131 hold");
        return;
    }
    status = nn2_adapter_ads131_bind(&port, &nn2_board_ads131_pins);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "ADS131 bind failed: %d", (int)status);
        return;
    }
    /* Hold SYNC/RESET low before any task is created. */
    status = port.safe_state(port.ctx);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "ADS131 safe_state failed: %d", (int)status);
        return;
    }
    status = port.init(port.ctx, &nn2_board_ads131_cfg);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "ADS131 init failed: %d", (int)status);
        return;
    }
    status = nn2_adapter_ads131_create_task(port.ctx);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "ADS131 task create failed: %d", (int)status);
        return;
    }
    ESP_LOGW(TAG,
             "ADS131 held in reset; start is not called; Class H is not run");
}
