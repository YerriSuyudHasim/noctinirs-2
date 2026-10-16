#include "nocti_v4_board.h"

#include <string.h>

esp_err_t nocti_v4_board_early_safe_init(nocti_v4_board_t *board)
{
    if (board == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(board, 0, sizeof(*board));
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << NOCTI_V4_PCA_RESET_GPIO) |
                        (1ULL << NOCTI_V4_ADS131_SYNC_RESET_GPIO) |
                        (1ULL << NOCTI_V4_ADS131_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(NOCTI_V4_PCA_RESET_GPIO, 0);
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
    const esp_err_t err = gpio_set_level(NOCTI_V4_PCA_RESET_GPIO, 1);
    if (err == ESP_OK) {
        board->pca_reset_asserted = false;
    }
    return err;
}

const char *nocti_v4_board_oe_state_name(nocti_v4_oe_state_t state)
{
    (void)state;
    return "PHYSICAL_STATE_UNVERIFIED";
}
