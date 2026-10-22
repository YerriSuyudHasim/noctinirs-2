#ifndef NN2_PORT_WIFI_CRED_STORE_H
#define NN2_PORT_WIFI_CRED_STORE_H

#include "nn2_status.h"
#include "nn2_wifi_cred.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WifiCredentialStorePort (nn2_port_wifi_cred_store).
   Locked by Owner A1 on issue #30 (comment 6226888814).

   Caller context: task. ISR use is forbidden. Do not call from a Wi-Fi
   event callback. save commits storage before it returns; that commit
   stays on the provisioning task.
   Blocking: load, save, and clear may block for the storage operation.
   They are boot and provision paths, not the optical hot path.
   Ownership: the caller owns the credential buffer. The store copies
   bytes into its own storage on save and copies them out on load.
   The password must not be written to a log.
   Errors: NN2_ERR_MISSING when no credentials are stored,
   NN2_ERR_INVALID when stored bytes fail validation,
   NN2_ERR_NVS_CORRUPT when the record cannot be read,
   NN2_ERR_IO, NN2_ERR_INVALID_ARG, NN2_ERR_NO_MEM.

   The adapter maps this port onto one dedicated namespace. It does not
   use the Wi-Fi driver's internal namespace. No encrypted-storage claim
   is made.

   Timing expectation: storage latency is not characterized (Class H
   NOT RUN). */

typedef struct nn2_port_wifi_cred_store {
    nn2_status_t (*load)(void *ctx, nn2_wifi_credentials_t *out);
    nn2_status_t (*save)(void *ctx, const nn2_wifi_credentials_t *in);
    nn2_status_t (*clear)(void *ctx);
    void *ctx;
} nn2_port_wifi_cred_store_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_PORT_WIFI_CRED_STORE_H */
