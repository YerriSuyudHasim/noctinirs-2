#include "nn2_adapter_esp32s3.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "nn2_led";

#if CONFIG_NN2_LED_ENABLE
#include "driver/gpio.h"

#define LED_PIN ((gpio_num_t)CONFIG_NN2_LED_GPIO)

static nn2_status_t led_on(void *ctx, uint8_t channel)
{
    (void)ctx;
    (void)channel;
    return (gpio_set_level(LED_PIN, 1) == ESP_OK) ? NN2_OK : NN2_ERR_IO;
}

static nn2_status_t led_off(void *ctx, uint8_t channel)
{
    (void)ctx;
    (void)channel;
    return (gpio_set_level(LED_PIN, 0) == ESP_OK) ? NN2_OK : NN2_ERR_IO;
}

nn2_status_t nn2_adapter_led_esp_init(nn2_port_led_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_NN2_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* Start LOW before enabling the output, then use the weakest drive strength. */
    if (gpio_set_level(LED_PIN, 0) != ESP_OK || gpio_config(&io) != ESP_OK ||
        gpio_set_drive_capability(LED_PIN, GPIO_DRIVE_CAP_0) != ESP_OK ||
        gpio_set_level(LED_PIN, 0) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d init failed", CONFIG_NN2_LED_GPIO);
        return NN2_ERR_IO;
    }
    ESP_LOGW(TAG, "LED output ENABLED on GPIO%d (drive cap 0, initial LOW)", CONFIG_NN2_LED_GPIO);
    out->on = led_on;
    out->off = led_off;
    out->ctx = NULL;
    return NN2_OK;
}

#else /* !CONFIG_NN2_LED_ENABLE: no-op, never touches GPIO */

static nn2_status_t led_on_noop(void *ctx, uint8_t channel)
{
    (void)ctx;
    ESP_LOGD(TAG, "LED on (no-op, ch %u)", (unsigned)channel);
    return NN2_OK;
}

static nn2_status_t led_off_noop(void *ctx, uint8_t channel)
{
    (void)ctx;
    ESP_LOGD(TAG, "LED off (no-op, ch %u)", (unsigned)channel);
    return NN2_OK;
}

nn2_status_t nn2_adapter_led_esp_init(nn2_port_led_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "LED output disabled (CONFIG_NN2_LED_ENABLE=n): no-op adapter, no GPIO driven");
    out->on = led_on_noop;
    out->off = led_off_noop;
    out->ctx = NULL;
    return NN2_OK;
}
#endif
