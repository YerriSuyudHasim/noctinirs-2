#include "nocti_v4_common.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "nocti_v4_wdt";
static uint32_t s_wdt_subscribe_errors;
static uint32_t s_wdt_kick_errors;
static uint32_t s_wdt_logged_subscribe_errors;
static uint32_t s_wdt_logged_kick_errors;

const char *nocti_v4_err_name(esp_err_t err)
{
    return esp_err_to_name(err);
}

bool nocti_v4_parse_u32(const char *text, uint32_t *out_value)
{
    if (text == NULL || out_value == NULL || *text == '\0' || isspace((unsigned char)*text)) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

void nocti_v4_task_wdt_subscribe(void)
{
    const esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_OK) {
        return;
    }
    (void)__atomic_fetch_add(&s_wdt_subscribe_errors, 1u, __ATOMIC_RELAXED);
    /* Task start, not the sample loop. */
    ESP_LOGW(TAG, "TWDT subscribe failed: %s", esp_err_to_name(err));
}

void nocti_v4_task_wdt_kick(void)
{
    const esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        (void)__atomic_fetch_add(&s_wdt_kick_errors, 1u, __ATOMIC_RELAXED);
    }
}

uint32_t nocti_v4_task_wdt_subscribe_errors(void)
{
    return __atomic_load_n(&s_wdt_subscribe_errors, __ATOMIC_RELAXED);
}

uint32_t nocti_v4_task_wdt_kick_errors(void)
{
    return __atomic_load_n(&s_wdt_kick_errors, __ATOMIC_RELAXED);
}

void nocti_v4_task_wdt_log_pending(void)
{
    const uint32_t subscribe_errors = nocti_v4_task_wdt_subscribe_errors();
    const uint32_t kick_errors = nocti_v4_task_wdt_kick_errors();
    const uint32_t logged_subscribe = __atomic_load_n(&s_wdt_logged_subscribe_errors,
                                                      __ATOMIC_RELAXED);
    const uint32_t logged_kick = __atomic_load_n(&s_wdt_logged_kick_errors,
                                                 __ATOMIC_RELAXED);
    if (subscribe_errors == logged_subscribe && kick_errors == logged_kick) {
        return;
    }
    __atomic_store_n(&s_wdt_logged_subscribe_errors, subscribe_errors, __ATOMIC_RELAXED);
    __atomic_store_n(&s_wdt_logged_kick_errors, kick_errors, __ATOMIC_RELAXED);
    ESP_LOGW(TAG, "TWDT errors subscribe=%" PRIu32 " kick=%" PRIu32,
             subscribe_errors, kick_errors);
}
