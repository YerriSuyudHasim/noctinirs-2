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
   LINK_DOWN calls schedule_reconnect and nothing else.
   LINK_UP records the link and does not schedule a reconnect. */
nn2_status_t nn2_app_wifi_on_link_event(const nn2_port_wifi_station_t *station,
                                        nn2_wifi_link_t link,
                                        nn2_wifi_sta_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* NN2_APP_WIFI_H */
