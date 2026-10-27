#include "nn2_provision.h"
#include "nn2_app_provision.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include <string.h>

static nn2_port_network_config_t s_storage;
static nn2_provision_status_fn s_status_callback;
static nn2_provision_safe_off_fn s_safe_off_callback;
static nn2_provision_status_t s_status;
static char s_line[197];
static size_t s_used;
static bool s_discard, s_cr, s_initialized;
static uint64_t s_started_at;

static void wipe(void *memory, size_t size)
{
    volatile uint8_t *p = memory;
    while (size--) {
        *p++ = 0;
    }
}

static void reset_line(void)
{
    wipe(s_line, sizeof(s_line));
    s_used = 0;
    s_discard = false;
    s_cr = false;
    s_started_at = 0;
}

static void reply(const char *message)
{
    size_t size = strlen(message);
    if (usb_serial_jtag_write_bytes(message, size, 0) != (int)size) {
        ++s_status.tx_drops;
    }
}

esp_err_t nn2_provision_init(
    const nn2_port_network_config_t *storage, nn2_provision_status_fn status_callback,
    nn2_provision_safe_off_fn safe_off_callback)
{
    if ((storage && !storage->save) || !status_callback || !safe_off_callback) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb.tx_buffer_size = 1536; /* Complete STATUS including cached battery/LED telemetry. */
    usb.rx_buffer_size = 512;
    esp_err_t result = usb_serial_jtag_driver_install(&usb);
    if (result != ESP_OK) {
        return result;
    }
    /* Keep SDK console output and our bounded writes on the same TX driver.
     * IDF v6.1's VFS path has a finite disconnected-host TX flush timeout. */
    usb_serial_jtag_vfs_use_driver();
    if (storage) s_storage = *storage;
    s_status_callback = status_callback;
    s_safe_off_callback = safe_off_callback;
    s_initialized = true;
    reply(storage ? "NN2 READY; WIFI or STATUS; no echo\n" : "NN2 READY; SoftAP-only; STATUS; no echo\n");
    return ESP_OK;
}

bool nn2_provision_poll(uint64_t now_us)
{
    if (!s_initialized) {
        return false;
    }
    if (s_used && now_us - s_started_at >= UINT64_C(10000000)) {
        wipe(s_line, sizeof(s_line));
        s_used = 0;
        s_discard = true; /* Drain through LF before accepting another command. */
        ++s_status.timed_out_lines;
    }
    uint8_t chunk[64];
    int count = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), 0);
    bool stored = false;
    for (int i = 0; i < count; ++i) {
        uint8_t byte = chunk[i];
        if (byte == '\n') {
            if (s_discard) {
                ++s_status.rejected_lines;
                reply("ERR malformed-or-expired-line\n");
            } else if (s_used == 8 && !memcmp(s_line, "SAFE_OFF", 8)) {
                reply(s_safe_off_callback() == NN2_OK ?
                    "OK safe-off requested; physical=unverified\n" : "ERR safe-off failed\n");
            } else if (s_used == 6 && !memcmp(s_line, "STATUS", 6)) {
                char status[1024] = { 0 };
                s_status_callback(status, sizeof(status));
                status[sizeof(status) - 1] = 0;
                reply(status);
            } else if (s_used && !s_storage.save) {
                ++s_status.rejected_lines;
                reply("ERR unsupported; SoftAP-only; use STATUS\n");
            } else if (s_used) {
                nn2_network_config_t config = { 0 };
                nn2_status_t result = nn2_app_provision(s_line, s_used, &s_storage, &config);
                wipe(&config, sizeof(config));
                if (result == NN2_OK) {
                    stored = true;
                    reply("OK candidate-accepted; poll STATUS for connection and persistence\n");
                } else {
                    ++s_status.rejected_lines;
                    reply(result == NN2_ERR_IO ? "ERR candidate-rejected\n"
                                               : "ERR format-or-length\n");
                }
            }
            reset_line();
        } else if (s_discard) {
            continue;
        } else if (byte == '\r' && !s_cr) {
            s_cr = true;
        } else if (s_cr || !byte || s_used == sizeof(s_line) - 1) {
            wipe(s_line, sizeof(s_line));
            s_used = 0;
            s_discard = true;
        } else {
            if (!s_used) {
                s_started_at = now_us;
            }
            s_line[s_used++] = (char)byte;
        }
    }
    wipe(chunk, sizeof(chunk));
    return stored;
}

nn2_provision_status_t nn2_provision_get_status(void)
{
    return s_status;
}
