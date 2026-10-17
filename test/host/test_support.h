#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "nn2_wifi_cred.h"

#include <string.h>

/* Synthetic fixtures only. These are not real network credentials. */
static void test_fill_cred(nn2_wifi_credentials_t *cred, const char *ssid, const char *password)
{
    size_t ssid_len;
    size_t password_len;

    memset(cred, 0, sizeof(*cred));
    ssid_len = strlen(ssid);
    password_len = strlen(password);
    if (ssid_len > NN2_WIFI_SSID_MAX) {
        ssid_len = NN2_WIFI_SSID_MAX;
    }
    if (password_len > NN2_WIFI_PASS_MAX) {
        password_len = NN2_WIFI_PASS_MAX;
    }
    memcpy(cred->ssid, ssid, ssid_len);
    memcpy(cred->password, password, password_len);
    cred->ssid_len = (uint8_t)ssid_len;
    cred->password_len = (uint8_t)password_len;
}

#endif /* TEST_SUPPORT_H */
