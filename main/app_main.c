/* Composition root. ESP-IDF defines ESP_PLATFORM on the target command
   line. Include sdkconfig.h here so a missing Kconfig header cannot skip
   the PSRAM check. Host builds leave ESP_PLATFORM unset. */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(ESP_PLATFORM) && !defined(CONFIG_IDF_TARGET)
#error "sdkconfig not seen"
#endif
/* This image does not enable PSRAM. A pin map for octal-PSRAM pads is
   not approved in an issue or ADR, so this check is the software switch
   only. */
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
#error "CONFIG_SPIRAM must stay disabled"
#endif

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
