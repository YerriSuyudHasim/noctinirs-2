#include "nn2_ad_wifi.h"

#include "nn2_rtos_config.h"
#include "nn2_wifi_backoff.h"
#include "nn2_wifi_limits.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "nn2_wifi";

typedef struct {
    uint8_t kind;
    uint8_t auth_failed;
    uint8_t reason;
    uint32_t ipv4_addr;
} nn2_ad_wifi_evt_t;

static nn2_wifi_backoff_t s_backoff;
static nn2_wifi_sta_status_t s_status;
static QueueHandle_t s_evt_q;
static StaticQueue_t s_evt_q_buf;
static nn2_ad_wifi_evt_t s_evt_q_items[NN2_WIFI_EVT_QUEUE_LEN];
static esp_timer_handle_t s_backoff_timer;
static int s_ready;
static int s_started;
static int s_want_connected;

static nn2_status_t map_esp(esp_err_t err)
{
    if (err == ESP_OK) {
        return NN2_OK;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return NN2_ERR_TIMEOUT;
    }
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_WIFI_SSID) {
        return NN2_ERR_INVALID_ARG;
    }
    if (err == ESP_ERR_NO_MEM) {
        return NN2_ERR_NO_MEM;
    }
    if (err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_STATE) {
        return NN2_ERR_BUSY;
    }
    return NN2_ERR_IO;
}

static void wipe_bytes(void *ptr, size_t len)
{
    volatile uint8_t *byte;
    size_t i;

    byte = (volatile uint8_t *)ptr;
    for (i = 0; i < len; i++) {
        byte[i] = 0u;
    }
}

static int is_auth_failure(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return 1;
    default:
        return 0;
    }
}

static void enqueue_evt(const nn2_ad_wifi_evt_t *evt)
{
    if (s_evt_q == NULL || xQueueSend(s_evt_q, evt, 0) != pdTRUE) {
        s_status.event_overflow_count++;
    }
}

static void backoff_timer_cb(void *arg)
{
    nn2_ad_wifi_evt_t evt;

    (void)arg;
    memset(&evt, 0, sizeof(evt));
    evt.kind = NN2_WIFI_STA_EVT_RECONNECT_DUE;
    /* Timer task: one nonblocking enqueue. No storage commit and no loop. */
    enqueue_evt(&evt);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    nn2_ad_wifi_evt_t evt;

    (void)arg;
    memset(&evt, 0, sizeof(evt));
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = data;

        evt.kind = NN2_WIFI_STA_EVT_LINK_DOWN;
        evt.reason = (disc != NULL) ? (uint8_t)disc->reason : 0u;
        evt.auth_failed = (uint8_t)is_auth_failure(evt.reason);
        s_status.link = NN2_WIFI_LINK_DOWN;
        s_status.ipv4_addr = 0u;
        s_status.disconnect_count++;
        s_status.auth_failed_last = evt.auth_failed != 0u;
        if (evt.auth_failed) {
            s_status.auth_fail_count++;
        }
        ESP_LOGI(TAG, "sta disconnected reason=%u", (unsigned)evt.reason);
        enqueue_evt(&evt);
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;

        evt.kind = NN2_WIFI_STA_EVT_LINK_UP;
        evt.ipv4_addr = (got != NULL) ? got->ip_info.ip.addr : 0u;
        s_status.link = NN2_WIFI_LINK_UP;
        s_status.ipv4_addr = evt.ipv4_addr;
        s_status.auth_failed_last = false;
        nn2_wifi_backoff_reset(&s_backoff);
        if (s_backoff_timer != NULL) {
            (void)esp_timer_stop(s_backoff_timer);
        }
        ESP_LOGI(TAG, "sta link up");
        enqueue_evt(&evt);
    }
}

static nn2_status_t ad_connect(void *ctx, const nn2_wifi_credentials_t *creds)
{
    wifi_config_t cfg;
    nn2_status_t status;
    esp_err_t err;

    (void)ctx;
    if (!s_ready || creds == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    status = nn2_wifi_cred_validate(creds);
    if (status != NN2_OK) {
        return status;
    }
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.sta.ssid, creds->ssid, creds->ssid_len);
    memcpy(cfg.sta.password, creds->password, creds->password_len);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.threshold.rssi = -127;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.failure_retry_cnt = 0u;
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    wipe_bytes(&cfg, sizeof(cfg));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set config failed: %d", (int)err);
        return map_esp(err);
    }
    s_want_connected = 1;
    if (!s_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi start failed: %d", (int)err);
            return map_esp(err);
        }
        s_started = 1;
    }
    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        err = esp_wifi_start();
        if (err == ESP_OK) {
            s_started = 1;
            err = esp_wifi_connect();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect request failed: %d", (int)err);
    } else {
        ESP_LOGI(TAG, "sta connect requested (ssid length %u)", (unsigned)creds->ssid_len);
    }
    return map_esp(err);
}

static nn2_status_t ad_disconnect(void *ctx)
{
    esp_err_t err;

    (void)ctx;
    if (!s_ready) {
        return NN2_ERR_INVALID_ARG;
    }
    s_want_connected = 0;
    if (s_backoff_timer != NULL) {
        (void)esp_timer_stop(s_backoff_timer);
    }
    err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return NN2_OK;
    }
    return map_esp(err);
}

static nn2_status_t ad_get_status(void *ctx, nn2_wifi_sta_status_t *out)
{
    (void)ctx;
    if (!s_ready || out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    *out = s_status;
    return NN2_OK;
}

static nn2_status_t ad_schedule_reconnect(void *ctx)
{
    uint32_t delay_ms;
    esp_err_t err;

    (void)ctx;
    if (!s_ready || s_backoff_timer == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (!s_want_connected) {
        return NN2_OK;
    }
    delay_ms = nn2_wifi_backoff_next_ms(&s_backoff);
    if (delay_ms == 0u) {
        nn2_ad_wifi_evt_t evt;

        /* Immediate retry is posted to nn2_net. esp_wifi_connect is not
           called on this path, and this function is not the Wi-Fi event
           callback. Drop any previously armed backoff so it cannot fire too. */
        (void)esp_timer_stop(s_backoff_timer);
        memset(&evt, 0, sizeof(evt));
        evt.kind = NN2_WIFI_STA_EVT_RECONNECT_DUE;
        if (s_evt_q == NULL || xQueueSend(s_evt_q, &evt, 0) != pdTRUE) {
            s_status.event_overflow_count++;
            return NN2_ERR_OVERFLOW;
        }
        ESP_LOGI(TAG, "sta reconnect posted with no delay");
        return NN2_OK;
    }
    if (delay_ms > NN2_WIFI_BACKOFF_MAX_MS) {
        return NN2_ERR_INVALID;
    }
    (void)esp_timer_stop(s_backoff_timer);
    err = esp_timer_start_once(s_backoff_timer, (uint64_t)delay_ms * 1000ull);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backoff arm failed: %d", (int)err);
        return map_esp(err);
    }
    ESP_LOGI(TAG, "sta reconnect armed for %u ms", (unsigned)delay_ms);
    return NN2_OK;
}

static nn2_status_t ad_reconnect_now(void *ctx)
{
    esp_err_t err;

    (void)ctx;
    if (!s_ready) {
        return NN2_ERR_INVALID_ARG;
    }
    if (!s_want_connected || s_status.link == NN2_WIFI_LINK_UP) {
        return NN2_OK;
    }
    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        err = esp_wifi_start();
        if (err == ESP_OK) {
            s_started = 1;
            err = esp_wifi_connect();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reconnect request failed: %d", (int)err);
    }
    return map_esp(err);
}

static nn2_status_t ad_wait_event(void *ctx, nn2_wifi_sta_evt_t *evt, uint32_t timeout_ms)
{
    nn2_ad_wifi_evt_t raw;
    TickType_t ticks;

    (void)ctx;
    if (!s_ready || evt == NULL || s_evt_q == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (timeout_ms > NN2_WIFI_WAIT_CAP_MS) {
        timeout_ms = NN2_WIFI_WAIT_CAP_MS;
    }
    ticks = (timeout_ms == 0u) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0u && ticks == 0) {
        ticks = 1;
    }
    if (xQueueReceive(s_evt_q, &raw, ticks) != pdTRUE) {
        return NN2_ERR_TIMEOUT;
    }
    *evt = (nn2_wifi_sta_evt_t)raw.kind;
    return NN2_OK;
}

nn2_status_t nn2_ad_wifi_init(nn2_port_wifi_station_t *out)
{
    esp_err_t err;
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_timer_create_args_t timer_args = {
        .callback = &backoff_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "nn2_wifi_bo",
        .skip_unhandled_events = true,
    };

    if (out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return NN2_ERR_BUSY;
    }
    if (nn2_wifi_backoff_init(&s_backoff,
                              NN2_WIFI_BACKOFF_FIRST_MS,
                              NN2_WIFI_BACKOFF_MAX_MS,
                              NN2_WIFI_BACKOFF_FACTOR) != NN2_OK) {
        return NN2_ERR_INVALID;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.link = NN2_WIFI_LINK_DOWN;
    s_evt_q = xQueueCreateStatic(NN2_WIFI_EVT_QUEUE_LEN,
                                 sizeof(nn2_ad_wifi_evt_t),
                                 (uint8_t *)s_evt_q_items,
                                 &s_evt_q_buf);
    if (s_evt_q == NULL) {
        return NN2_ERR_NO_MEM;
    }
    err = esp_timer_create(&timer_args, &s_backoff_timer);
    if (err != ESP_OK) {
        return map_esp(err);
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return map_esp(err);
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return map_esp(err);
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return NN2_ERR_NO_MEM;
    }
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %d", (int)err);
        return map_esp(err);
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return map_esp(err);
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return map_esp(err);
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        return map_esp(err);
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        return map_esp(err);
    }
    s_ready = 1;
    out->connect = ad_connect;
    out->disconnect = ad_disconnect;
    out->get_status = ad_get_status;
    out->schedule_reconnect = ad_schedule_reconnect;
    out->reconnect_now = ad_reconnect_now;
    out->wait_event = ad_wait_event;
    out->ctx = NULL;
    ESP_LOGI(TAG, "sta adapter ready");
    return NN2_OK;
}
