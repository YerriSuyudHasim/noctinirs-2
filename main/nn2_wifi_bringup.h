#ifndef NN2_WIFI_BRINGUP_H
#define NN2_WIFI_BRINGUP_H

#include "nn2_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Composition root for USB provision, the credential store, and STA
   reconnect. Creates nn2_prov and nn2_net, both pinned to core 0. */
nn2_status_t nn2_wifi_bringup(void);

#ifdef __cplusplus
}
#endif

#endif /* NN2_WIFI_BRINGUP_H */
