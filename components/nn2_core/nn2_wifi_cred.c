#include "nn2_wifi_cred.h"

#include <string.h>

static int is_rejected_octet(unsigned char octet)
{
    return octet < 0x20u || octet == 0x7fu;
}

nn2_status_t nn2_wifi_cred_validate(const nn2_wifi_credentials_t *cred)
{
    uint8_t i;

    if (cred == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (cred->ssid_len < 1u || cred->ssid_len > NN2_WIFI_SSID_MAX) {
        return NN2_ERR_INVALID;
    }
    if (cred->password_len < NN2_WIFI_PASS_MIN || cred->password_len > NN2_WIFI_PASS_MAX) {
        return NN2_ERR_INVALID;
    }
    if (cred->ssid[cred->ssid_len] != '\0' || cred->password[cred->password_len] != '\0') {
        return NN2_ERR_INVALID;
    }
    if (strlen(cred->ssid) != (size_t)cred->ssid_len) {
        return NN2_ERR_INVALID;
    }
    if (strlen(cred->password) != (size_t)cred->password_len) {
        return NN2_ERR_INVALID;
    }
    for (i = 0; i < cred->ssid_len; i++) {
        if (is_rejected_octet((unsigned char)cred->ssid[i])) {
            return NN2_ERR_INVALID;
        }
    }
    for (i = 0; i < cred->password_len; i++) {
        if (is_rejected_octet((unsigned char)cred->password[i])) {
            return NN2_ERR_INVALID;
        }
    }
    return NN2_OK;
}

void nn2_wifi_cred_wipe(nn2_wifi_credentials_t *cred)
{
    volatile uint8_t *byte;
    size_t i;

    if (cred == NULL) {
        return;
    }
    byte = (volatile uint8_t *)cred;
    for (i = 0; i < sizeof(*cred); i++) {
        byte[i] = 0u;
    }
}
