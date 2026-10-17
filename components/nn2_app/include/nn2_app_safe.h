#ifndef NN2_APP_SAFE_H
#define NN2_APP_SAFE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run states that the composition root understands. FAULTED includes
   boot self-test failure. BOOT is the state before tasks exist. */
typedef enum {
    NN2_APP_BOOT = 0,
    NN2_APP_FAULTED = 1,
    NN2_APP_SAFE_OFF = 2,
    NN2_APP_STOPPED = 3,
    NN2_APP_RUNNING = 4
} nn2_app_run_state_t;

/* True when the ADS131 SYNC/RESET line must be held low.
   BOOT, FAULTED, and SAFE_OFF require the hold. */
bool nn2_app_adc_hold_required(nn2_app_run_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* NN2_APP_SAFE_H */
