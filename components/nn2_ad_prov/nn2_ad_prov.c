#include "nn2_ad_prov.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/* ESP-IDF headers test unset CONFIG_* and build macros with #if.
   -Werror=undef stays on for this file; those headers are not our code. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_log.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop

static const char *TAG = "nn2_prov";

static void wipe_buf(char *buf, size_t cap)
{
    volatile char *byte;
    size_t i;

    byte = (volatile char *)buf;
    for (i = 0; i < cap; i++) {
        byte[i] = '\0';
    }
}

static nn2_status_t read_line(char *buf, size_t cap, uint32_t timeout_ms, int mask)
{
    int64_t deadline;
    size_t used;
    int overflow;
    int fd;

    if (buf == NULL || cap < 2u) {
        return NN2_ERR_INVALID_ARG;
    }
    wipe_buf(buf, cap);
    used = 0u;
    overflow = 0;
    fd = fileno(stdin);
    if (fd < 0) {
        return NN2_ERR_IO;
    }
    deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);
    for (;;) {
        int64_t now;
        int64_t remain_us;
        struct timeval tv;
        fd_set rfds;
        int selected;
        char ch;
        ssize_t nread;

        now = esp_timer_get_time();
        if (now >= deadline) {
            wipe_buf(buf, cap);
            return NN2_ERR_TIMEOUT;
        }
        remain_us = deadline - now;
        if (remain_us < 1) {
            remain_us = 1;
        }
        tv.tv_sec = (time_t)(remain_us / 1000000);
        tv.tv_usec = (suseconds_t)(remain_us % 1000000);
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        selected = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (selected < 0) {
            if (errno == EINTR) {
                continue;
            }
            wipe_buf(buf, cap);
            return NN2_ERR_IO;
        }
        if (selected == 0) {
            wipe_buf(buf, cap);
            return NN2_ERR_TIMEOUT;
        }
        nread = read(fd, &ch, 1);
        if (nread < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            wipe_buf(buf, cap);
            return NN2_ERR_IO;
        }
        if (nread == 0) {
            wipe_buf(buf, cap);
            return NN2_ERR_IO;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (overflow) {
                wipe_buf(buf, cap);
                return NN2_ERR_INVALID;
            }
            buf[used] = '\0';
            return NN2_OK;
        }
        if (!overflow && (ch == '\b' || ch == 0x7f)) {
            if (used > 0u) {
                used--;
                buf[used] = '\0';
            }
            continue;
        }
        if (used + 1u >= cap) {
            overflow = 1;
            continue;
        }
        buf[used] = ch;
        used++;
        if (mask) {
            fputc('*', stdout);
        } else {
            fputc((unsigned char)ch, stdout);
        }
        fflush(stdout);
    }
}

static nn2_status_t ad_read_once(void *ctx, nn2_wifi_credentials_t *out, uint32_t timeout_ms)
{
    nn2_wifi_credentials_t cred;
    nn2_status_t status;

    (void)ctx;
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    nn2_wifi_cred_wipe(&cred);
    fputs("\r\nSSID: ", stdout);
    fflush(stdout);
    status = read_line(cred.ssid, sizeof(cred.ssid), timeout_ms, 0);
    if (status != NN2_OK) {
        nn2_wifi_cred_wipe(&cred);
        ESP_LOGW(TAG, "ssid line failed: %d", (int)status);
        return status;
    }
    cred.ssid_len = (uint8_t)strlen(cred.ssid);
    fputs("\r\nPassword: ", stdout);
    fflush(stdout);
    status = read_line(cred.password, sizeof(cred.password), timeout_ms, 1);
    fputs("\r\n", stdout);
    fflush(stdout);
    if (status != NN2_OK) {
        nn2_wifi_cred_wipe(&cred);
        ESP_LOGW(TAG, "password line failed: %d", (int)status);
        return status;
    }
    cred.password_len = (uint8_t)strlen(cred.password);
    status = nn2_wifi_cred_validate(&cred);
    if (status != NN2_OK) {
        nn2_wifi_cred_wipe(&cred);
        ESP_LOGW(TAG, "provision input rejected");
        return status;
    }
    *out = cred;
    nn2_wifi_cred_wipe(&cred);
    ESP_LOGI(TAG, "provision input accepted");
    return NN2_OK;
}

nn2_status_t nn2_ad_prov_init(nn2_port_prov_console_t *out)
{
    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    out->read_once = ad_read_once;
    out->ctx = NULL;
    return NN2_OK;
}
