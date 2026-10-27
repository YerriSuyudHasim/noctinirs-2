#include "nn2_board.h"
#include "nn2_board_wiring.h"
#include <stdatomic.h>
#include "driver/gpio.h"
#include "esp_system.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "nn2_board requires ESP32-S3"
#endif
#if CONFIG_SPIRAM
#error "Current board contract requires PSRAM disabled"
#endif

/* Source: SCH_Thesis_main_2026-10-16.json, Main_board.
 * U1 IO18 endpoint (1255,-1440) -> wire gge55585 -> RESET_LED label.
 * PCA9956B RESET# pin 35 (290,-555) -> wire gge55405 -> RESET_LED label.
 * Datasheet RESET# is active low. Only the opt-in LED owner releases reset.
 * Before app_main, bootloader/power-on behavior still requires board evidence. */
#define NN2_BOARD_RESET_LED NN2_BOARD_RESET_LED_GPIO
static bool s_initialized;
static atomic_bool s_asserted;

esp_err_t nn2_board_init_safe(void)
{
    if (s_initialized) {
        return nn2_board_hold_emitters_off();
    }
    esp_err_t result = gpio_set_level(NN2_BOARD_RESET_LED, 0);
    if (result != ESP_OK) {
        return result;
    }
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << NN2_BOARD_RESET_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&config);
    if (result == ESP_OK) {
        result = gpio_set_level(NN2_BOARD_RESET_LED, 0);
    }
    s_initialized = result == ESP_OK;
    s_asserted = s_initialized;
    return result;
}

esp_err_t nn2_board_hold_emitters_off(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = gpio_set_level(NN2_BOARD_RESET_LED, 0);
    s_asserted = result == ESP_OK;
    return result;
}

/* Only the opt-in LED owner may call this, under its cancellation interlock. */
esp_err_t nn2_board_release_emitters_reset(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t result = gpio_set_level(NN2_BOARD_RESET_LED, 1);
    if (result == ESP_OK) s_asserted = false;
    return result;
}

static nn2_status_t hold_off(void *ctx)
{
    (void)ctx;
    return nn2_board_hold_emitters_off() == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

nn2_port_safety_t nn2_board_safety_port(void)
{
    return (nn2_port_safety_t) { .hold_off = hold_off };
}

nn2_board_boot_info_t nn2_board_get_boot_info(void)
{
    return (nn2_board_boot_info_t) {
        .reset_reason = (unsigned)esp_reset_reason(),
        .emitter_reset_gpio = NN2_BOARD_RESET_LED,
        .reset_asserted_by_software = s_asserted,
    };
}
