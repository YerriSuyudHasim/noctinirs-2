/* These values come from this component's target_compile_definitions.
   Checked before nn2_wifi_limits.h, which supplies host defaults. */
#if !defined(NN2_WIFI_BACKOFF_FIRST_MS)
#error "NN2_WIFI_BACKOFF_FIRST_MS is required"
#endif
#if !defined(NN2_WIFI_BACKOFF_MAX_MS)
#error "NN2_WIFI_BACKOFF_MAX_MS is required"
#endif
#if !defined(NN2_WIFI_BACKOFF_FACTOR)
#error "NN2_WIFI_BACKOFF_FACTOR is required"
#endif
#if !defined(NN2_WIFI_CONNECT_GUARD_MS)
#error "NN2_WIFI_CONNECT_GUARD_MS is required"
#endif

#include "nn2_ad_wifi.h"

#include "nn2_rtos_config.h"
#include "nn2_wifi_backoff.h"
#include "nn2_wifi_limits.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ESP-IDF headers test unset CONFIG_* and build macros with #if.
   -Werror=undef stays on for this file; those headers are not our code. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#pragma GCC diagnostic pop

static const char *TAG = "nn2_wifi";

/* Queued for nn2_net. Not a public station event. */
#define NN2_AD_EVT_DISCONNECT_REQ 4u

typedef struct {
    uint8_t kind;
    uint8_t auth_failed;
    uint8_t reason;
    uint32_t ipv4_addr;
} nn2_ad_wifi_evt_t;

static nn2_wifi_backoff_t s_backoff;
static nn2_wifi_sta_status_t s_status;
static atomic_uint s_overflow;
static esp_netif_t *s_sta_netif;
static QueueHandle_t s_evt_q;
static StaticQueue_t s_evt_q_buf;
static nn2_ad_wifi_evt_t s_evt_q_items[NN2_WIFI_EVT_QUEUE_LEN];
static esp_timer_handle_t s_backoff_timer;
static int s_ready;
static int s_started;
/* Written only on nn2_net. ad_connect sets it. A queued disconnect clears
   it inside ad_wait_event. ad_disconnect only enqueues. */
static int s_want_connected;
/* Written only on nn2_net. Set when esp_wifi_connect returns OK.
   Cleared on LINK_UP, LINK_DOWN, a queued disconnect, and when
   schedule_reconnect takes the next backoff step. */
static int s_connect_in_flight;
static int64_t s_connect_started_us;

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

static void note_overflow(void)
{
    atomic_fetch_add_explicit(&s_overflow, 1u, memory_order_relaxed);
}

static uint32_t load_overflow(void)
{
    return atomic_load_explicit(&s_overflow, memory_order_relaxed);
}

static void mark_connect_in_flight(void)
{
    s_connect_in_flight = 1;
    s_connect_started_us = esp_timer_get_time();
    s_status.connect_in_flight = true;
    s_status.connect_attempt_ms = 0u;
}

static void clear_connect_in_flight(void)
{
    s_connect_in_flight = 0;
    s_connect_started_us = 0;
    s_status.connect_in_flight = false;
    s_status.connect_attempt_ms = 0u;
}

static uint32_t connect_attempt_ms(void)
{
    int64_t now;
    int64_t elapsed_us;
    uint64_t ms;

    if (!s_connect_in_flight) {
        return 0u;
    }
    now = esp_timer_get_time();
    if (now < s_connect_started_us) {
        return 0u;
    }
    elapsed_us = now - s_connect_started_us;
    ms = (uint64_t)elapsed_us / 1000ull;
    if (ms > 0xffffffffull) {
        return 0xffffffffu;
    }
    return (uint32_t)ms;
}

/* Event and timer callbacks may call this. It only posts. The overflow
   counter is atomic because a full queue cannot carry the report. */
static void enqueue_evt(const nn2_ad_wifi_evt_t *evt)
{
    if (s_evt_q == NULL || xQueueSend(s_evt_q, evt, 0) != pdTRUE) {
        note_overflow();
    }
}

/* nn2_net is the only writer of s_status. Call from that task. */
static void refresh_link_from_driver(void)
{
    wifi_ap_record_t ap;
    esp_netif_ip_info_t ip;
    int associated;
    int have_ip;

    associated = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    have_ip = 0;
    memset(&ip, 0, sizeof(ip));
    if (associated && s_sta_netif != NULL && esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK && ip.ip.addr != 0u) {
        have_ip = 1;
    }
    if (associated && have_ip) {
        s_status.link = NN2_WIFI_LINK_UP;
        s_status.ipv4_addr = ip.ip.addr;
        s_status.auth_failed_last = false;
        return;
    }
    s_status.link = NN2_WIFI_LINK_DOWN;
    s_status.ipv4_addr = 0u;
}

static void apply_event_on_net(const nn2_ad_wifi_evt_t *raw)
{
    if (raw->kind == NN2_WIFI_STA_EVT_LINK_DOWN) {
        s_status.link = NN2_WIFI_LINK_DOWN;
        s_status.ipv4_addr = 0u;
        s_status.disconnect_count++;
        s_status.auth_failed_last = raw->auth_failed != 0u;
        if (raw->auth_failed != 0u) {
            s_status.auth_fail_count++;
        }
        clear_connect_in_flight();
    } else if (raw->kind == NN2_WIFI_STA_EVT_LINK_UP) {
        s_status.link = NN2_WIFI_LINK_UP;
        s_status.ipv4_addr = raw->ipv4_addr;
        s_status.auth_failed_last = false;
        clear_connect_in_flight();
    }
    s_status.event_overflow_count = load_overflow();
}

static void backoff_timer_cb(void *arg)
{
    nn2_ad_wifi_evt_t evt;

    (void)arg;
    memset(&evt, 0, sizeof(evt));
    evt.kind = NN2_WIFI_STA_EVT_RECONNECT_DUE;
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
        enqueue_evt(&evt);
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;

        evt.kind = NN2_WIFI_STA_EVT_LINK_UP;
        evt.ipv4_addr = (got != NULL) ? got->ip_info.ip.addr : 0u;
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
        mark_connect_in_flight();
        ESP_LOGI(TAG, "sta connect requested (ssid length %u)", (unsigned)creds->ssid_len);
    }
    return map_esp(err);
}

/* nn2_net only, from a queued disconnect request. */
static void apply_disconnect_on_net(void)
{
    esp_err_t err;

    s_want_connected = 0;
    clear_connect_in_flight();
    if (s_backoff_timer != NULL) {
        (void)esp_timer_stop(s_backoff_timer);
    }
    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "disconnect request failed: %d", (int)err);
    }
}

static nn2_status_t ad_disconnect(void *ctx)
{
    nn2_ad_wifi_evt_t evt;

    (void)ctx;
    if (!s_ready) {
        return NN2_ERR_INVALID_ARG;
    }
    /* Another task may call this. It must not write s_want_connected. */
    memset(&evt, 0, sizeof(evt));
    evt.kind = NN2_AD_EVT_DISCONNECT_REQ;
    if (s_evt_q == NULL || xQueueSend(s_evt_q, &evt, 0) != pdTRUE) {
        note_overflow();
        return NN2_ERR_OVERFLOW;
    }
    return NN2_OK;
}

static nn2_status_t ad_get_status(void *ctx, nn2_wifi_sta_status_t *out)
{
    (void)ctx;
    if (!s_ready || out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    /* nn2_net only. Refresh the link from the driver so a dropped
       LINK_DOWN or LINK_UP is still visible to the liveness poll. */
    refresh_link_from_driver();
    s_status.event_overflow_count = load_overflow();
    s_status.want_connected = s_want_connected != 0;
    s_status.reconnect_timer_armed = (s_backoff_timer != NULL) && esp_timer_is_active(s_backoff_timer);
    s_status.connect_in_flight = s_connect_in_flight != 0;
    s_status.connect_attempt_ms = connect_attempt_ms();
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
    /* The in-flight attempt is over: either the link went down, or the
       connect guard expired and nn2_net asked for the next backoff step. */
    clear_connect_in_flight();
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
            note_overflow();
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
    refresh_link_from_driver();
    /* LINK_UP already cleared a stale RECONNECT_DUE's reason to connect.
       An attempt that is still in flight stays running until LINK_DOWN
       or the connect guard. */
    if (!s_want_connected || s_status.link == NN2_WIFI_LINK_UP || s_connect_in_flight) {
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
    } else {
        mark_connect_in_flight();
    }
    return map_esp(err);
}

static nn2_status_t ad_wait_event(void *ctx, nn2_wifi_sta_evt_t *evt, uint32_t timeout_ms)
{
    nn2_ad_wifi_evt_t raw;
    TickType_t ticks;
    TickType_t start;
    TickType_t budget;

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
    start = xTaskGetTickCount();
    budget = ticks;
    for (;;) {
        TickType_t wait;

        if (timeout_ms == 0u) {
            wait = 0;
        } else if (budget == 0) {
            return NN2_ERR_TIMEOUT;
        } else {
            wait = budget;
        }
        if (xQueueReceive(s_evt_q, &raw, wait) != pdTRUE) {
            return NN2_ERR_TIMEOUT;
        }
        if (timeout_ms > 0u) {
            TickType_t elapsed;

            elapsed = xTaskGetTickCount() - start;
            budget = (elapsed >= ticks) ? 0 : (ticks - elapsed);
        }
        if (raw.kind == NN2_AD_EVT_DISCONNECT_REQ) {
            apply_disconnect_on_net();
            continue;
        }
        if (raw.kind != NN2_WIFI_STA_EVT_LINK_DOWN &&
            raw.kind != NN2_WIFI_STA_EVT_LINK_UP &&
            raw.kind != NN2_WIFI_STA_EVT_RECONNECT_DUE) {
            continue;
        }
        apply_event_on_net(&raw);
        *evt = (nn2_wifi_sta_evt_t)raw.kind;
        return NN2_OK;
    }
}

static nn2_status_t ad_on_link_up(void *ctx)
{
    (void)ctx;
    if (!s_ready || s_backoff_timer == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    nn2_wifi_backoff_reset(&s_backoff);
    (void)esp_timer_stop(s_backoff_timer);
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
    atomic_store_explicit(&s_overflow, 0u, memory_order_relaxed);
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
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
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
    out->on_link_up = ad_on_link_up;
    out->ctx = NULL;
    ESP_LOGI(TAG, "sta adapter ready");
    return NN2_OK;
}
