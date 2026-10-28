#ifndef NN2_PORT_NETWORK_CONFIG_H
#define NN2_PORT_NETWORK_CONFIG_H
#include "nn2_core_types.h"
/* Credentials are RAM values, never logged. save commits one versioned blob.
 * A failed save has an indeterminate persistence outcome: reload before retry.
 * A failed save never changes the application's output value. Task context only.
 * Security: NVS is not encrypted by this development profile. */
typedef struct {
    uint8_t version, ssid_length, password_length, reserved;
    char ssid[33], password[64];
} nn2_network_config_t;
typedef struct {
    void *ctx;
    nn2_status_t (*save)(void *ctx, const nn2_network_config_t *value);
} nn2_port_network_config_t;
#endif
