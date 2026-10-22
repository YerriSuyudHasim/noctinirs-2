#ifndef NN2_APP_WIFI_H
#define NN2_APP_WIFI_H

#include "nn2_port_prov_console.h"
#include "nn2_port_wifi_cred_store.h"
#include "nn2_port_wifi_station.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Application policy for M2 provisioning. No ESP-IDF or FreeRTOS types.
   Link loss schedules a station reconnect only. It does not stop a stream
   and it does not command emitters (issue #33 O6 and O8 are out of scope). */

typedef struct {
    const nn2_port_wifi_cred_store_t *store;
    const nn2_port_prov_console_t *console;
    uint32_t console_timeout_ms;
} nn2_app_wifi_provision_deps_t;

/* Load credentials. When they are missing, invalid, or the store reports
   corruption, read the console once, validate, and save. On success, out
   receives a copy and this function does not connect the station. */
nn2_status_t nn2_app_wifi_provision_if_needed(const nn2_app_wifi_provision_deps_t *deps,
                                              nn2_wifi_credentials_t *out);

/* Validate and ask the station port to connect. Does not log the password. */
nn2_status_t nn2_app_wifi_connect_from_creds(const nn2_port_wifi_station_t *station,
                                             const nn2_wifi_credentials_t *creds);

/* Record link up or down in status_out.
   LINK_DOWN clears connect_in_flight and calls schedule_reconnect.
   It does not stop a stream and it does not command emitters.
   LINK_UP clears connect_in_flight, records the link, calls on_link_up
   when the port provides it, and does not schedule a reconnect. */
nn2_status_t nn2_app_wifi_on_link_event(const nn2_port_wifi_station_t *station,
                                        nn2_wifi_link_t link,
                                        nn2_wifi_sta_status_t *status_out);

/* One station-task poll timed out. Schedule a reconnect when the link is
   down, a connection is still wanted, and the backoff timer is not armed.
   That covers a LINK_DOWN or RECONNECT_DUE dropped by a full queue.
   While connect_in_flight is set and connect_attempt_ms is below
   connect_guard_ms, do not schedule: the link stays down until GOT_IP.
   When the guard expires, schedule the next backoff step. The adapter
   clears connect_in_flight on that call. connect_guard_ms is supplied by
   the caller; this layer does not own that limit. */
nn2_status_t nn2_app_wifi_on_wait_timeout(const nn2_port_wifi_station_t *station,
                                         const nn2_wifi_sta_status_t *status,
                                         uint32_t connect_guard_ms);

/* reconnect_now failed. Schedule the next backoff step. NN2_OK does not.
   A stale RECONNECT_DUE after LINK_UP makes reconnect_now return NN2_OK,
   so this function does not schedule another attempt. */
nn2_status_t nn2_app_wifi_on_reconnect_result(const nn2_port_wifi_station_t *station,
                                             nn2_status_t reconnect_status);

#ifdef __cplusplus
}
#endif

#endif /* NN2_APP_WIFI_H */
