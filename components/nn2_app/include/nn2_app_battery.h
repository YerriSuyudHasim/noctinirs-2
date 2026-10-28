#ifndef NN2_APP_BATTERY_H
#define NN2_APP_BATTERY_H

#include "nn2_port_battery.h"

#define NN2_BATTERY_IDLE_PERIOD_US UINT64_C(60000000)
#define NN2_BATTERY_ACQUIRING_PERIOD_US UINT64_C(300000000)

/* Private state is copied, never cast in place; no special alignment needed. */
typedef struct {
    unsigned char opaque[128];
} nn2_app_battery_storage_t;

typedef struct {
    nn2_port_battery_sample_t sample;
    bool valid;
    bool attempted;
    uint64_t last_attempt_us;
    uint64_t last_success_us;
    nn2_status_t last_result;
} nn2_app_battery_status_t;

nn2_status_t nn2_app_battery_init(nn2_app_battery_storage_t *storage,
                                 const nn2_port_battery_t *port);
/* Monotonic microseconds, one task only. First eligible poll reads immediately.
 * Period is measured from last attempt: idle 60s, acquisition 300s. Transitions
 * use the current mode without a catch-up burst. A closed bus window defers a
 * due read without consuming the deadline. Failure retains the previous sample
 * and waits a full period before retry; it never changes charge enable state. */
nn2_status_t nn2_app_battery_poll(nn2_app_battery_storage_t *storage, uint64_t now_us,
                                 bool acquiring, bool i2c_window_open);
/* Cached data only: valid means a past successful read, not current freshness.
 * Age = current monotonic time - last_success_us; inspect last_result as well. */
nn2_app_battery_status_t nn2_app_battery_get_status(const nn2_app_battery_storage_t *storage);

#endif
