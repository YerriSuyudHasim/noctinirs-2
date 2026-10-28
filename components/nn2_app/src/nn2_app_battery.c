#include "nn2_app_battery.h"
#include <string.h>

typedef struct {
    nn2_port_battery_t port;
    nn2_app_battery_status_t status;
} battery_monitor_t;

_Static_assert(sizeof(battery_monitor_t) <= sizeof(nn2_app_battery_storage_t), "battery storage");

/* memcpy avoids imposing a private effective type on public byte storage. */
nn2_status_t nn2_app_battery_init(nn2_app_battery_storage_t *storage,
                                 const nn2_port_battery_t *port)
{
    if (storage == NULL || port == NULL || port->read == NULL) return NN2_ERR_INVALID_ARG;
    battery_monitor_t monitor = {.port = *port, .status.last_result = NN2_ERR_EMPTY};
    memset(storage, 0, sizeof(*storage));
    memcpy(storage->opaque, &monitor, sizeof(monitor));
    return NN2_OK;
}

nn2_status_t nn2_app_battery_poll(nn2_app_battery_storage_t *storage, uint64_t now_us,
                                 bool acquiring, bool i2c_window_open)
{
    if (storage == NULL) return NN2_ERR_INVALID_ARG;
    battery_monitor_t monitor;
    memcpy(&monitor, storage->opaque, sizeof(monitor));
    if (monitor.port.read == NULL) return NN2_ERR_INVALID_STATE;
    nn2_app_battery_status_t *s = &monitor.status;
    if (s->attempted && now_us < s->last_attempt_us) return NN2_ERR_INVALID_ARG;
    const uint64_t period = acquiring ? NN2_BATTERY_ACQUIRING_PERIOD_US : NN2_BATTERY_IDLE_PERIOD_US;
    if (!i2c_window_open || (s->attempted && now_us - s->last_attempt_us < period)) return NN2_OK;
    s->attempted = true;
    s->last_attempt_us = now_us;
    nn2_port_battery_sample_t sample;
    s->last_result = monitor.port.read(monitor.port.ctx, &sample);
    if (s->last_result == NN2_OK) {
        s->sample = sample;
        s->valid = true;
        s->last_success_us = now_us;
    }
    memcpy(storage->opaque, &monitor, sizeof(monitor));
    return s->last_result;
}

nn2_app_battery_status_t nn2_app_battery_get_status(const nn2_app_battery_storage_t *storage)
{
    if (storage == NULL) return (nn2_app_battery_status_t) {.last_result = NN2_ERR_INVALID_ARG};
    battery_monitor_t monitor;
    memcpy(&monitor, storage->opaque, sizeof(monitor));
    return monitor.status;
}
