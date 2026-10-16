// ============================================================
// File: components/nocti_ble/nocti_ble.c
//
// Revisi fokus (stability untuk Web Bluetooth):
// 1) Gate stream TX sampai MTU runtime siap (>= 247)
// 2) Fix potensi mbuf leak saat notify gagal
// 3) Backoff ENOMEM lebih stabil
// 4) Coalescing target lebih kecil agar burst tidak bikin queue penuh
//
// slash:
// /belajar: drop_fnirs naik biasanya karena stream ring buffer overflow
// /belajar: overflow sering dipicu stall sesaat di central (Web Bluetooth)
// /belajar: mbuf leak dan retry agresif bikin stall jadi makin parah
// ============================================================

#include "nocti_ble.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nocti_log.h"
#include "nocti_stream.h"

#include "nvs_flash.h"

// NimBLE (ESP-IDF)
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include "host/ble_gatt.h"

static const char *TAG = "nocti_ble";

// ------------------------------------------------------------
// ble_store_config_init dipakai banyak contoh NimBLE
// slash: deklarasi saja cukup, implementasi ada di util NimBLE pada ESP-IDF
// ------------------------------------------------------------
void ble_store_config_init(void);

// ------------------------------------------------------------
// UUID custom NoctiNIRS (128-bit)
// slash:
// /belajar: BLE_UUID128_INIT memakai urutan little endian byte
// /belajar: byte ke 7 diset menjadi 0x01,0x02,0x03 untuk SVC,CMD,DATA
// ------------------------------------------------------------

static const ble_uuid128_t UUID_NOCTI_SVC =
    BLE_UUID128_INIT(0xa5,0x00,0x00,0x00,0x00,0x00,0x00,0x01, 0x53,0x52,0x49,0x4e,0x69,0x74,0x63,0x6f);

static const ble_uuid128_t UUID_NOCTI_CMD =
    BLE_UUID128_INIT(0xa5,0x00,0x00,0x00,0x00,0x00,0x00,0x02, 0x53,0x52,0x49,0x4e,0x69,0x74,0x63,0x6f);

static const ble_uuid128_t UUID_NOCTI_DATA =
    BLE_UUID128_INIT(0xa5,0x00,0x00,0x00,0x00,0x00,0x00,0x03, 0x53,0x52,0x49,0x4e,0x69,0x74,0x63,0x6f);

// ============================================================
// Internal state
// ============================================================

static nocti_ble_t *s_self = NULL;

static uint16_t s_cmd_val_handle  = 0;
static uint16_t s_data_val_handle = 0;

static bool s_adv_requested = false;
static bool s_adv_running   = false;

// slash: MTU runtime hasil negosiasi, awalnya 23 lalu naik saat MTU event datang
static volatile uint16_t s_mtu_runtime = 23;
#define NOCTI_BLE_STREAM_MTU_READY   (247u)

// slash: debug kesehatan notify
static volatile uint32_t s_notify_ok = 0;
static volatile uint32_t s_notify_enomem = 0;
static volatile uint32_t s_notify_fail = 0;

// ============================================================
// Helpers
// ============================================================

static inline uint16_t u16_le_read(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint16_t att_payload_max_from_mtu(uint16_t mtu)
{
    // slash: ATT notify payload max = mtu - 3
    if (mtu <= 3) return 0;
    return (uint16_t)(mtu - 3);
}

// ============================================================
// GATT access callback
// ============================================================

static int gatt_access_cb(uint16_t conn_handle,
                          uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    (void)arg;
    (void)conn_handle;

    if (!s_self) return BLE_ATT_ERR_UNLIKELY;

    // CMD characteristic (write only)
    if (attr_handle == s_cmd_val_handle) {

        if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }

        size_t len = (size_t)OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0) return 0;

        if (len > NOCTI_BLE_MAX_CMD_BYTES) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        int rc = os_mbuf_copydata(ctxt->om, 0, len, s_self->cmd_buf);
        if (rc != 0) return BLE_ATT_ERR_UNLIKELY;

        if (s_self->cfg.cmd_cb) {
            s_self->cfg.cmd_cb(s_self->cmd_buf, len, s_self->cfg.cmd_cb_ctx);
        }
        return 0;
    }

    // DATA characteristic (notify only)
    if (attr_handle == s_data_val_handle) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// ============================================================
// GATT service table
// ============================================================

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_NOCTI_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &UUID_NOCTI_CMD.u,
                .access_cb = gatt_access_cb,
                .val_handle = &s_cmd_val_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &UUID_NOCTI_DATA.u,
                .access_cb = gatt_access_cb,
                .val_handle = &s_data_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    { 0 }
};

// ============================================================
// Advertising
// ============================================================

static void start_advertising_internal(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    nocti_ble_t *b = s_self;
    if (!b) return 0;

    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT: {

        s_adv_running = false;

        if (event->connect.status == 0) {

            b->connected = true;
            b->conn_handle = event->connect.conn_handle;
            b->notify_enabled = false;

            // slash: reset state MTU dan counter saat connect baru
            s_mtu_runtime = clamp_u16(ble_att_mtu(b->conn_handle), 23, 517);
            s_notify_ok = 0;
            s_notify_enomem = 0;
            s_notify_fail = 0;

            NOCTI_LOGI(TAG, "connected, handle=%u", (unsigned)b->conn_handle);

            // slash: info runtime
            NOCTI_LOGI(TAG, "mtu runtime now=%u", (unsigned)s_mtu_runtime);

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(b->conn_handle, &desc) == 0) {
                NOCTI_LOGI(TAG, "conn itvl=%u latency=%u timeout=%u",
                           (unsigned)desc.conn_itvl,
                           (unsigned)desc.conn_latency,
                           (unsigned)desc.supervision_timeout);
            }

            if (b->cfg.conn_cb) {
                b->cfg.conn_cb(true, b->conn_handle, b->cfg.conn_cb_ctx);
            }

        } else {
            NOCTI_LOGW(TAG, "connect failed, status=%d", event->connect.status);
            b->connected = false;
            b->notify_enabled = false;
            b->conn_handle = 0;
            start_advertising_internal();
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {

        NOCTI_LOGI(TAG, "disconnect, reason=%d", event->disconnect.reason);

        b->connected = false;
        b->notify_enabled = false;
        b->conn_handle = 0;

        // slash: reset MTU state
        s_mtu_runtime = 23;

        if (b->cfg.conn_cb) {
            b->cfg.conn_cb(false, 0, b->cfg.conn_cb_ctx);
        }

        start_advertising_internal();
        return 0;
    }

    case BLE_GAP_EVENT_SUBSCRIBE: {

        if (event->subscribe.attr_handle == s_data_val_handle) {

            b->notify_enabled = (event->subscribe.cur_notify != 0);
            NOCTI_LOGI(TAG, "subscribe DATA notify=%d", (int)b->notify_enabled);

            // slash: reset counter saat mulai notify untuk observability
            if (b->notify_enabled) {
                s_notify_ok = 0;
                s_notify_enomem = 0;
                s_notify_fail = 0;
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_MTU: {

        s_mtu_runtime = clamp_u16(event->mtu.value, 23, 517);
        NOCTI_LOGI(TAG, "mtu updated, mtu=%u", (unsigned)s_mtu_runtime);
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE: {

        s_adv_running = false;
        if (s_adv_requested) {
            start_advertising_internal();
        }
        return 0;
    }

    default:
        return 0;
    }
}

static void start_advertising_internal(void)
{
    nocti_ble_t *b = s_self;
    if (!b) return;

    if (!s_adv_requested) return;

    if (!ble_hs_synced()) {
        NOCTI_LOGI(TAG, "adv postponed, host not synced yet");
        return;
    }

    if (b->connected) return;
    if (s_adv_running) return;

    struct ble_hs_adv_fields adv;
    memset(&adv, 0, sizeof(adv));

    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    if (b->cfg.adv_include_name) {
        const char *name = ble_svc_gap_device_name();
        size_t nlen = strlen(name);

        if (nlen <= 18) {
            adv.name = (uint8_t *)name;
            adv.name_len = (uint8_t)nlen;
            adv.name_is_complete = 1;
        } else {
            adv.name = (uint8_t *)name;
            adv.name_len = 18;
            adv.name_is_complete = 0;
        }
    }

    int rc = ble_gap_adv_set_fields(&adv);
    if (rc != 0) {
        NOCTI_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));

    rsp.uuids128 = (ble_uuid128_t *)&UUID_NOCTI_SVC;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        NOCTI_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = b->cfg.adv_itvl_min;
    adv_params.itvl_max = b->cfg.adv_itvl_max;

    rc = ble_gap_adv_start(
        b->addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        gap_event_cb,
        NULL
    );

    if (rc == 0) {
        s_adv_running = true;
        NOCTI_LOGI(TAG, "advertising started");
        return;
    }

    if (rc == BLE_HS_EALREADY) {
        s_adv_running = true;
        NOCTI_LOGI(TAG, "advertising already running");
        return;
    }

    NOCTI_LOGE(TAG, "adv_start rc=%d", rc);
}

// ============================================================
// NimBLE host lifecycle
// ============================================================

static void on_reset(int reason)
{
    NOCTI_LOGW(TAG, "reset reason=%d", reason);
}

static void on_sync(void)
{
    nocti_ble_t *b = s_self;
    if (!b) return;

    int rc = ble_hs_id_infer_auto(0, &b->addr_type);
    if (rc != 0) {
        NOCTI_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
        return;
    }

    start_advertising_internal();
}

static void nimble_host_task(void *param)
{
    (void)param;

    // Returns only after nimble_port_stop() is called.
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============================================================
// Notify helpers
// ============================================================

static esp_err_t notify_one(const uint8_t *data, size_t len)
{
    // slash:
    // /belajar: notify_one hanya untuk 1 ATT notification payload
    // /belajar: ble_gatts_notify_custom() selalu mengambil ownership mbuf,
    //           baik saat berhasil maupun gagal. Jangan free om setelah call.
    nocti_ble_t *b = s_self;
    if (!b || !b->connected || !b->notify_enabled) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    uint16_t mtu = clamp_u16(ble_att_mtu(b->conn_handle), 23, 517);
    uint16_t maxp = att_payload_max_from_mtu(mtu);

    if (maxp == 0) return ESP_ERR_INVALID_SIZE;
    if (len > (size_t)maxp) return ESP_ERR_INVALID_SIZE;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (!om) {
        s_notify_enomem++;
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(b->conn_handle, s_data_val_handle, om);

    if (rc == 0) {
        s_notify_ok++;
        return ESP_OK;
    }

    // NimBLE sudah mengonsumsi om meskipun rc != 0.
    if (rc == BLE_HS_ENOTCONN) {
        s_notify_fail++;
        return ESP_ERR_INVALID_STATE;
    }
    if (rc == BLE_HS_ENOMEM) {
        s_notify_enomem++;
        return ESP_ERR_NO_MEM;
    }

    s_notify_fail++;
    return ESP_FAIL;
}

esp_err_t nocti_ble_send_stream_frame(nocti_ble_t *b, const uint8_t *frame, size_t frame_len)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;
    if (!frame || frame_len == 0) return ESP_ERR_INVALID_ARG;
    if (!b->connected || !b->notify_enabled) return ESP_ERR_INVALID_STATE;

    uint16_t mtu = clamp_u16(ble_att_mtu(b->conn_handle), 23, 517);
    size_t att_payload_max = (size_t)att_payload_max_from_mtu(mtu);

    const size_t hdr_sz = sizeof(nocti_ble_frag_hdr_t);
    if (att_payload_max <= hdr_sz) return ESP_ERR_INVALID_SIZE;

    size_t frag_data_max = att_payload_max - hdr_sz;

    size_t frag_cnt_sz = (frame_len + frag_data_max - 1) / frag_data_max;
    if (frag_cnt_sz == 0) frag_cnt_sz = 1;
    if (frag_cnt_sz > 255) return ESP_ERR_INVALID_SIZE;

    uint8_t frag_cnt = (uint8_t)frag_cnt_sz;

    uint16_t frame_seq = 0;
    if (frame_len >= 6 && frame[0] == 0xA5) {
        frame_seq = u16_le_read(&frame[4]);
    }

    uint8_t pkt[520];

    for (uint8_t frag_idx = 0; frag_idx < frag_cnt; frag_idx += 1) {

        size_t off = (size_t)frag_idx * frag_data_max;
        size_t remain = frame_len - off;
        size_t n = (remain < frag_data_max) ? remain : frag_data_max;

        nocti_ble_frag_hdr_t fh;
        fh.frame_seq = frame_seq;
        fh.frame_len = (uint16_t)frame_len;
        fh.frag_idx  = frag_idx;
        fh.frag_cnt  = frag_cnt;
        fh.frag_off  = (uint16_t)off;

        memcpy(&pkt[0], &fh, hdr_sz);
        memcpy(&pkt[hdr_sz], &frame[off], n);

        int tries = 0;
        esp_err_t err = ESP_FAIL;

        while (tries < 6) {
            err = notify_one(pkt, hdr_sz + n);
            if (err == ESP_OK) break;

            if (err == ESP_ERR_NO_MEM) {
                // slash: backoff bertahap agar host dan central sempat drain
                uint32_t d_ms = 5u + (uint32_t)tries * 5u; // 5,10,15,20...
                vTaskDelay(pdMS_TO_TICKS(d_ms));
                tries += 1;
                continue;
            }

            break;
        }

        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

// ============================================================
// TX task (coalescing)
// ============================================================

static void tx_task_fn(void *param)
{
    (void)param;

    nocti_ble_t *b = s_self;
    if (!b) {
        vTaskDelete(NULL);
        return;
    }

    // slash:
    // /belajar: target lebih kecil lebih stabil untuk Web Bluetooth
    #define NOCTI_BLE_COALESCE_MAX_BYTES     (2048u)
    #define NOCTI_BLE_COALESCE_TARGET_BYTES  (800u)
    #define NOCTI_BLE_COALESCE_MAX_FRAMES    (64u)

    uint8_t blob[NOCTI_BLE_COALESCE_MAX_BYTES];
    size_t  blob_len = 0;

    uint8_t frame[NOCTI_STREAM_MAX_FRAME_BYTES];

    uint32_t last_dbg_ms = 0;

    while (s_self && b->inited) {

        if (!b->cfg.enable_stream_tx || !b->cfg.stream) {
            blob_len = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!b->connected || !b->notify_enabled) {
            blob_len = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // slash: debug ringan tiap 10 detik
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((now_ms - last_dbg_ms) >= 10000u) {
            last_dbg_ms = now_ms;
            NOCTI_LOGI(TAG, "tx dbg mtu=%u ok=%u enomem=%u fail=%u",
                       (unsigned)s_mtu_runtime,
                       (unsigned)s_notify_ok,
                       (unsigned)s_notify_enomem,
                       (unsigned)s_notify_fail);
        }

        // 1) Ambil minimal 1 frame dengan timeout normal
        size_t out_len = 0;
        esp_err_t r = nocti_stream_read(
            b->cfg.stream,
            frame,
            sizeof(frame),
            &out_len,
            b->cfg.stream_read_timeout_ms
        );

        if (r == ESP_OK && out_len > 0) {

            // kalau blob kosong tapi frame lebih besar dari blob, kirim frame langsung
            if (blob_len == 0 && out_len > sizeof(blob)) {
                (void)nocti_ble_send_stream_frame(b, frame, out_len);
                continue;
            }

            // kalau tidak muat, flush blob dulu
            if (blob_len > 0 && (blob_len + out_len) > sizeof(blob)) {
                (void)nocti_ble_send_stream_frame(b, blob, blob_len);
                blob_len = 0;
            }

            if ((blob_len + out_len) <= sizeof(blob)) {
                memcpy(&blob[blob_len], frame, out_len);
                blob_len += out_len;
            }

            // 2) Ambil frame tambahan tanpa blocking sampai target tercapai
            uint32_t n_frames = 1;

            while (blob_len < (size_t)NOCTI_BLE_COALESCE_TARGET_BYTES &&
                   n_frames < (uint32_t)NOCTI_BLE_COALESCE_MAX_FRAMES) {

                out_len = 0;
                r = nocti_stream_read(
                    b->cfg.stream,
                    frame,
                    sizeof(frame),
                    &out_len,
                    0
                );

                if (!(r == ESP_OK && out_len > 0)) break;
                if ((blob_len + out_len) > sizeof(blob)) break;

                memcpy(&blob[blob_len], frame, out_len);
                blob_len += out_len;
                n_frames += 1;
            }

            // 3) Kirim blob jika sudah cukup penuh
            if (blob_len >= (size_t)NOCTI_BLE_COALESCE_TARGET_BYTES) {
                (void)nocti_ble_send_stream_frame(b, blob, blob_len);
                blob_len = 0;
            }

            continue;
        }

        if (r == ESP_ERR_TIMEOUT) {
            // slash: flush blob agar latency tidak membesar
            if (blob_len > 0) {
                (void)nocti_ble_send_stream_frame(b, blob, blob_len);
                blob_len = 0;
            }
            continue;
        }

        // error lain
        if (blob_len > 0) {
            (void)nocti_ble_send_stream_frame(b, blob, blob_len);
            blob_len = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

// ============================================================
// Public API
// ============================================================

static esp_err_t nvs_init_safe(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        esp_err_t e2 = nvs_flash_erase();
        if (e2 != ESP_OK) return e2;
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nocti_ble_init(nocti_ble_t *b, const nocti_ble_cfg_t *cfg)
{
    if (!b || !cfg) return ESP_ERR_INVALID_ARG;

    memset(b, 0, sizeof(*b));
    b->cfg = *cfg;

    s_adv_requested = false;
    s_adv_running = false;

    if (!b->cfg.device_name) b->cfg.device_name = "NoctiNIRS";
    if (b->cfg.preferred_mtu == 0) b->cfg.preferred_mtu = 247;

    if (b->cfg.adv_itvl_min == 0) b->cfg.adv_itvl_min = 160;
    if (b->cfg.adv_itvl_max == 0) b->cfg.adv_itvl_max = 240;

    if (b->cfg.tx_task_stack_words == 0) b->cfg.tx_task_stack_words = 4096;
    if (b->cfg.tx_task_prio == 0) b->cfg.tx_task_prio = 6;

    s_self = b;

    esp_err_t err = nvs_init_safe();
    if (err != ESP_OK) return err;

    err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set(b->cfg.device_name);

    uint16_t mtu = clamp_u16(b->cfg.preferred_mtu, 23, 517);
    ble_att_set_preferred_mtu(mtu);

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) return ESP_FAIL;

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) return ESP_FAIL;

    ble_store_config_init();

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Set before tasks start so the TX task cannot exit due to an init race.
    b->inited = true;

    // Use ESP-IDF's NimBLE FreeRTOS launcher so stack size and core affinity
    // follow CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE / PINNED_TO_CORE.
    nimble_port_freertos_init(nimble_host_task);

    if (b->cfg.enable_stream_tx && b->cfg.stream) {
        BaseType_t ok2 = xTaskCreate(
            tx_task_fn,
            "nocti_ble_tx",
            b->cfg.tx_task_stack_words,
            NULL,
            b->cfg.tx_task_prio,
            &b->tx_task
        );
        if (ok2 != pdPASS) {
            b->inited = false;
            (void)nimble_port_stop();
            return ESP_ERR_NO_MEM;
        }
    }

    // slash: init state MTU default
    s_mtu_runtime = 23;

    NOCTI_LOGI(TAG, "ble inited, name=%s, mtu=%u", b->cfg.device_name, (unsigned)mtu);
    return ESP_OK;
}

esp_err_t nocti_ble_start_advertising(nocti_ble_t *b)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;

    s_adv_requested = true;
    start_advertising_internal();
    return ESP_OK;
}

esp_err_t nocti_ble_stop(nocti_ble_t *b)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;

    s_adv_requested = false;
    s_adv_running = false;
    b->inited = false;  // lets the optional TX task terminate cleanly

    (void)ble_gap_adv_stop();

    if (b->connected) {
        (void)ble_gap_terminate(b->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    b->connected = false;
    b->notify_enabled = false;
    b->conn_handle = 0;

    s_mtu_runtime = 23;

    (void)nimble_port_stop();
    return ESP_OK;
}

bool nocti_ble_is_connected(const nocti_ble_t *b)
{
    return (b && b->connected);
}

bool nocti_ble_can_tx_stream(const nocti_ble_t *b)
{
    // slash:
    // /belajar: gate TX sampai MTU runtime sudah siap
    // /belajar: ini mencegah backlog saat awal connect MTU masih 23
    return (b && b->connected && b->notify_enabled && (s_mtu_runtime >= NOCTI_BLE_STREAM_MTU_READY));
}

uint16_t nocti_ble_conn_handle(const nocti_ble_t *b)
{
    return b ? b->conn_handle : 0;
}