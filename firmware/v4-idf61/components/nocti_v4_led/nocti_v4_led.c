#include "nocti_v4_led.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "nocti_v4_common.h"
#include "nn2_core_optical.h"

static const char *TAG = "nocti_v4_led";

#define PCA_MODE1       0x00u
#define PCA_MODE2       0x01u
#define PCA_LEDOUT0     0x02u
#define PCA_LEDOUT5     0x07u
#define PCA_GRPPWM      0x08u
#define PCA_GRPFREQ     0x09u
#define PCA_IREF0       0x22u
#define PCA_PWMALL      0x3Fu
/* PCA9956B IREFALL. ADR-0004 §3.8.1 D6 names this address 40h. */
#define PCA_IREFALL     0x40u
#define PCA_EFLAG0      0x41u
#define PCA_EFLAG5      0x46u

#define PCA_MODE1_EXPECTED_WRITABLE 0x40u
#define PCA_MODE1_VERIFY_MASK       0x7Fu
#define PCA_MODE2_EXPECTED_WRITABLE 0x00u
#define PCA_MODE2_VERIFY_MASK       0x28u
#define PCA_LEDOUT_OFF              0x00u
#define PCA_LEDOUT_ON               0x01u
#define PCA_INIT_TIMEOUT_MS         50u
#define PCA_COMMAND_TIMEOUT_MS      50u
#define PCA_DEFAULT_CURRENT_MA      NN2_OPTICAL_DEFAULT_MA
#define PCA_MAX_CURRENT_MA          NN2_OPTICAL_MAX_MA
#define PCA_FAST_QUAL_STRESS_READS  1000u

static const uint8_t k_source_to_pca[NOCTI_V4_OPTICAL_SOURCE_COUNT] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
    14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u,
};

/* Immutable registers used for the 800-kbit/s read-only qualification. EFLAG
 * is sampled separately because it is live status rather than configuration. */
static const uint8_t k_fast_qual_regs[] = {
    PCA_MODE1, PCA_MODE2,
    PCA_LEDOUT0 + 0u, PCA_LEDOUT0 + 1u, PCA_LEDOUT0 + 2u,
    PCA_LEDOUT0 + 3u, PCA_LEDOUT0 + 4u, PCA_LEDOUT0 + 5u,
    PCA_IREF0 + 0u, PCA_IREF0 + 1u, PCA_IREF0 + 2u, PCA_IREF0 + 3u,
    PCA_IREF0 + 4u, PCA_IREF0 + 5u, PCA_IREF0 + 6u, PCA_IREF0 + 7u,
    PCA_IREF0 + 8u, PCA_IREF0 + 9u,
    PCA_IREF0 + 14u, PCA_IREF0 + 15u, PCA_IREF0 + 16u,
    PCA_IREF0 + 17u, PCA_IREF0 + 18u, PCA_IREF0 + 19u,
    PCA_IREF0 + 20u, PCA_IREF0 + 21u, PCA_IREF0 + 22u, PCA_IREF0 + 23u,
};

_Static_assert(sizeof(k_fast_qual_regs) == 28u,
               "fast-I2C qualification register list must remain audited");

static nocti_v4_i2c_fast_qual_status_t s_fast_qualification;

static esp_err_t remember(nocti_v4_led_t *led, esp_err_t result)
{
    if (led != NULL) {
        led->last_result = result;
    }
    return result;
}

static uint8_t current_to_iref(uint16_t milliamps)
{
    if (milliamps == 0u) {
        return 0u;
    }
    const uint64_t numerator = (uint64_t)milliamps * 1000u *
                               (uint64_t)(4u * NOCTI_V4_PCA_REXT_OHM);
    uint32_t value = (uint32_t)((numerator + 450000u) / 900000u);
    if (value > UINT8_MAX) {
        value = UINT8_MAX;
    }
    return (uint8_t)value;
}

static bool all_ledout_off(const uint8_t ledout[6])
{
    for (uint8_t index = 0u; index < 6u; ++index) {
        if (ledout[index] != 0u) {
            return false;
        }
    }
    return true;
}

static esp_err_t read_reg(nocti_v4_led_t *led, uint8_t reg, uint8_t *value)
{
    return nocti_v4_i2c_read_reg(led->i2c, reg, value, PCA_COMMAND_TIMEOUT_MS,
                                 led->health);
}

static esp_err_t write_reg(nocti_v4_led_t *led, uint8_t reg, uint8_t value)
{
    return nocti_v4_i2c_write_reg(led->i2c, reg, value, PCA_COMMAND_TIMEOUT_MS,
                                  led->health);
}

static esp_err_t fail_safe_reset(nocti_v4_led_t *led, esp_err_t cause)
{
    if (led == NULL || led->board == NULL || led->health == NULL) {
        return cause;
    }
    const esp_err_t reset_err = nocti_v4_board_assert_pca_reset(led->board);
    led->initialized = false;
    led->ledout_cache_valid = false;
    led->selected_source = -1;
    if (reset_err == ESP_OK) {
        led->safe_off_state = NOCTI_V4_SAFE_OFF_RESET_ASSERTED;
        led->health->pca_resets++;
    } else {
        led->safe_off_state = NOCTI_V4_SAFE_OFF_UNKNOWN;
        led->health->safe_off_failures++;
        ESP_LOGE(TAG, "PCA RESET# fail-safe failed after %s: %s",
                 nocti_v4_err_name(cause), nocti_v4_err_name(reset_err));
    }
    return remember(led, cause);
}

static esp_err_t check_equal(nocti_v4_led_t *led, const char *name,
                             uint8_t expected, uint8_t actual)
{
    if (expected == actual) {
        return ESP_OK;
    }
    led->health->pca_readback_mismatch++;
    ESP_LOGE(TAG, "%s mismatch expected=0x%02X actual=0x%02X", name,
             (unsigned)expected, (unsigned)actual);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t read_error_flags(nocti_v4_led_t *led, uint8_t flags[6])
{
    for (uint8_t index = 0u; index < 6u; ++index) {
        const esp_err_t err = read_reg(led, (uint8_t)(PCA_EFLAG0 + index), &flags[index]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static void print_error_flags_for_channel(uint8_t channel, const uint8_t flags[6])
{
    const uint8_t packed = (uint8_t)((flags[channel / 4u] >> ((channel % 4u) * 2u)) & 0x03u);
    const char *meaning = packed == 0u ? "NO_ERROR" :
                          packed == 1u ? "SHORT" :
                          packed == 2u ? "OPEN" : "RESERVED";
    printf("PCA_EFLAG channel=%u code=%u meaning=%s\n", (unsigned)channel,
           (unsigned)packed, meaning);
}

esp_err_t nocti_v4_led_resolve_source(uint8_t source, uint8_t *pca_channel)
{
    if (pca_channel == NULL || source >= NOCTI_V4_OPTICAL_SOURCE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    *pca_channel = k_source_to_pca[source];
    return ESP_OK;
}

static esp_err_t verify_all_off(nocti_v4_led_t *led)
{
    uint8_t observed[6] = {0};
    for (uint8_t index = 0u; index < 6u; ++index) {
        esp_err_t err = read_reg(led, (uint8_t)(PCA_LEDOUT0 + index), &observed[index]);
        if (err != ESP_OK) {
            return err;
        }
        err = check_equal(led, "LEDOUT", 0u, observed[index]);
        if (err != ESP_OK) {
            return err;
        }
    }
    memcpy(led->ledout, observed, sizeof(led->ledout));
    led->ledout_cache_valid = true;
    led->selected_source = -1;
    led->safe_off_state = NOCTI_V4_SAFE_OFF_REGISTER_VERIFIED;
    return ESP_OK;
}

static esp_err_t program_iref_verified(nocti_v4_led_t *led)
{
    for (uint8_t channel = 0u; channel < NOCTI_V4_PCA_CHANNEL_COUNT; ++channel) {
        const esp_err_t write_result = write_reg(led, (uint8_t)(PCA_IREF0 + channel),
                                                 led->iref[channel]);
        if (write_result != ESP_OK) {
            return write_result;
        }
        uint8_t observed = 0u;
        const esp_err_t read_result = read_reg(led, (uint8_t)(PCA_IREF0 + channel), &observed);
        if (read_result != ESP_OK) {
            return read_result;
        }
        if (observed != led->iref[channel]) {
            led->health->iref_mismatch++;
            led->health->pca_readback_mismatch++;
            ESP_LOGE(TAG, "IREF%u mismatch expected=0x%02X actual=0x%02X",
                     (unsigned)channel, (unsigned)led->iref[channel], (unsigned)observed);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

/* D6 emitter-off writes (ADR-0004 §3.8.1, #33 comment 6333972667):
 * IREFALL=00h, then PWMALL=00h, then LEDOUT0-5=00h. Current stays zero
 * before any PWM or LEDOUT update. OE# is not a GPIO on this image;
 * nocti_v4_board_early_safe_init leaves oe_state unverified and this
 * function does not drive it. */
static esp_err_t write_d6_emitter_off(nocti_v4_led_t *led)
{
    esp_err_t err = nocti_v4_i2c_write_reg(led->i2c, PCA_IREFALL, 0x00u,
                                           PCA_INIT_TIMEOUT_MS, led->health);
    if (err != ESP_OK) {
        return err;
    }
    err = nocti_v4_i2c_write_reg(led->i2c, PCA_PWMALL, 0x00u,
                                 PCA_INIT_TIMEOUT_MS, led->health);
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t index = 0u; index < 6u; ++index) {
        err = nocti_v4_i2c_write_reg(led->i2c, (uint8_t)(PCA_LEDOUT0 + index), 0u,
                                     PCA_INIT_TIMEOUT_MS, led->health);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t verify_iref_zero(nocti_v4_led_t *led)
{
    for (uint8_t channel = 0u; channel < NOCTI_V4_PCA_CHANNEL_COUNT; ++channel) {
        uint8_t observed = 0u;
        const esp_err_t err = read_reg(led, (uint8_t)(PCA_IREF0 + channel), &observed);
        if (err != ESP_OK) {
            return err;
        }
        const esp_err_t check = check_equal(led, "IREF", 0u, observed);
        if (check != ESP_OK) {
            return check;
        }
    }
    return ESP_OK;
}

static esp_err_t program_sync_golden_path(nocti_v4_led_t *led)
{
    esp_err_t err = nocti_v4_board_assert_pca_reset(led->board);
    if (err != ESP_OK) {
        return err;
    }
    led->health->pca_resets++;
    led->safe_off_state = NOCTI_V4_SAFE_OFF_RESET_ASSERTED;
    esp_rom_delay_us(NOCTI_V4_PCA_RESET_LOW_US);
    err = nocti_v4_board_release_pca_reset(led->board);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    esp_rom_delay_us(NOCTI_V4_PCA_RESET_RECOVERY_US);

    uint8_t mode1 = 0u;
    err = nocti_v4_i2c_read_reg(led->i2c, PCA_MODE1, &mode1, PCA_INIT_TIMEOUT_MS,
                                led->health);
    if (err != ESP_OK) {
        led->health->pca_presence_fail++;
        return fail_safe_reset(led, err);
    }
    ESP_LOGI(TAG, "PCA presence addr=0x%02X MODE1=0x%02X", NOCTI_V4_PCA_ADDRESS_7BIT,
             (unsigned)mode1);

    err = write_d6_emitter_off(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    err = verify_iref_zero(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    err = verify_all_off(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }

    const struct { uint8_t reg; uint8_t value; } config[] = {
        {PCA_MODE1, PCA_MODE1_EXPECTED_WRITABLE},
        {PCA_MODE2, PCA_MODE2_EXPECTED_WRITABLE},
        {PCA_GRPPWM, 0x00u},
        {PCA_GRPFREQ, 0x00u},
    };
    for (size_t index = 0u; index < sizeof(config) / sizeof(config[0]); ++index) {
        err = nocti_v4_i2c_write_reg(led->i2c, config[index].reg, config[index].value,
                                     PCA_INIT_TIMEOUT_MS, led->health);
        if (err != ESP_OK) {
            return fail_safe_reset(led, err);
        }
    }
    /* Operating IREF (default 30 mA, cap 50 mA) is written only after LEDOUT
     * readback is zero, so the emitters stay off. */
    err = program_iref_verified(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }

    uint8_t read_mode1 = 0u;
    uint8_t read_mode2 = 0u;
    err = read_reg(led, PCA_MODE1, &read_mode1);
    if (err == ESP_OK) {
        err = read_reg(led, PCA_MODE2, &read_mode2);
    }
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    if ((read_mode1 & PCA_MODE1_VERIFY_MASK) != PCA_MODE1_EXPECTED_WRITABLE ||
        (read_mode2 & PCA_MODE2_VERIFY_MASK) != PCA_MODE2_EXPECTED_WRITABLE) {
        led->health->pca_readback_mismatch++;
        ESP_LOGE(TAG, "MODE readback mismatch MODE1=0x%02X expected-mask=0x%02X MODE2=0x%02X expected-mask=0x%02X reserved=0x%02X",
                 (unsigned)read_mode1, (unsigned)PCA_MODE1_EXPECTED_WRITABLE,
                 (unsigned)read_mode2, (unsigned)PCA_MODE2_EXPECTED_WRITABLE,
                 (unsigned)(read_mode2 & 0x07u));
        return fail_safe_reset(led, ESP_ERR_INVALID_RESPONSE);
    }
    uint8_t flags[6] = {0};
    err = read_error_flags(led, flags);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    ESP_LOGI(TAG, "PCA init verified MODE1=0x%02X MODE2=0x%02X EFLAGS=%02X %02X %02X %02X %02X %02X",
             (unsigned)read_mode1, (unsigned)read_mode2, (unsigned)flags[0],
             (unsigned)flags[1], (unsigned)flags[2], (unsigned)flags[3],
             (unsigned)flags[4], (unsigned)flags[5]);
    led->initialized = true;
    return remember(led, ESP_OK);
}

esp_err_t nocti_v4_led_init(nocti_v4_led_t *led, nocti_v4_board_t *board,
                             nocti_v4_i2c_t *i2c, nocti_v4_health_t *health)
{
    if (led == NULL || board == NULL || i2c == NULL || health == NULL ||
        !board->gpio_ready || !i2c->initialized || i2c->queue_depth != 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(led, 0, sizeof(*led));
    led->board = board;
    led->i2c = i2c;
    led->health = health;
    led->selected_source = -1;
    led->safe_off_state = board->pca_reset_asserted ?
        NOCTI_V4_SAFE_OFF_RESET_ASSERTED : NOCTI_V4_SAFE_OFF_UNKNOWN;
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
        led->iref[k_source_to_pca[source]] = current_to_iref(PCA_DEFAULT_CURRENT_MA);
    }
    return program_sync_golden_path(led);
}

esp_err_t nocti_v4_led_all_off(nocti_v4_led_t *led)
{
    if (led == NULL || !led->initialized) {
        return remember(led, ESP_ERR_INVALID_STATE);
    }
    led->health->led_all_off_commands++;
    led->safe_off_state = NOCTI_V4_SAFE_OFF_REQUESTED;
    /* Sync bus only (queue depth 0). One i2c_master_transmit or
     * transmit_receive per register, PCA_COMMAND_TIMEOUT_MS (50) each.
     * Success is 8 D6 writes + 24 IREF reads + 6 LEDOUT reads + 24 IREF
     * restore writes + 24 IREF restore reads = 86 calls. The first error
     * returns through fail_safe_reset (one GPIO write, no retry). The
     * in-scan async OFF burst is a separate 7-byte transmit. */
    esp_err_t err = write_d6_emitter_off(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    err = verify_iref_zero(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    err = verify_all_off(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    /* LEDOUT is the verified zero image. Put the previous IREF shadow
     * back so a later ON is not a zero-current enable. */
    err = program_iref_verified(led);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    return remember(led, ESP_OK);
}

esp_err_t nocti_v4_led_on(nocti_v4_led_t *led, uint8_t source)
{
    if (led == NULL || !led->initialized) {
        return remember(led, ESP_ERR_INVALID_STATE);
    }
    uint8_t channel = 0u;
    if (nocti_v4_led_resolve_source(source, &channel) != ESP_OK) {
        led->health->invalid_led_command++;
        return remember(led, ESP_ERR_INVALID_ARG);
    }
    const esp_err_t off_result = nocti_v4_led_all_off(led);
    if (off_result != ESP_OK) {
        return off_result;
    }
    const uint8_t ledout_index = (uint8_t)(channel / 4u);
    const uint8_t shift = (uint8_t)((channel % 4u) * 2u);
    uint8_t next = led->ledout[ledout_index];
    next = (uint8_t)((next & (uint8_t)~(0x03u << shift)) | (PCA_LEDOUT_ON << shift));
    const esp_err_t write_result = write_reg(led, (uint8_t)(PCA_LEDOUT0 + ledout_index), next);
    if (write_result != ESP_OK) {
        return fail_safe_reset(led, write_result);
    }
    uint8_t observed_ledout = 0u;
    uint8_t observed_iref = 0u;
    esp_err_t err = read_reg(led, (uint8_t)(PCA_LEDOUT0 + ledout_index), &observed_ledout);
    if (err == ESP_OK) {
        err = read_reg(led, (uint8_t)(PCA_IREF0 + channel), &observed_iref);
    }
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    if (observed_ledout != next || observed_iref != led->iref[channel]) {
        led->health->pca_readback_mismatch++;
        if (observed_iref != led->iref[channel]) {
            led->health->iref_mismatch++;
        }
        ESP_LOGE(TAG, "LED_ON readback mismatch source=%u channel=%u LEDOUT expected=0x%02X got=0x%02X IREF expected=0x%02X got=0x%02X",
                 (unsigned)source, (unsigned)channel, (unsigned)next,
                 (unsigned)observed_ledout, (unsigned)led->iref[channel],
                 (unsigned)observed_iref);
        return fail_safe_reset(led, ESP_ERR_INVALID_RESPONSE);
    }
    uint8_t flags[6] = {0};
    err = read_error_flags(led, flags);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    led->ledout[ledout_index] = observed_ledout;
    led->ledout_cache_valid = true;
    led->selected_source = source;
    led->safe_off_state = NOCTI_V4_SAFE_OFF_UNKNOWN;
    led->health->led_on_commands++;
    printf("LED_ON OK source=%u channel=%u LEDOUT%u=0x%02X IREF%u=0x%02X\n",
           (unsigned)source, (unsigned)channel, (unsigned)ledout_index,
           (unsigned)observed_ledout, (unsigned)channel, (unsigned)observed_iref);
    print_error_flags_for_channel(channel, flags);
    return remember(led, ESP_OK);
}

esp_err_t nocti_v4_led_off(nocti_v4_led_t *led)
{
    if (led == NULL || !led->initialized) {
        return remember(led, ESP_ERR_INVALID_STATE);
    }
    led->health->led_off_commands++;
    return nocti_v4_led_all_off(led);
}

esp_err_t nocti_v4_led_set_iref_ma(nocti_v4_led_t *led, uint8_t source, uint16_t milliamps)
{
    if (led == NULL || !led->initialized || milliamps > PCA_MAX_CURRENT_MA) {
        if (led != NULL && led->health != NULL) {
            led->health->invalid_led_command++;
        }
        return remember(led, ESP_ERR_INVALID_ARG);
    }
    uint8_t channel = 0u;
    if (nocti_v4_led_resolve_source(source, &channel) != ESP_OK) {
        led->health->invalid_led_command++;
        return remember(led, ESP_ERR_INVALID_ARG);
    }
    const uint8_t desired = current_to_iref(milliamps);
    esp_err_t err = write_reg(led, (uint8_t)(PCA_IREF0 + channel), desired);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    uint8_t observed = 0u;
    err = read_reg(led, (uint8_t)(PCA_IREF0 + channel), &observed);
    if (err != ESP_OK) {
        return fail_safe_reset(led, err);
    }
    if (observed != desired) {
        led->health->iref_mismatch++;
        led->health->pca_readback_mismatch++;
        return fail_safe_reset(led, ESP_ERR_INVALID_RESPONSE);
    }
    led->iref[channel] = observed;
    printf("LED_IREF OK source=%u channel=%u current_ma=%u IREF=0x%02X\n",
           (unsigned)source, (unsigned)channel, (unsigned)milliamps, (unsigned)observed);
    return remember(led, ESP_OK);
}

esp_err_t nocti_v4_led_reset_hold(nocti_v4_led_t *led)
{
    if (led == NULL || led->board == NULL || led->health == NULL) {
        return remember(led, ESP_ERR_INVALID_ARG);
    }
    const esp_err_t err = nocti_v4_board_assert_pca_reset(led->board);
    led->initialized = false;
    led->ledout_cache_valid = false;
    led->selected_source = -1;
    if (err == ESP_OK) {
        led->health->pca_resets++;
        led->safe_off_state = NOCTI_V4_SAFE_OFF_RESET_ASSERTED;
    } else {
        led->safe_off_state = NOCTI_V4_SAFE_OFF_UNKNOWN;
        led->health->safe_off_failures++;
    }
    return remember(led, err);
}

esp_err_t nocti_v4_led_reinit(nocti_v4_led_t *led)
{
    if (led == NULL || led->board == NULL || led->i2c == NULL || led->health == NULL) {
        return remember(led, ESP_ERR_INVALID_ARG);
    }
    const esp_err_t reset_err = nocti_v4_led_reset_hold(led);
    if (reset_err != ESP_OK) {
        return reset_err;
    }
    led->health->pca_reinits++;
    const esp_err_t err = nocti_v4_led_init(led, led->board, led->i2c, led->health);
    return remember(led, err);
}

esp_err_t nocti_v4_led_readback(nocti_v4_led_t *led, bool print_result)
{
    if (led == NULL || led->i2c == NULL || led->health == NULL || !led->i2c->initialized) {
        return remember(led, ESP_ERR_INVALID_STATE);
    }
    uint8_t mode1 = 0u;
    uint8_t mode2 = 0u;
    uint8_t ledout[6] = {0};
    uint8_t iref[NOCTI_V4_PCA_CHANNEL_COUNT] = {0};
    uint8_t flags[6] = {0};
    esp_err_t err = read_reg(led, PCA_MODE1, &mode1);
    if (err == ESP_OK) err = read_reg(led, PCA_MODE2, &mode2);
    for (uint8_t index = 0u; err == ESP_OK && index < 6u; ++index) {
        err = read_reg(led, (uint8_t)(PCA_LEDOUT0 + index), &ledout[index]);
    }
    for (uint8_t channel = 0u; err == ESP_OK && channel < NOCTI_V4_PCA_CHANNEL_COUNT; ++channel) {
        err = read_reg(led, (uint8_t)(PCA_IREF0 + channel), &iref[channel]);
    }
    if (err == ESP_OK) err = read_error_flags(led, flags);
    if (err != ESP_OK) {
        if (print_result) printf("LED_READBACK FAIL result=%s\n", nocti_v4_err_name(err));
        return remember(led, err);
    }
    if (print_result) {
        printf("LED_READBACK addr=0x%02X i2c_hz=%" PRIu32 " sda=%d scl=%d reset_gpio=%d reset_asserted=%u oe=%s qdepth=%" PRIu32 "\n",
               NOCTI_V4_PCA_ADDRESS_7BIT, led->i2c->clock_hz, NOCTI_V4_I2C_SDA_GPIO,
               NOCTI_V4_I2C_SCL_GPIO, NOCTI_V4_PCA_RESET_GPIO,
               led->board->pca_reset_asserted ? 1u : 0u,
               nocti_v4_board_oe_state_name(led->board->oe_state), led->i2c->queue_depth);
        printf("PCA_REG MODE1=0x%02X MODE2=0x%02X LEDOUT=%02X %02X %02X %02X %02X %02X EFLAG=%02X %02X %02X %02X %02X %02X\n",
               mode1, mode2, ledout[0], ledout[1], ledout[2], ledout[3], ledout[4], ledout[5],
               flags[0], flags[1], flags[2], flags[3], flags[4], flags[5]);
        for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
            const uint8_t channel = k_source_to_pca[source];
            printf("PCA_IREF source=%u channel=%u reg=0x%02X value=0x%02X\n",
                   (unsigned)source, (unsigned)channel, (unsigned)(PCA_IREF0 + channel),
                   (unsigned)iref[channel]);
        }
        printf("LED_READBACK selected_source=%" PRId32 " last_i2c=%s safe_off=%s physical_off=UNVERIFIED\n",
               led->selected_source, nocti_v4_err_name(led->last_result),
               nocti_v4_safe_off_state_name(led->safe_off_state));
    }
    return remember(led, ESP_OK);
}

static esp_err_t fast_qualification_read(nocti_v4_led_t *led, uint8_t reg,
                                         uint8_t *value, uint64_t *duration_sum_us)
{
    const int64_t started_us = esp_timer_get_time();
    ++s_fast_qualification.transactions_attempted;
    const esp_err_t err = read_reg(led, reg, value);
    const int64_t ended_us = esp_timer_get_time();
    const uint32_t duration_us = ended_us >= started_us ?
        (uint32_t)(ended_us - started_us) : 0u;
    if (s_fast_qualification.duration_min_us == 0u ||
        duration_us < s_fast_qualification.duration_min_us) {
        s_fast_qualification.duration_min_us = duration_us;
    }
    if (duration_us > s_fast_qualification.duration_max_us) {
        s_fast_qualification.duration_max_us = duration_us;
    }
    *duration_sum_us += duration_us;
    s_fast_qualification.duration_avg_us =
        (uint32_t)(*duration_sum_us / s_fast_qualification.transactions_attempted);
    if (err == ESP_OK) {
        ++s_fast_qualification.transactions_ok;
    } else {
        ++s_fast_qualification.communication_errors;
    }
    return err;
}

static void store_fast_qualification_registers(const uint8_t values[28])
{
    s_fast_qualification.mode1 = values[0];
    s_fast_qualification.mode2 = values[1];
    memcpy(s_fast_qualification.ledout, &values[2],
           sizeof(s_fast_qualification.ledout));
    memcpy(s_fast_qualification.mapped_iref, &values[8],
           sizeof(s_fast_qualification.mapped_iref));
}

static esp_err_t restore_sync_after_fast_qualification(nocti_v4_led_t *led)
{
    esp_err_t err = ESP_OK;
    if (led->i2c->initialized &&
        (led->i2c->clock_hz != NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ ||
         led->i2c->queue_depth != NOCTI_V4_I2C_TRANS_QUEUE_DEPTH)) {
        err = nocti_v4_i2c_deinit(led->i2c);
    }
    if (err == ESP_OK && !led->i2c->initialized) {
        err = nocti_v4_i2c_init_sync(led->i2c, led->health);
    }
    if (err == ESP_OK && led->i2c->clock_hz == NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ &&
        led->i2c->queue_depth == NOCTI_V4_I2C_TRANS_QUEUE_DEPTH) {
        err = nocti_v4_led_reinit(led);
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        (void)nocti_v4_led_reset_hold(led);
        return err;
    }
    s_fast_qualification.restored_sync_configured =
        led->initialized && led->i2c->initialized &&
        led->i2c->clock_hz == NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ &&
        led->i2c->queue_depth == NOCTI_V4_I2C_TRANS_QUEUE_DEPTH &&
        led->safe_off_state == NOCTI_V4_SAFE_OFF_REGISTER_VERIFIED;
    return s_fast_qualification.restored_sync_configured ? ESP_OK :
                                                        ESP_ERR_INVALID_STATE;
}

esp_err_t nocti_v4_led_run_fast_i2c_qualification(nocti_v4_led_t *led)
{
    memset(&s_fast_qualification, 0, sizeof(s_fast_qualification));
    s_fast_qualification.state = NOCTI_V4_I2C_FAST_QUAL_FAIL;
    s_fast_qualification.configured_hz = NOCTI_V4_I2C_SCAN_RUNTIME_HZ;
    s_fast_qualification.stress_reads_target = PCA_FAST_QUAL_STRESS_READS;
    s_fast_qualification.electrical_rise_fall_verified = false;

    if (led == NULL || led->i2c == NULL || led->health == NULL ||
        !led->initialized || !led->i2c->initialized ||
        led->i2c->clock_hz != NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ ||
        led->i2c->queue_depth != NOCTI_V4_I2C_TRANS_QUEUE_DEPTH) {
        s_fast_qualification.last_result = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t expected[sizeof(k_fast_qual_regs)] = {0};
    uint8_t observed[sizeof(k_fast_qual_regs)] = {0};
    uint64_t duration_sum_us = 0u;
    esp_err_t qualification_result = nocti_v4_led_all_off(led);
    for (size_t index = 0u;
         qualification_result == ESP_OK && index < sizeof(k_fast_qual_regs);
         ++index) {
        qualification_result = read_reg(led, k_fast_qual_regs[index],
                                         &expected[index]);
    }
    if (qualification_result == ESP_OK) {
        qualification_result = nocti_v4_i2c_deinit(led->i2c);
    }
    if (qualification_result == ESP_OK) {
        qualification_result = nocti_v4_i2c_init_at_speed(
            led->i2c, led->health, NOCTI_V4_I2C_TRANS_QUEUE_DEPTH,
            NOCTI_V4_I2C_SCAN_RUNTIME_HZ);
    }

    for (size_t index = 0u;
         qualification_result == ESP_OK && index < sizeof(k_fast_qual_regs);
         ++index) {
        qualification_result = fast_qualification_read(
            led, k_fast_qual_regs[index], &observed[index], &duration_sum_us);
        if (qualification_result == ESP_OK && observed[index] != expected[index]) {
            ++s_fast_qualification.readback_mismatch;
            qualification_result = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (qualification_result == ESP_OK) {
        store_fast_qualification_registers(observed);
    }
    for (uint8_t index = 0u; qualification_result == ESP_OK && index < 6u;
         ++index) {
        qualification_result = fast_qualification_read(
            led, (uint8_t)(PCA_EFLAG0 + index),
            &s_fast_qualification.eflag[index], &duration_sum_us);
    }
    for (uint32_t index = 0u;
         qualification_result == ESP_OK && index < PCA_FAST_QUAL_STRESS_READS;
         ++index) {
        const size_t register_index = index % sizeof(k_fast_qual_regs);
        uint8_t value = 0u;
        qualification_result = fast_qualification_read(
            led, k_fast_qual_regs[register_index], &value, &duration_sum_us);
        if (qualification_result == ESP_OK && value != expected[register_index]) {
            ++s_fast_qualification.readback_mismatch;
            qualification_result = ESP_ERR_INVALID_RESPONSE;
        }
        if (qualification_result == ESP_OK) {
            ++s_fast_qualification.stress_reads_completed;
        }
    }

    const esp_err_t restore_result = restore_sync_after_fast_qualification(led);
    if (restore_result != ESP_OK) {
        s_fast_qualification.last_result = restore_result;
        return restore_result;
    }
    if (qualification_result == ESP_OK &&
        s_fast_qualification.stress_reads_completed == PCA_FAST_QUAL_STRESS_READS &&
        s_fast_qualification.communication_errors == 0u &&
        s_fast_qualification.readback_mismatch == 0u) {
        s_fast_qualification.state = NOCTI_V4_I2C_FAST_QUAL_PASS;
    }
    s_fast_qualification.last_result = qualification_result;
    return qualification_result;
}

void nocti_v4_led_get_fast_i2c_qualification(
    nocti_v4_i2c_fast_qual_status_t *out_status)
{
    if (out_status != NULL) {
        *out_status = s_fast_qualification;
    }
}

static const char *fast_qualification_state_name(nocti_v4_i2c_fast_qual_state_t state)
{
    switch (state) {
        case NOCTI_V4_I2C_FAST_QUAL_PASS: return "PASS";
        case NOCTI_V4_I2C_FAST_QUAL_FAIL: return "FAIL";
        default: return "NOT_RUN";
    }
}

void nocti_v4_led_print_fast_i2c_qualification(void)
{
    const nocti_v4_i2c_fast_qual_status_t *q = &s_fast_qualification;
    printf("I2C_FMP_STATUS functional_800khz_test=%s configured_hz=%" PRIu32
           " transactions_attempted=%" PRIu32 " transactions_ok=%" PRIu32
           " errors=%" PRIu32 " readback_mismatch=%" PRIu32
           " stress_reads=%" PRIu32 "/%" PRIu32
           " duration_min_us=%" PRIu32 " duration_max_us=%" PRIu32
           " duration_avg_us=%" PRIu32 " restored_sync_configured=%u"
           " electrical_rise_fall_verified=%s esp32s3_datasheet_limit_hz=800000"
           " pca9956b_limit_hz=1000000 external_pullup_ohm=%u pullup_rail_mv=%u"
           " internal_pullups=0 last_result=%s\n",
           fast_qualification_state_name(q->state), q->configured_hz,
           q->transactions_attempted, q->transactions_ok,
           q->communication_errors, q->readback_mismatch,
           q->stress_reads_completed, q->stress_reads_target,
           q->duration_min_us, q->duration_max_us, q->duration_avg_us,
           q->restored_sync_configured ? 1u : 0u,
           q->electrical_rise_fall_verified ? "YES" : "NO",
           NOCTI_V4_I2C_EXTERNAL_PULLUP_OHM, NOCTI_V4_I2C_PULLUP_RAIL_MV,
           nocti_v4_err_name(q->last_result));
    if (q->state == NOCTI_V4_I2C_FAST_QUAL_NOT_RUN) {
        return;
    }
    printf("I2C_FMP_REG MODE1=0x%02X MODE2=0x%02X"
           " LEDOUT=%02X %02X %02X %02X %02X %02X"
           " EFLAG=%02X %02X %02X %02X %02X %02X\n",
           q->mode1, q->mode2, q->ledout[0], q->ledout[1], q->ledout[2],
           q->ledout[3], q->ledout[4], q->ledout[5], q->eflag[0], q->eflag[1],
           q->eflag[2], q->eflag[3], q->eflag[4], q->eflag[5]);
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
        printf("I2C_FMP_IREF source=%u channel=%u reg=0x%02X value=0x%02X\n",
               source, k_source_to_pca[source],
               (unsigned)(PCA_IREF0 + k_source_to_pca[source]),
               q->mapped_iref[source]);
    }
}

void nocti_v4_led_print_map(void)
{
    for (uint8_t source = 0u; source < NOCTI_V4_OPTICAL_SOURCE_COUNT; ++source) {
        const uint8_t channel = k_source_to_pca[source];
        printf("LED_MAP source=%u wavelength_nm=%u pca_channel=%u ledout_reg=0x%02X ledout_field_shift=%u\n",
               (unsigned)source, (source & 1u) == 0u ? 735u : 850u,
               (unsigned)channel, (unsigned)(PCA_LEDOUT0 + channel / 4u),
               (unsigned)((channel % 4u) * 2u));
    }
}

const char *nocti_v4_safe_off_state_name(nocti_v4_safe_off_state_t state)
{
    switch (state) {
        case NOCTI_V4_SAFE_OFF_REQUESTED: return "REQUESTED_OFF_NOT_VERIFIED";
        case NOCTI_V4_SAFE_OFF_REGISTER_VERIFIED: return "REGISTER_VERIFIED_OFF_PHYSICAL_UNVERIFIED";
        case NOCTI_V4_SAFE_OFF_RESET_ASSERTED: return "RESET_ASSERTED_OUTPUT_DISABLED";
        default: return "UNKNOWN";
    }
}

void nocti_v4_led_print_status(const nocti_v4_led_t *led)
{
    if (led == NULL) {
        return;
    }
    printf("LED_STATUS initialized=%u qdepth=%" PRIu32 " callbacks_registered=0 async_runtime=0 selected_source=%" PRId32 " safe_off=%s reset_asserted=%u oe=%s last_i2c=%s ledout_cache_valid=%u cache_all_off=%u\n",
           led->initialized ? 1u : 0u, led->i2c != NULL ? led->i2c->queue_depth : UINT32_MAX,
           led->selected_source, nocti_v4_safe_off_state_name(led->safe_off_state),
           led->board != NULL && led->board->pca_reset_asserted ? 1u : 0u,
           led->board != NULL ? nocti_v4_board_oe_state_name(led->board->oe_state) : "UNKNOWN",
           nocti_v4_err_name(led->last_result), led->ledout_cache_valid ? 1u : 0u,
           led->ledout_cache_valid && all_ledout_off(led->ledout) ? 1u : 0u);
}
