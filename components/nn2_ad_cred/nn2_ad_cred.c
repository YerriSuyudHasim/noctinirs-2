#include "nn2_ad_cred.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nn2_cred";

/* Dedicated application namespace. Not the Wi-Fi driver namespace.
   NVS namespace names are at most 15 characters; this one is 8. */
static const char *const NN2_NVS_NAMESPACE = "nn2_wifi";
static const char *const NN2_NVS_KEY_SSID = "ssid";
static const char *const NN2_NVS_KEY_PASS = "pass";

static nvs_handle_t s_handle;
static int s_open;

static nn2_status_t map_nvs(esp_err_t err)
{
    if (err == ESP_OK) {
        return NN2_OK;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return NN2_ERR_MISSING;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_NVS_INVALID_STATE ||
        err == ESP_ERR_NVS_TYPE_MISMATCH || err == ESP_ERR_NVS_INVALID_HANDLE ||
        err == ESP_ERR_NVS_CORRUPT_KEY_PART) {
        return NN2_ERR_NVS_CORRUPT;
    }
    if (err == ESP_ERR_NO_MEM || err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
        return NN2_ERR_NO_MEM;
    }
    return NN2_ERR_IO;
}

static nn2_status_t read_key(const char *key, char *buf, size_t cap, uint8_t *out_len)
{
    size_t stored = cap;
    esp_err_t err;
    size_t text_len;

    err = nvs_get_str(s_handle, key, buf, &stored);
    if (err != ESP_OK) {
        return map_nvs(err);
    }
    if (stored == 0u || buf[0] == '\0') {
        *out_len = 0u;
        return NN2_OK;
    }
    text_len = stored - 1u;
    if (text_len > 255u) {
        return NN2_ERR_NVS_CORRUPT;
    }
    *out_len = (uint8_t)text_len;
    return NN2_OK;
}

static nn2_status_t ad_load(void *ctx, nn2_wifi_credentials_t *out)
{
    nn2_wifi_credentials_t cred;
    nn2_status_t ssid_status;
    nn2_status_t pass_status;
    nn2_status_t valid;

    (void)ctx;
    if (!s_open || out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    nn2_wifi_cred_wipe(&cred);
    ssid_status = read_key(NN2_NVS_KEY_SSID, cred.ssid, sizeof(cred.ssid), &cred.ssid_len);
    pass_status = read_key(NN2_NVS_KEY_PASS, cred.password, sizeof(cred.password), &cred.password_len);
    if (ssid_status == NN2_ERR_MISSING && pass_status == NN2_ERR_MISSING) {
        nn2_wifi_cred_wipe(&cred);
        nn2_wifi_cred_wipe(out);
        return NN2_ERR_MISSING;
    }
    if (ssid_status != NN2_OK || pass_status != NN2_OK) {
        nn2_wifi_cred_wipe(&cred);
        nn2_wifi_cred_wipe(out);
        if (ssid_status == NN2_ERR_NVS_CORRUPT || pass_status == NN2_ERR_NVS_CORRUPT) {
            return NN2_ERR_NVS_CORRUPT;
        }
        if (ssid_status == NN2_ERR_MISSING || pass_status == NN2_ERR_MISSING) {
            return NN2_ERR_NVS_CORRUPT;
        }
        return (ssid_status != NN2_OK) ? ssid_status : pass_status;
    }
    valid = nn2_wifi_cred_validate(&cred);
    if (valid != NN2_OK) {
        nn2_wifi_cred_wipe(&cred);
        nn2_wifi_cred_wipe(out);
        return NN2_ERR_INVALID;
    }
    *out = cred;
    nn2_wifi_cred_wipe(&cred);
    return NN2_OK;
}

static nn2_status_t ad_save(void *ctx, const nn2_wifi_credentials_t *in)
{
    nn2_status_t valid;
    esp_err_t err;

    (void)ctx;
    if (!s_open || in == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    valid = nn2_wifi_cred_validate(in);
    if (valid != NN2_OK) {
        return valid;
    }
    err = nvs_set_str(s_handle, NN2_NVS_KEY_SSID, in->ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ssid store failed: %d", (int)err);
        return map_nvs(err);
    }
    err = nvs_set_str(s_handle, NN2_NVS_KEY_PASS, in->password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "password store failed: %d", (int)err);
        return map_nvs(err);
    }
    /* Commit runs on the caller task. The Wi-Fi event callback does not
       reach this function. */
    err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "store commit failed: %d", (int)err);
        return map_nvs(err);
    }
    ESP_LOGI(TAG, "credentials stored (ssid length %u)", (unsigned)in->ssid_len);
    return NN2_OK;
}

static nn2_status_t ad_clear(void *ctx)
{
    esp_err_t err;

    (void)ctx;
    if (!s_open) {
        return NN2_ERR_INVALID_ARG;
    }
    err = nvs_erase_all(s_handle);
    if (err != ESP_OK) {
        return map_nvs(err);
    }
    err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        return map_nvs(err);
    }
    ESP_LOGI(TAG, "credential namespace cleared");
    return NN2_OK;
}

nn2_status_t nn2_ad_cred_init(nn2_port_wifi_cred_store_t *out)
{
    esp_err_t err;

    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (s_open) {
        return NN2_ERR_BUSY;
    }
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Partition-level failure. Do not erase every namespace. */
        ESP_LOGE(TAG, "nvs init needs maintenance: %d", (int)err);
        return NN2_ERR_NVS_CORRUPT;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %d", (int)err);
        return map_nvs(err);
    }
    err = nvs_open(NN2_NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "namespace open failed: %d", (int)err);
        return map_nvs(err);
    }
    s_open = 1;
    out->load = ad_load;
    out->save = ad_save;
    out->clear = ad_clear;
    out->ctx = NULL;
    return NN2_OK;
}
