#include "nn2_adapter_esp32s3.h"
#include "esp_log.h"

static const char *TAG = "nn2";

static void log_write(void *ctx, nn2_log_level_t level, const char *msg)
{
    (void)ctx;
    switch (level) {
    case NN2_LOG_ERROR: ESP_LOGE(TAG, "%s", msg); break;
    case NN2_LOG_WARN:  ESP_LOGW(TAG, "%s", msg); break;
    case NN2_LOG_INFO:  ESP_LOGI(TAG, "%s", msg); break;
    default:            ESP_LOGD(TAG, "%s", msg); break;
    }
}

nn2_port_log_t nn2_adapter_log_esp_port(void)
{
    nn2_port_log_t p = { .write = log_write, .ctx = NULL };
    return p;
}
