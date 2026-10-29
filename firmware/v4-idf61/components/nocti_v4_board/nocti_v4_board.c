#include "nocti_v4_board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_emitter_gate_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_emitter_release_allowed=true;

esp_err_t nocti_v4_board_early_safe_init(nocti_v4_board_t *board)
{
    if (board == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(board, 0, sizeof(*board));
    /* Drive the active-low resets before gpio_config, then again after.
     * That order is nn2_board_init_safe at feat35 31fb2a9
     * (components/nn2_board/nn2_board.c). V4 still owns ADS reset and CS. */
    esp_err_t err = gpio_set_level(NOCTI_V4_PCA_RESET_GPIO, 0);
    if (err == ESP_OK) {
        err = gpio_set_level(NOCTI_V4_ADS131_SYNC_RESET_GPIO, 0);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(NOCTI_V4_ADS131_CS_GPIO, 1);
    }
    if (err != ESP_OK) {
        return err;
    }
    const gpio_config_t resets = {
        .pin_bit_mask = (1ULL << NOCTI_V4_PCA_RESET_GPIO) |
                        (1ULL << NOCTI_V4_ADS131_SYNC_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&resets);
    if (err == ESP_OK) {
        const gpio_config_t chip_select = {
            .pin_bit_mask = 1ULL << NOCTI_V4_ADS131_CS_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&chip_select);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(NOCTI_V4_PCA_RESET_GPIO, 0);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(NOCTI_V4_ADS131_SYNC_RESET_GPIO, 0);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(NOCTI_V4_ADS131_CS_GPIO, 1);
    }
    if (err == ESP_OK) {
        const gpio_config_t drdy_input = {
            .pin_bit_mask = 1ULL << NOCTI_V4_ADS131_DRDY_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&drdy_input);
    }
    if (err == ESP_OK) {
        board->gpio_ready = true;
        board->pca_reset_asserted = true;
        board->ads131_reset_asserted = true;
        /* OE# stays undriven. This image has no OE GPIO; the state remains
         * unverified, including after the D6 IREFALL/PWMALL/LEDOUT writes. */
        board->oe_state = NOCTI_V4_OE_PHYSICAL_STATE_UNVERIFIED;
    }
    return err;
}

esp_err_t nocti_v4_board_assert_ads131_reset(nocti_v4_board_t *board)
{
    if (board == NULL || !board->gpio_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = gpio_set_level(NOCTI_V4_ADS131_SYNC_RESET_GPIO, 0);
    if (err == ESP_OK) {
        board->ads131_reset_asserted = true;
    }
    return err;
}

esp_err_t nocti_v4_board_release_ads131_reset(nocti_v4_board_t *board)
{
    if (board == NULL || !board->gpio_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = gpio_set_level(NOCTI_V4_ADS131_SYNC_RESET_GPIO, 1);
    if (err == ESP_OK) {
        board->ads131_reset_asserted = false;
    }
    return err;
}

esp_err_t nocti_v4_board_assert_pca_reset(nocti_v4_board_t *board)
{
    if (board == NULL || !board->gpio_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = gpio_set_level(NOCTI_V4_PCA_RESET_GPIO, 0);
    if (err == ESP_OK) {
        board->pca_reset_asserted = true;
    }
    return err;
}

esp_err_t nocti_v4_board_release_pca_reset(nocti_v4_board_t *board)
{
    if (board == NULL || !board->gpio_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_emitter_gate_lock);
    esp_err_t err=s_emitter_release_allowed ?
        gpio_set_level(NOCTI_V4_PCA_RESET_GPIO, 1) : ESP_ERR_INVALID_STATE;
    if (err==ESP_OK) board->pca_reset_asserted=false;
    portEXIT_CRITICAL(&s_emitter_gate_lock);
    return err;
}

const char *nocti_v4_board_oe_state_name(nocti_v4_oe_state_t state)
{
    (void)state;
    return "PHYSICAL_STATE_UNVERIFIED";
}

esp_err_t nocti_v4_board_set_emitter_gate(nocti_v4_board_t *board, bool allowed)
{
    if (board==NULL || !board->gpio_ready) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_emitter_gate_lock);
    s_emitter_release_allowed=allowed;
    esp_err_t err=ESP_OK;
    if (!allowed) {
        err=gpio_set_level(NOCTI_V4_PCA_RESET_GPIO,0);
        if (err==ESP_OK) board->pca_reset_asserted=true;
    }
    portEXIT_CRITICAL(&s_emitter_gate_lock);
    return err;
}
