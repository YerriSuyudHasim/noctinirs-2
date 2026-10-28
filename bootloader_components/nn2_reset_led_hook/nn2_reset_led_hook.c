/* Adapted from PR #60 d3478d6d, with current shared pin definition.
 * Before-init hook holds reset through app handoff; no I2C here.
 * ROM-to-hook behavior is not covered or physically verified. */
#include "nn2_board_wiring.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
void bootloader_hooks_include(void) {}
void bootloader_before_init(void)
{
    esp_rom_gpio_pad_unhold(NN2_BOARD_RESET_LED_GPIO);
    gpio_ll_set_level(&GPIO, NN2_BOARD_RESET_LED_GPIO, 0);
    gpio_ll_func_sel(&GPIO, NN2_BOARD_RESET_LED_GPIO, PIN_FUNC_GPIO);
    gpio_ll_od_disable(&GPIO, NN2_BOARD_RESET_LED_GPIO);
    gpio_ll_output_enable(&GPIO, NN2_BOARD_RESET_LED_GPIO);
    gpio_ll_set_level(&GPIO, NN2_BOARD_RESET_LED_GPIO, 0);
}
