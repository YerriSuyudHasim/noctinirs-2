#include "nn2_wifi_bringup.h"

#include "esp_log.h"

static const char *TAG = "noctinirs2";

void app_main(void)
{
    nn2_status_t status;

    ESP_LOGI(TAG, "NoctiNIRS 2 firmware v0.1.0");
    ESP_LOGI(TAG, "Wi-Fi STA provisioning is enabled");
    ESP_LOGW(TAG, "Optical acquisition, emitters, and clinical behavior are not implemented");
    status = nn2_wifi_bringup();
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "wifi bring-up failed: %d", (int)status);
    }
}
