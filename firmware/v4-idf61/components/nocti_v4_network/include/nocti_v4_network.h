#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v4_health.h"
#include "nocti_v4_optical.h"
#include "nocti_v4_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOCTI_V4_WIFI_AP_SSID             "NOCTINIRS"
#include "sdkconfig.h"
/* Kconfig omits this string when the NN2 communication mode is selected.
 * Native V4 networking remains compiled for diagnostics, but is not started.
 * An empty fallback still fails its existing credential-length validation. */
#if defined(CONFIG_NOCTI_V4_MIGRATION_WIFI_PASSWORD)
#define NOCTI_V4_WIFI_AP_PASSWORD CONFIG_NOCTI_V4_MIGRATION_WIFI_PASSWORD
#else
#define NOCTI_V4_WIFI_AP_PASSWORD ""
#endif
#define NOCTI_V4_WIFI_AP_CHANNEL          6u
#define NOCTI_V4_WIFI_AP_MAX_CONNECTIONS  1u
#define NOCTI_V4_UDP_CONTROL_PORT         3333u
#define NOCTI_V4_UDP_STREAM_PORT          3334u
#define NOCTI_V4_NETWORK_FRAME_RING_CAPACITY 2u
#define NOCTI_V4_NETWORK_TASK_CORE        0
#define NOCTI_V4_NETWORK_TASK_PRIORITY    4u
#define NOCTI_V4_NETWORK_TASK_STACK_BYTES 6144u

typedef enum {
    NOCTI_V4_NETWORK_STOPPED = 0,
    NOCTI_V4_NETWORK_STARTING,
    NOCTI_V4_NETWORK_READY,
    NOCTI_V4_NETWORK_ERROR,
} nocti_v4_network_lifecycle_t;

typedef struct {
    bool initialized;
    nocti_v4_network_lifecycle_t lifecycle;
    bool ap_started;
    uint32_t station_count;
    bool peer_valid;
    uint32_t peer_ipv4_host_order;
    bool stream_enabled;
    bool legacy_compat_enabled;
    bool scan_session_accepting;
    uint32_t scan_session_epoch;
    uint16_t control_port;
    uint16_t stream_port;
    uint32_t ring_depth;
    int32_t task_core;
    uint32_t task_priority;
    uint32_t task_stack_watermark_bytes;
    esp_err_t last_error;
} nocti_v4_network_status_t;

typedef struct {
    void *runtime;
    bool initialized;
} nocti_v4_network_t;

esp_err_t nocti_v4_network_init(nocti_v4_network_t *network,
                                nocti_v4_time_t *timebase,
                                nocti_v4_health_t *health);
/* Nonblocking bounded sink called by the priority-5 completed-frame task. */
bool nocti_v4_network_frame_sink(
    void *context, const nocti_v4_completed_optical_frame_t *frame);
void nocti_v4_network_begin_scan_session(nocti_v4_network_t *network,
                                         uint32_t session_epoch);
/* Stops accepting frames and boundedly drains already-owned network frames. */
esp_err_t nocti_v4_network_end_scan_session(nocti_v4_network_t *network,
                                             uint32_t timeout_ms);
const char *nocti_v4_network_lifecycle_name(nocti_v4_network_lifecycle_t state);
void nocti_v4_network_get_status(const nocti_v4_network_t *network,
                                 nocti_v4_network_status_t *out_status);
void nocti_v4_network_print_status(const nocti_v4_network_t *network);
void nocti_v4_network_print_health(const nocti_v4_network_t *network);

#ifdef __cplusplus
}
#endif
