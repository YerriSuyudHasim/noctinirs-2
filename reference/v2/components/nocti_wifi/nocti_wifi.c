// ============================================================
// File: components/nocti_wifi/nocti_wifi.c
// ============================================================
//
// Implementasi SoftAP + UDP control + UDP stream
//
// slash:
// /belajar: ctrl socket bind port 3333 untuk recvfrom
// /belajar: socket CTRL yang bind ke 3333 dipakai untuk RX, reply, stream, dan advert
// /belajar: streaming_enabled dipicu oleh START dan STOP
// ============================================================

#include "nocti_wifi.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
// slash: sendmsg butuh iovec
#include <sys/uio.h>

#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "nocti_log.h"
#include "nocti_timebase.h"
#include "nocti_crc.h"
#include "nocti_stream.h"

static const char *TAG = "nocti_wifi";

#define EV_AP_STARTED     (1u << 0)
#define EV_STA_CONNECTED  (1u << 1)

static inline uint32_t ip4_host_from_inaddr(uint32_t inaddr_net_order)
{
    return ntohl(inaddr_net_order);
}

static inline uint32_t ip4_net_from_host(uint32_t ip4_host)
{
    return htonl(ip4_host);
}

static inline bool starts_with(const uint8_t *p, size_t n, const char *s)
{
    size_t m = strlen(s);
    if (n < m) return false;
    return (memcmp(p, s, m) == 0);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);

static void ctrl_task_entry(void *arg);
static void adv_task_entry(void *arg);
static void tx_task_entry(void *arg);

void nocti_wifi_cfg_set_defaults(nocti_wifi_cfg_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    cfg->ap_ssid = "REFERENCE_WIFI_SSID";
    cfg->ap_pass = "REFERENCE_WIFI_PASSWORD";
    cfg->ap_channel = 6;
    cfg->ap_max_conn = 2;

    cfg->use_custom_ip = false;
    cfg->ap_ip = ip4_host_from_inaddr(inet_addr("192.168.4.1"));
    cfg->ap_gw = ip4_host_from_inaddr(inet_addr("192.168.4.1"));
    cfg->ap_netmask = ip4_host_from_inaddr(inet_addr("255.255.255.0"));

    cfg->udp_ctrl_port = 3333;
    cfg->udp_stream_port = 3334;

    cfg->enable_builtin_cmd = true;

    cfg->enable_advert = true;
    cfg->advert_period_ms = 1000;

    cfg->stream = NULL;
    cfg->enable_stream_tx = false;
    cfg->stream_read_timeout_ms = 10;

    cfg->use_udp_header = true;
    cfg->reset_session_on_start = true;

    cfg->ctrl_task_stack_words = 8192;
    cfg->ctrl_task_prio = 6;

    cfg->adv_task_stack_words = 3072;
    cfg->adv_task_prio = 4;

    cfg->tx_task_stack_words = 4096;
    cfg->tx_task_prio = 7;
}

static esp_err_t set_softap_ip_if_needed(nocti_wifi_t *w, esp_netif_t *netif)
{
    if (!w->cfg.use_custom_ip) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));

    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = ip4_net_from_host(w->cfg.ap_ip);
    ip.gw.addr = ip4_net_from_host(w->cfg.ap_gw);
    ip.netmask.addr = ip4_net_from_host(w->cfg.ap_netmask);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
    return ESP_OK;
}

static void compute_broadcast_ip(esp_netif_t *netif, uint32_t *out_bcast_host)
{
    if (!out_bcast_host) return;

    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) {
        *out_bcast_host = ip4_host_from_inaddr(inet_addr("192.168.4.255"));
        return;
    }

    uint32_t ip_host = ntohl(ip.ip.addr);
    uint32_t nm_host = ntohl(ip.netmask.addr);

    uint32_t bcast_host = (ip_host & nm_host) | (~nm_host);
    *out_bcast_host = bcast_host;
}

static esp_err_t wifi_softap_start(nocti_wifi_t *w)
{
    // slash: NVS diperlukan oleh WiFi internal
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) return ESP_FAIL;
    w->ap_netif = ap_netif;

    ret = set_softap_ip_if_needed(w, ap_netif);
    if (ret != ESP_OK) return ret;

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&icfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, w));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, w));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, w->cfg.ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(w->cfg.ap_ssid);

    if (w->cfg.ap_pass && strlen(w->cfg.ap_pass) > 0) {
        strncpy((char *)cfg.ap.password, w->cfg.ap_pass, sizeof(cfg.ap.password));
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.password[0] = 0;
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    cfg.ap.channel = w->cfg.ap_channel;
    cfg.ap.max_connection = w->cfg.ap_max_conn;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip = {0};
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip));

    NOCTI_LOGI(TAG, "SoftAP SSID: %s", w->cfg.ap_ssid);
    NOCTI_LOGI(TAG, "SoftAP PASS: %s", (w->cfg.ap_pass && strlen(w->cfg.ap_pass)) ? w->cfg.ap_pass : "(open)");
    NOCTI_LOGI(TAG, "SoftAP channel: %u", (unsigned)w->cfg.ap_channel);
    NOCTI_LOGI(TAG, "AP IP: " IPSTR, IP2STR(&ip.ip));
    NOCTI_LOGI(TAG, "AP GW: " IPSTR, IP2STR(&ip.gw));
    NOCTI_LOGI(TAG, "AP NM: " IPSTR, IP2STR(&ip.netmask));

    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    nocti_wifi_t *w = (nocti_wifi_t *)arg;
    if (!w) return;

    EventGroupHandle_t ev = (EventGroupHandle_t)w->ev;

    if (base == WIFI_EVENT) {

        if (id == WIFI_EVENT_AP_START) {
            w->ap_started = true;
            xEventGroupSetBits(ev, EV_AP_STARTED);
            NOCTI_LOGI(TAG, "SoftAP started");
        }

        if (id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
            w->sta_connected = true;
            xEventGroupSetBits(ev, EV_STA_CONNECTED);
            NOCTI_LOGI(TAG, "STA connected aid=%d", (int)e->aid);
        }

        if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
            w->sta_connected = false;
            xEventGroupClearBits(ev, EV_STA_CONNECTED);

            if (w->mutex) {
                SemaphoreHandle_t mutex = (SemaphoreHandle_t)w->mutex;
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
                    w->sta_ip4 = 0u;
                    w->has_target = false;
                    w->streaming_enabled = false;
                    memset(&w->target, 0, sizeof(w->target));
                    xSemaphoreGive(mutex);
                }
            }

            NOCTI_LOGI(TAG, "STA disconnected aid=%d reason=%d; UDP target cleared",
                       (int)e->aid,
                       (int)e->reason);
        }
    }

    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *e = (ip_event_ap_staipassigned_t *)data;
        if (w->mutex) {
            SemaphoreHandle_t mutex = (SemaphoreHandle_t)w->mutex;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
                w->sta_ip4 = ntohl(e->ip.addr);
                xSemaphoreGive(mutex);
            }
        }
        NOCTI_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip));
    }
}

static esp_err_t set_target_locked(nocti_wifi_t *w, const nocti_wifi_peer_t *peer)
{
    w->target = *peer;
    w->has_target = true;

    NOCTI_LOGI(TAG, "Target set to %u.%u.%u.%u",
               (unsigned)((peer->ip4 >> 24) & 0xFF),
               (unsigned)((peer->ip4 >> 16) & 0xFF),
               (unsigned)((peer->ip4 >> 8) & 0xFF),
               (unsigned)((peer->ip4 >> 0) & 0xFF));
    return ESP_OK;
}

static void handle_builtin_cmd(nocti_wifi_t *w,
                               const uint8_t *rx,
                               size_t n,
                               const nocti_wifi_peer_t *from)
{
    if (!w->cfg.enable_builtin_cmd) return;

    const char *resp = "NOCTI ACK\n";

    SemaphoreHandle_t m = (SemaphoreHandle_t)w->mutex;
    xSemaphoreTake(m, portMAX_DELAY);

    if (starts_with(rx, n, "PING")) {
        resp = "PONG\n";
    } else if (starts_with(rx, n, "HELLO")) {
        (void)set_target_locked(w, from);
        resp = "NOCTI HELLO OK\n";
    } else if (starts_with(rx, n, "INFO")) {
        resp = "NOCTI INFO ctrl=3333 stream=3334\n";
    } else if (starts_with(rx, n, "START")) {
        // slash: START mengunci target dan mengaktifkan streaming
        // slash: session_id naik agar receiver bisa memisahkan sesi
        (void)set_target_locked(w, from);

        if (w->cfg.reset_session_on_start) {
            w->session_id += 1u;
            w->stream_seq = 0u;
        }

        w->streaming_enabled = true;
        resp = "START_OK\n";
    } else if (starts_with(rx, n, "STOP")) {
        w->streaming_enabled = false;
        resp = "STOP_OK\n";
    }

    xSemaphoreGive(m);

    (void)nocti_wifi_send_ctrl_to(w, from, resp, strlen(resp));
}

static int outbound_socket(const nocti_wifi_t *w)
{
    if (!w) return -1;
    if (w->sock_ctrl >= 0) return w->sock_ctrl;
    return w->sock_tx;
}

static void log_socket_failure(const char *operation)
{
    NOCTI_LOGW(TAG, "%s failed errno=%d (%s)",
               operation ? operation : "socket operation",
               errno,
               strerror(errno));
}

esp_err_t nocti_wifi_send_ctrl_to(nocti_wifi_t *w,
                                 const nocti_wifi_peer_t *to,
                                 const void *data,
                                 size_t len)
{
    if (!w || !to || !data || len == 0) return ESP_ERR_INVALID_ARG;
    const int sock = outbound_socket(w);
    if (sock < 0) return ESP_ERR_INVALID_STATE;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(to->port);
    dst.sin_addr.s_addr = ip4_net_from_host(to->ip4);

    const int r = sendto(sock, data, (int)len, 0,
                         (struct sockaddr *)&dst, sizeof(dst));
    if (r < 0) {
        log_socket_failure("control sendto");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t send_stream_to_target(nocti_wifi_t *w, const void *data, size_t len)
{
    if (!w || !data || len == 0) return ESP_ERR_INVALID_ARG;
    const int sock = outbound_socket(w);
    if (sock < 0 || !w->mutex) return ESP_ERR_INVALID_STATE;

    nocti_wifi_peer_t target = {0};
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)w->mutex;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool has_target = w->has_target;
    target = w->target;
    xSemaphoreGive(mutex);
    if (!has_target) return ESP_ERR_INVALID_STATE;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(w->cfg.udp_stream_port);
    dst.sin_addr.s_addr = ip4_net_from_host(target.ip4);

    const int r = sendto(sock, data, (int)len, 0,
                         (struct sockaddr *)&dst, sizeof(dst));
    if (r < 0) {
        log_socket_failure("stream sendto");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// slash: versi iovec agar tidak perlu memcpy payload
static esp_err_t send_stream_to_target_iov(nocti_wifi_t *w,
                                           const struct iovec *iov,
                                           int iovcnt)
{
    if (!w || !iov || iovcnt <= 0) return ESP_ERR_INVALID_ARG;
    const int sock = outbound_socket(w);
    if (sock < 0 || !w->mutex) return ESP_ERR_INVALID_STATE;

    nocti_wifi_peer_t target = {0};
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)w->mutex;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool has_target = w->has_target;
    target = w->target;
    xSemaphoreGive(mutex);
    if (!has_target) return ESP_ERR_INVALID_STATE;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(w->cfg.udp_stream_port);
    dst.sin_addr.s_addr = ip4_net_from_host(target.ip4);

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &dst;
    msg.msg_namelen = sizeof(dst);
    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = (size_t)iovcnt;

    const int r = sendmsg(sock, &msg, 0);
    if (r < 0) {
        log_socket_failure("stream sendmsg");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// ============================================================
// UDP header builder v1 (zero copy)
// slash: payload tidak disalin, hanya header yang dibuat
// ============================================================

static void build_udp_stream_hdr_v1(nocti_wifi_t *w,
                                    size_t payload_len,
                                    nocti_udp_hdr_v1_t *out_hdr)
{
    // slash: asumsi dipanggil saat stream aktif, payload_len sudah valid
    SemaphoreHandle_t m = (SemaphoreHandle_t)w->mutex;
    xSemaphoreTake(m, portMAX_DELAY);

    w->stream_seq += 1u;
    uint32_t seq = w->stream_seq;
    uint32_t session_id = w->session_id;

    xSemaphoreGive(m);

    memset(out_hdr, 0, sizeof(*out_hdr));
    out_hdr->magic = NOCTI_UDP_MAGIC;
    out_hdr->ver = (uint8_t)NOCTI_UDP_VER_V1;
    out_hdr->type = (uint8_t)NOCTI_UDP_TYPE_STREAM;
    out_hdr->flags = 0;

    out_hdr->session_id = session_id;
    out_hdr->seq = seq;
    out_hdr->timestamp_us = (uint64_t)nocti_timebase_now_us();

    out_hdr->payload_len = (uint16_t)payload_len;
    out_hdr->crc16 = 0;
}

static void ctrl_task_entry(void *arg)
{
    nocti_wifi_t *w = (nocti_wifi_t *)arg;
    EventGroupHandle_t ev = (EventGroupHandle_t)w->ev;

    xEventGroupWaitBits(ev, EV_AP_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        NOCTI_LOGE(TAG, "ctrl socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(w->cfg.udp_ctrl_port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        NOCTI_LOGE(TAG, "ctrl bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    int enable_broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   &enable_broadcast, sizeof(enable_broadcast)) != 0) {
        log_socket_failure("ctrl SO_BROADCAST");
    }

    w->sock_ctrl = sock;
    NOCTI_LOGI(TAG, "UDP CTRL listening on port %u", (unsigned)w->cfg.udp_ctrl_port);

    while (!w->stop_req) {

        uint8_t rx[256];
        struct sockaddr_in from_addr = {0};
        socklen_t from_len = sizeof(from_addr);

        int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&from_addr, &from_len);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        nocti_wifi_peer_t from = {0};
        from.ip4 = ntohl(from_addr.sin_addr.s_addr);
        from.port = ntohs(from_addr.sin_port);

        const int printable_len = (n > 96) ? 96 : n;
        NOCTI_LOGI(TAG,
                   "UDP RX %u.%u.%u.%u:%u bytes=%d cmd=%.*s",
                   (unsigned)((from.ip4 >> 24) & 0xFFu),
                   (unsigned)((from.ip4 >> 16) & 0xFFu),
                   (unsigned)((from.ip4 >> 8) & 0xFFu),
                   (unsigned)(from.ip4 & 0xFFu),
                   (unsigned)from.port,
                   n,
                   printable_len,
                   (const char *)rx);

        handle_builtin_cmd(w, rx, (size_t)n, &from);

        if (w->cfg.cmd_cb) {
            w->cfg.cmd_cb(w, rx, (size_t)n, &from, w->cfg.cmd_cb_ctx);
        }

        NOCTI_LOGI(TAG, "UDP CTRL stack minimum free=%u bytes",
                   (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }

    close(sock);
    w->sock_ctrl = -1;
    vTaskDelete(NULL);
}

static void adv_task_entry(void *arg)
{
    nocti_wifi_t *w = (nocti_wifi_t *)arg;
    EventGroupHandle_t ev = (EventGroupHandle_t)w->ev;

    xEventGroupWaitBits(ev, EV_STA_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);

    uint32_t bcast_ip_host = 0;
    compute_broadcast_ip((esp_netif_t *)w->ap_netif, &bcast_ip_host);

    uint32_t seq = 0;

    while (!w->stop_req) {
        if (!w->cfg.enable_advert) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        seq++;

        char msg[128];
        int m = snprintf(msg, sizeof(msg),
                         "NOCTI ADVERT seq=%lu ctrl=%u stream=%u\n",
                         (unsigned long)seq,
                         (unsigned)w->cfg.udp_ctrl_port,
                         (unsigned)w->cfg.udp_stream_port);

        uint32_t station_ip4 = 0u;
        if (w->mutex) {
            SemaphoreHandle_t mutex = (SemaphoreHandle_t)w->mutex;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
                station_ip4 = w->sta_ip4;
                xSemaphoreGive(mutex);
            }
        }

        struct sockaddr_in dst = {0};
        dst.sin_family = AF_INET;
        // The host bridge listens on the stream/reply port, not the device
        // control port.  Prefer unicast to the DHCP-assigned station IP.
        dst.sin_port = htons(w->cfg.udp_stream_port);
        dst.sin_addr.s_addr = htonl(station_ip4 != 0u ? station_ip4 : bcast_ip_host);

        const int sock = outbound_socket(w);
        if (sock >= 0 && sendto(sock, msg, m, 0,
                                (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            log_socket_failure("advert sendto");
        }
        vTaskDelay(pdMS_TO_TICKS(w->cfg.advert_period_ms));
    }

    vTaskDelete(NULL);
}

static void tx_task_entry(void *arg)
{
    nocti_wifi_t *w = (nocti_wifi_t *)arg;

    // slash: nol-copy dari stream
    // slash: item_ptr menunjuk ke memory internal ringbuffer
    const uint8_t *item_ptr = NULL;
    size_t item_len = 0;
    void *owner = NULL;

    while (!w->stop_req) {

        if (!w->cfg.enable_stream_tx || !w->cfg.stream) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Snapshot cross-task state under the mutex.  This avoids undefined
        // behaviour when the firmware is built with -O2 and another task
        // changes streaming_enabled/has_target.
        bool streaming_enabled = false;
        bool has_target = false;
        if (w->mutex) {
            SemaphoreHandle_t mutex = (SemaphoreHandle_t)w->mutex;
            xSemaphoreTake(mutex, portMAX_DELAY);
            streaming_enabled = w->streaming_enabled;
            has_target = w->has_target;
            xSemaphoreGive(mutex);
        }
        if (!streaming_enabled || !has_target) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // slash: reset token sebelum receive untuk menghindari reuse saat error
        owner = NULL;
        item_ptr = NULL;
        item_len = 0;

        esp_err_t r = nocti_stream_recv_item(w->cfg.stream,
                                             &item_ptr,
                                             &item_len,
                                             &owner,
                                             w->cfg.stream_read_timeout_ms);

        if (r != ESP_OK) continue;

        if (item_len == 0 || !item_ptr) {
            if (owner && item_ptr) {
                nocti_stream_return_item(owner, (void *)item_ptr);
            }
            continue;
        }

        if (w->cfg.use_udp_header) {
            // slash: kirim header dan payload sebagai dua segmen (iovec)
            // slash: ini menghindari memcpy payload besar

            nocti_udp_hdr_v1_t h;
            build_udp_stream_hdr_v1(w, item_len, &h);

            // slash: hitung CRC dari header (crc16=0) lalu payload
            uint16_t crc = nocti_crc16_init();
            crc = nocti_crc16_ccitt_false_update(crc, &h, sizeof(h));
            crc = nocti_crc16_ccitt_false_update(crc, item_ptr, item_len);
            h.crc16 = crc;

            struct iovec iov[2];
            iov[0].iov_base = &h;
            iov[0].iov_len  = sizeof(h);
            iov[1].iov_base = (void *)item_ptr;
            iov[1].iov_len  = item_len;

            (void)send_stream_to_target_iov(w, iov, 2);
        } else {
            // slash: tanpa header UDP, langsung kirim bytes stream
            (void)send_stream_to_target(w, item_ptr, item_len);
        }

        // slash: wajib return item agar ringbuffer tidak leak
        if (owner && item_ptr) {
            nocti_stream_return_item(owner, (void *)item_ptr);
        }
    }

    vTaskDelete(NULL);
}

esp_err_t nocti_wifi_init(nocti_wifi_t *w, const nocti_wifi_cfg_t *cfg)
{
    if (!w || !cfg) return ESP_ERR_INVALID_ARG;
    memset(w, 0, sizeof(*w));
    w->cfg = *cfg;

    w->sock_ctrl = -1;
    w->sock_tx = -1;

    w->session_id = 0u;
    w->stream_seq = 0u;

    w->ev = xEventGroupCreate();
    if (!w->ev) return ESP_ERR_NO_MEM;

    w->mutex = xSemaphoreCreateMutex();
    if (!w->mutex) return ESP_ERR_NO_MEM;

    esp_err_t ret = wifi_softap_start(w);
    if (ret != ESP_OK) return ret;

    int sock_tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_tx < 0) return ESP_FAIL;

    int enable = 1;
    setsockopt(sock_tx, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));
    w->sock_tx = sock_tx;

    TaskHandle_t t = NULL;
    BaseType_t task_result = xTaskCreatePinnedToCore(
        ctrl_task_entry,
        "nocti_udp_ctrl",
        w->cfg.ctrl_task_stack_words,
        w,
        w->cfg.ctrl_task_prio,
        &t,
        0);
    if (task_result != pdPASS || t == NULL) {
        NOCTI_LOGE(TAG, "UDP control task creation failed stack=%u",
                   (unsigned)w->cfg.ctrl_task_stack_words);
        close(w->sock_tx);
        w->sock_tx = -1;
        return ESP_ERR_NO_MEM;
    }
    w->ctrl_task = t;

    t = NULL;
    task_result = xTaskCreatePinnedToCore(
        adv_task_entry,
        "nocti_udp_adv",
        w->cfg.adv_task_stack_words,
        w,
        w->cfg.adv_task_prio,
        &t,
        0);
    if (task_result != pdPASS || t == NULL) {
        NOCTI_LOGE(TAG, "UDP advert task creation failed stack=%u",
                   (unsigned)w->cfg.adv_task_stack_words);
        w->stop_req = true;
        return ESP_ERR_NO_MEM;
    }
    w->adv_task = t;

    if (w->cfg.enable_stream_tx && w->cfg.stream) {
        t = NULL;
        task_result = xTaskCreatePinnedToCore(
            tx_task_entry,
            "nocti_udp_tx",
            w->cfg.tx_task_stack_words,
            w,
            w->cfg.tx_task_prio,
            &t,
            0);
        if (task_result != pdPASS || t == NULL) {
            NOCTI_LOGE(TAG, "UDP TX task creation failed stack=%u",
                       (unsigned)w->cfg.tx_task_stack_words);
            w->stop_req = true;
            return ESP_ERR_NO_MEM;
        }
        w->tx_task = t;
    }

    w->inited = true;

    NOCTI_LOGI(TAG,
               "Ready. CTRL RX/TX source=%u, host reply/stream destination=%u; "
               "tasks ctrl=%u adv=%u tx=%u bytes",
               (unsigned)w->cfg.udp_ctrl_port,
               (unsigned)w->cfg.udp_stream_port,
               (unsigned)w->cfg.ctrl_task_stack_words,
               (unsigned)w->cfg.adv_task_stack_words,
               (unsigned)w->cfg.tx_task_stack_words);

    return ESP_OK;
}

esp_err_t nocti_wifi_stop(nocti_wifi_t *w)
{
    if (!w) return ESP_ERR_INVALID_ARG;
    if (!w->inited) return ESP_OK;

    w->stop_req = true;

    if (w->sock_ctrl >= 0) {
        close(w->sock_ctrl);
        w->sock_ctrl = -1;
    }
    if (w->sock_tx >= 0) {
        close(w->sock_tx);
        w->sock_tx = -1;
    }

    esp_wifi_stop();
    w->inited = false;
    return ESP_OK;
}

bool nocti_wifi_is_sta_connected(const nocti_wifi_t *w)
{
    return w ? w->sta_connected : false;
}

bool nocti_wifi_has_target(const nocti_wifi_t *w)
{
    return w ? w->has_target : false;
}

bool nocti_wifi_is_streaming(const nocti_wifi_t *w)
{
    return w ? w->streaming_enabled : false;
}

uint32_t nocti_wifi_session_id(const nocti_wifi_t *w)
{
    return w ? w->session_id : 0u;
}

uint32_t nocti_wifi_stream_seq(const nocti_wifi_t *w)
{
    return w ? w->stream_seq : 0u;
}