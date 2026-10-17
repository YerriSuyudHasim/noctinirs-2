#ifndef NN2_WIFI_LIMITS_H
#define NN2_WIFI_LIMITS_H

#include <stdint.h>

/* Bounds for the M2 STA credential path (issue #30).
   Target builds pass NN2_WIFI_BACKOFF_FIRST_MS, NN2_WIFI_BACKOFF_MAX_MS,
   NN2_WIFI_BACKOFF_FACTOR, and NN2_WIFI_CONNECT_GUARD_MS from
   nn2_core target_compile_definitions (Kconfig values for the two
   backoff delays; the factor and connect guard are fixed policy).
   This header does not include sdkconfig.h. Host tests leave
   ESP_PLATFORM unset and take the defaults below. The 3 s grace timer
   that moves the device to STOPPED (issue #33 D1) is not implemented. */

#define NN2_WIFI_SSID_MAX 32u
#define NN2_WIFI_PASS_MIN 8u
#define NN2_WIFI_PASS_MAX 64u

#if defined(ESP_PLATFORM)
#if !defined(NN2_WIFI_BACKOFF_FIRST_MS)
#error "NN2_WIFI_BACKOFF_FIRST_MS is required on target"
#endif
#if !defined(NN2_WIFI_BACKOFF_MAX_MS)
#error "NN2_WIFI_BACKOFF_MAX_MS is required on target"
#endif
#if !defined(NN2_WIFI_BACKOFF_FACTOR)
#error "NN2_WIFI_BACKOFF_FACTOR is required on target"
#endif
#if !defined(NN2_WIFI_CONNECT_GUARD_MS)
#error "NN2_WIFI_CONNECT_GUARD_MS is required on target"
#endif
#else
#ifndef NN2_WIFI_BACKOFF_FIRST_MS
#define NN2_WIFI_BACKOFF_FIRST_MS 500
#endif
#ifndef NN2_WIFI_BACKOFF_MAX_MS
#define NN2_WIFI_BACKOFF_MAX_MS 8000
#endif
#ifndef NN2_WIFI_BACKOFF_FACTOR
#define NN2_WIFI_BACKOFF_FACTOR 2
#endif
#ifndef NN2_WIFI_CONNECT_GUARD_MS
#define NN2_WIFI_CONNECT_GUARD_MS 15000
#endif
#endif

/* First backoff slot must sit inside the 3 s Wi-Fi-loss grace window and
   must be at most 1 s (RTOS note on #30). The immediate retry is 0 ms and
   is not this value. */
_Static_assert(NN2_WIFI_BACKOFF_FIRST_MS > 0, "first backoff delay must be non-zero");
_Static_assert(NN2_WIFI_BACKOFF_FIRST_MS <= 1000, "first backoff delay must be <= 1000 ms");
_Static_assert(NN2_WIFI_BACKOFF_FIRST_MS < 3000, "first backoff delay must be < 3000 ms");
_Static_assert(NN2_WIFI_BACKOFF_MAX_MS >= NN2_WIFI_BACKOFF_FIRST_MS, "backoff cap is below the first delay");

/* Per-line console budget. Each wait inside the console adapter is finite. */
#define NN2_WIFI_PROV_TIMEOUT_MS 120000u
#define NN2_WIFI_PROV_RETRY_MS 2000u

/* Composition-task poll period. Kept finite so a later TWDT subscription
   does not inherit an unbounded wait. */
#define NN2_WIFI_EVENT_WAIT_MS 1000u
#define NN2_WIFI_WAIT_CAP_MS 60000u

/* A successful esp_wifi_connect stays in flight until LINK_UP, LINK_DOWN,
   or NN2_WIFI_CONNECT_GUARD_MS. The 1 s poll must not start another
   attempt before then. The link reads DOWN for the whole association
   and DHCP interval. The guard value is injected above. */

_Static_assert(NN2_WIFI_CONNECT_GUARD_MS > NN2_WIFI_EVENT_WAIT_MS,
               "connect guard must outlast one station poll");
_Static_assert(NN2_WIFI_CONNECT_GUARD_MS <= NN2_WIFI_WAIT_CAP_MS,
               "connect guard must stay inside the wait cap");

#endif /* NN2_WIFI_LIMITS_H */
