#ifndef NN2_WIFI_CRED_H
#define NN2_WIFI_CRED_H

#include "nn2_status.h"
#include "nn2_wifi_limits.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned credential buffer. password is sensitive: do not log it.
   ssid holds NN2_WIFI_SSID_MAX octets plus a NUL.
   password holds NN2_WIFI_PASS_MAX octets plus a NUL. */
typedef struct {
    char ssid[NN2_WIFI_SSID_MAX + 1u];
    char password[NN2_WIFI_PASS_MAX + 1u];
    uint8_t ssid_len;
    uint8_t password_len;
} nn2_wifi_credentials_t;

/* Reject empty SSID, SSID longer than 32 octets, and a WPA2-PSK password
   shorter than 8 or longer than 64 octets. Control characters are rejected.
   Octets above 0x7F are allowed so a UTF-8 SSID can be stored. */
nn2_status_t nn2_wifi_cred_validate(const nn2_wifi_credentials_t *cred);

/* Overwrite the buffer, including the password. */
void nn2_wifi_cred_wipe(nn2_wifi_credentials_t *cred);

#ifdef __cplusplus
}
#endif

#endif /* NN2_WIFI_CRED_H */
