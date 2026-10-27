#include "nn2_config.h"
#include "nn2_app_provision.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define NN2_NETWORK_BLOB_BYTES 99u
static bool s_initialized;

static void wipe(void *memory, size_t size)
{
    volatile uint8_t *p = memory;
    while (size--) {
        *p++ = 0;
    }
}

esp_err_t nn2_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t result = nvs_flash_init();
    s_initialized = result == ESP_OK;
    /* NO_FREE_PAGES/NEW_VERSION_FOUND are reported, never erased automatically. */
    return result;
}

esp_err_t nn2_config_save_network(const nn2_network_config_t *value)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!nn2_app_network_config_valid(value)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t blob[NN2_NETWORK_BLOB_BYTES] = { 1, value->ssid_length, value->password_length, 0 };
    memcpy(blob + 4, value->ssid, value->ssid_length);
    memcpy(blob + 36, value->password, value->password_length);
    nvs_handle_t handle;
    esp_err_t result = nvs_open("nn2_network", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, "wifi_cf1", blob, sizeof(blob));
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    wipe(blob, sizeof(blob));
    return result;
}

esp_err_t nn2_config_load_network(nn2_network_config_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open("nn2_network", NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint8_t blob[NN2_NETWORK_BLOB_BYTES] = { 0 };
    size_t length = sizeof(blob);
    result = nvs_get_blob(handle, "wifi_cf1", blob, &length);
    nvs_close(handle);
    nn2_network_config_t value = { 0 };
    if (result == ESP_OK) {
        value.version = blob[0];
        value.ssid_length = blob[1];
        value.password_length = blob[2];
        value.reserved = blob[3];
        memcpy(value.ssid, blob + 4, 32);
        memcpy(value.password, blob + 36, 63);
        if (length != sizeof(blob) || !nn2_app_network_config_valid(&value)) {
            result = ESP_ERR_INVALID_ARG;
        } else {
            *out = value;
        }
    }
    wipe(blob, sizeof(blob));
    wipe(&value, sizeof(value));
    return result;
}

static nn2_status_t save(void *ctx, const nn2_network_config_t *value)
{
    (void)ctx;
    return nn2_config_save_network(value) == ESP_OK ? NN2_OK : NN2_ERR_IO;
}

nn2_port_network_config_t nn2_config_storage_port(void)
{
    return (nn2_port_network_config_t) { .save = save };
}

esp_err_t nn2_config_next_boot_id(uint64_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open("nn2_network", NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint64_t previous = 0;
    result = nvs_get_u64(handle, "boot_id", &previous);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    }
    if (result == ESP_OK && previous == UINT64_MAX) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        result = nvs_set_u64(handle, "boot_id", previous + 1u);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        *out = previous + 1u;
    }
    return result;
}

