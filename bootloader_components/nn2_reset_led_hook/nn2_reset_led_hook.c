/* Issue #49 bootloader hook. ESP-IDF v6.1 weak bootloader_before_init.
 * Drive RESET_LED (IO18) push-pull low and hold it through app hand-off.
 * No I2C, no flash, no eFuse, no secure-boot or flash-encryption change.
 * The hold is the whole bootloader-to-app interval, which is the issue's
 * ">= 2.5 us and held through the app hand-off". Class H must measure the
 * ROM-to-hook gap. This file does not claim that measurement.
 */
#include "nn2_reset_led_pin.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"

void bootloader_hooks_include(void)
{
}

static void nn2_reset_led_drive_low(void)
{
    /* Latch low before output enable so the enable edge is not a high glitch.
     * Drive strength is left at the pad reset value: issue #49 does not state one.
     * Internal pulls are left unchanged. */
    esp_rom_gpio_pad_unhold(NN2_RESET_LED_GPIO);
    gpio_ll_set_level(&GPIO, NN2_RESET_LED_GPIO, 0);
    gpio_ll_func_sel(&GPIO, (uint8_t)NN2_RESET_LED_GPIO, PIN_FUNC_GPIO);
    gpio_ll_od_disable(&GPIO, NN2_RESET_LED_GPIO);
    gpio_ll_set_level(&GPIO, NN2_RESET_LED_GPIO, 0);
    gpio_ll_output_enable(&GPIO, NN2_RESET_LED_GPIO);
    gpio_ll_set_level(&GPIO, NN2_RESET_LED_GPIO, 0);
}

void bootloader_before_init(void)
{
    nn2_reset_led_drive_low();
}

void bootloader_after_init(void)
{
    /* Do not release RESET#. The app safe-off policy owns the later release. */
}
