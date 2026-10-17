#ifndef NN2_PORT_WIFI_STATION_H
#define NN2_PORT_WIFI_STATION_H

#include "nn2_status.h"
#include "nn2_wifi_cred.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WifiStationPort (nn2_port_wifi_station).
   Locked by Owner A1 on issue #30 (comment 6226888814), following the
   Architect planning lock (comment 6226211962). Station only. No SoftAP.

   Caller context: task. ISR use is forbidden.
   Blocking: connect, disconnect, get_status, schedule_reconnect, and
   reconnect_now are nonblocking requests. wait_event blocks at most
   timeout_ms milliseconds (capped by the adapter) and returns
   NN2_ERR_TIMEOUT. Tick conversion stays in the adapter.
   Ownership: credential pointers are borrowed for the call. The adapter
   must not retain the password after connect returns.
   Errors: NN2_ERR_INVALID_ARG, NN2_ERR_INVALID, NN2_ERR_TIMEOUT,
   NN2_ERR_IO, NN2_ERR_NO_MEM, NN2_ERR_BUSY.

   Link report: LINK_UP means the station has an IPv4 address. LINK_DOWN
   means the association is down. This port only reports that fact. It does
   not stop a stream and it does not command emitters. Owner lock O6 on
   issue #33 is a later issue.

   Timing expectation: reconnect delay is bounded by the core backoff
   machine. No on-target latency claim is made (Class H NOT RUN). */

typedef enum {
    NN2_WIFI_LINK_DOWN = 0,
    NN2_WIFI_LINK_UP = 1
} nn2_wifi_link_t;

typedef enum {
    NN2_WIFI_STA_EVT_LINK_DOWN = 1,
    NN2_WIFI_STA_EVT_LINK_UP = 2,
    NN2_WIFI_STA_EVT_RECONNECT_DUE = 3
} nn2_wifi_sta_evt_t;

typedef struct {
    nn2_wifi_link_t link;
    uint32_t ipv4_addr;
    uint32_t disconnect_count;
    uint32_t auth_fail_count;
    uint32_t event_overflow_count;
    bool auth_failed_last;
    /* True after a successful connect request until disconnect. */
    bool want_connected;
    /* True while the one-shot reconnect timer is armed. */
    bool reconnect_timer_armed;
} nn2_wifi_sta_status_t;

typedef struct nn2_port_wifi_station {
    nn2_status_t (*connect)(void *ctx, const nn2_wifi_credentials_t *creds);
    nn2_status_t (*disconnect)(void *ctx);
    nn2_status_t (*get_status)(void *ctx, nn2_wifi_sta_status_t *out);
    /* Arm one bounded backoff interval. Does not wait and does not loop. */
    nn2_status_t (*schedule_reconnect)(void *ctx);
    /* Ask the driver to associate once. Called from the station task after
       the backoff interval, not from the Wi-Fi event callback. A failure is
       returned to that task, which schedules the next backoff step. */
    nn2_status_t (*reconnect_now)(void *ctx);
    nn2_status_t (*wait_event)(void *ctx, nn2_wifi_sta_evt_t *evt, uint32_t timeout_ms);
    /* Station task only, after LINK_UP. Reset backoff and stop the timer. */
    nn2_status_t (*on_link_up)(void *ctx);
    void *ctx;
} nn2_port_wifi_station_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_PORT_WIFI_STATION_H */
