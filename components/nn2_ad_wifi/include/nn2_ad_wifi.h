#ifndef NN2_AD_WIFI_H
#define NN2_AD_WIFI_H

#include "nn2_port_wifi_station.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESP-IDF station adapter. Reports link up and link down through the port.
   Reconnect delay is armed with a one-shot timer and completed on the
   station task. The event callback does not commit storage and does not
   busy-loop. */
nn2_status_t nn2_ad_wifi_init(nn2_port_wifi_station_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NN2_AD_WIFI_H */
