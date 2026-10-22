#ifndef NN2_WIFI_LIMITS_H
#define NN2_WIFI_LIMITS_H

#include <stdint.h>

/* Bounds for the M2 STA credential path (issue #30).
   SSID and passphrase lengths, the console budget, and the station
   poll period live here. Reconnect backoff and the connect guard do
   not: the Wi-Fi adapter supplies those and fails closed when they are
   missing. This header does not include sdkconfig.h and does not test
   ESP_PLATFORM. The 3 s grace timer that moves the device to STOPPED
   (issue #33 D1) is not implemented. */

#define NN2_WIFI_SSID_MAX 32u
#define NN2_WIFI_PASS_MIN 8u
#define NN2_WIFI_PASS_MAX 64u

/* Per-line console budget. Each wait inside the console adapter is finite. */
#define NN2_WIFI_PROV_TIMEOUT_MS 120000u
#define NN2_WIFI_PROV_RETRY_MS 2000u

/* Composition-task poll period. Kept finite so a later TWDT subscription
   does not inherit an unbounded wait. */
#define NN2_WIFI_EVENT_WAIT_MS 1000u
#define NN2_WIFI_WAIT_CAP_MS 60000u

#endif /* NN2_WIFI_LIMITS_H */
