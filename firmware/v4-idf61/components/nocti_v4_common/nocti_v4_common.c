#include "nocti_v4_common.h"
#include "nocti_v4_sample_edge.h"

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

nocti_v4_sample_edge_state_t nocti_v4_sample_edge_state;

void nocti_v4_sample_edge_arm(TaskHandle_t task, uint32_t generation)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    if (generation == 0u) {
        generation = 1u;
    }
    __atomic_store_n(&edge->have_submit, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->wake_pending, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->watch_armed, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->latch_valid, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->observed_valid, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->task, task, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->generation, generation, __ATOMIC_RELEASE);
}

void nocti_v4_sample_edge_disarm(void)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    __atomic_store_n(&edge->generation, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&edge->have_submit, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&edge->watch_armed, 0u, __ATOMIC_RELEASE);
}

void nocti_v4_sample_edge_set_submit(uint32_t sequence)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    __atomic_store_n(&edge->next_submit_sequence, sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->have_submit, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&edge->wake_pending, 0u, __ATOMIC_RELEASE);
}

void nocti_v4_sample_edge_set_watch(uint32_t sequence)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    __atomic_store_n(&edge->latch_valid, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->watch_sequence, sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->watch_armed, 1u, __ATOMIC_RELEASE);
}

int IRAM_ATTR nocti_v4_sample_edge_observed(uint32_t *sequence, uint32_t *generation)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    if (sequence == NULL ||
        __atomic_load_n(&edge->observed_valid, __ATOMIC_ACQUIRE) == 0u) {
        return 0;
    }
    *sequence = __atomic_load_n(&edge->observed_sequence, __ATOMIC_RELAXED);
    if (generation != NULL) {
        *generation = __atomic_load_n(&edge->observed_generation, __ATOMIC_RELAXED);
    }
    return 1;
}

int nocti_v4_sample_edge_copy_latch(uint32_t *sequence, uint64_t *timestamp_us)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    uint32_t low;
    uint32_t high;
    if (sequence == NULL || timestamp_us == NULL ||
        __atomic_load_n(&edge->latch_valid, __ATOMIC_ACQUIRE) == 0u) {
        return 0;
    }
    *sequence = __atomic_load_n(&edge->latched_sequence, __ATOMIC_RELAXED);
    low = __atomic_load_n(&edge->latched_us_lo, __ATOMIC_RELAXED);
    high = __atomic_load_n(&edge->latched_us_hi, __ATOMIC_RELAXED);
    *timestamp_us = (uint64_t)low | ((uint64_t)high << 32);
    return 1;
}
