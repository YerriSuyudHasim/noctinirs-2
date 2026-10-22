#ifndef NN2_AD_CRED_H
#define NN2_AD_CRED_H

#include "nn2_port_wifi_cred_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open the dedicated nn2_wifi namespace and fill the credential-store port.
   Does not erase the NVS partition. Call from task context during bring-up,
   before the station driver starts. */
nn2_status_t nn2_ad_cred_init(nn2_port_wifi_cred_store_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NN2_AD_CRED_H */
