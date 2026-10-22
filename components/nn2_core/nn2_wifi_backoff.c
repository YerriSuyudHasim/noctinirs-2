#include "nn2_wifi_backoff.h"

#include <stddef.h>

nn2_status_t nn2_wifi_backoff_init(nn2_wifi_backoff_t *backoff,
                                   uint32_t first_ms,
                                   uint32_t max_ms,
                                   uint8_t factor)
{
    if (backoff == NULL || first_ms == 0u || max_ms < first_ms || factor < 2u) {
        return NN2_ERR_INVALID_ARG;
    }
    backoff->first_ms = first_ms;
    backoff->max_ms = max_ms;
    backoff->factor = factor;
    backoff->current_ms = first_ms;
    backoff->immediate_pending = 1u;
    return NN2_OK;
}

void nn2_wifi_backoff_reset(nn2_wifi_backoff_t *backoff)
{
    if (backoff == NULL) {
        return;
    }
    backoff->current_ms = backoff->first_ms;
    backoff->immediate_pending = 1u;
}

uint32_t nn2_wifi_backoff_next_ms(nn2_wifi_backoff_t *backoff)
{
    uint32_t delay;
    uint32_t grown;

    if (backoff == NULL || backoff->factor < 2u || backoff->max_ms == 0u) {
        return 0u;
    }
    if (backoff->immediate_pending) {
        backoff->immediate_pending = 0u;
        backoff->current_ms = backoff->first_ms;
        return 0u;
    }
    delay = backoff->current_ms;
    if (delay == 0u || delay > backoff->max_ms) {
        delay = backoff->max_ms;
    }
    if (delay > backoff->max_ms / (uint32_t)backoff->factor) {
        backoff->current_ms = backoff->max_ms;
    } else {
        grown = delay * (uint32_t)backoff->factor;
        backoff->current_ms = (grown > backoff->max_ms) ? backoff->max_ms : grown;
    }
    return delay;
}
