#ifndef NN2_WIFI_BACKOFF_H
#define NN2_WIFI_BACKOFF_H

#include "nn2_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure reconnect-delay state machine. No timer, task, or busy-loop lives here.
   The first next_ms() after init or reset returns 0 (immediate retry).
   Later values start at first_ms, double by factor, and never exceed max_ms. */
typedef struct {
    uint32_t first_ms;
    uint32_t max_ms;
    uint32_t current_ms;
    uint8_t factor;
    uint8_t immediate_pending;
} nn2_wifi_backoff_t;

/* factor must be >= 2. max_ms must be >= first_ms. first_ms must be > 0.
   The 0 ms immediate retry is produced by the machine; it is not first_ms. */
nn2_status_t nn2_wifi_backoff_init(nn2_wifi_backoff_t *backoff,
                                   uint32_t first_ms,
                                   uint32_t max_ms,
                                   uint8_t factor);

void nn2_wifi_backoff_reset(nn2_wifi_backoff_t *backoff);

/* Returns 0 when backoff is NULL, and also for the first delay after a
   successful init or reset. Later delays are in [first_ms, max_ms]. */
uint32_t nn2_wifi_backoff_next_ms(nn2_wifi_backoff_t *backoff);

#ifdef __cplusplus
}
#endif

#endif /* NN2_WIFI_BACKOFF_H */
