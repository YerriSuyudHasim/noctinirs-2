// ============================================================
// File: components/nocti_stream/nocti_stream.c
// Revisi fokus (WiFi UDP friendly, beban MCU rendah):
// 1) Tambah SESSION record untuk boundary sesi
// 2) Tambah API start_session dan clear ringbuffer tanpa copy
// 3) Tambah API recv_item zero-copy untuk transport (WiFi) agar bisa sendmsg tanpa memcpy
// 4) Pertahankan format frame v1 agar modul lain tetap kompatibel
// ============================================================

#include "nocti_stream.h"

#include <string.h>

#include "nocti_log.h"
#include "nocti_timebase.h" // slash: convenience API berbasis timebase
#include "nocti_crc.h"      // slash: CRC16-CCITT-FALSE
#include "esp_heap_caps.h"  // slash: cek heap internal (tanpa PSRAM)

static const char *TAG = "nocti_stream";

// slash: compile time check ukuran header harus sesuai konstanta
typedef char nocti_stream_hdr_size_check[(sizeof(nocti_stream_hdr_t) == NOCTI_STREAM_HDR_BYTES) ? 1 : -1];

static inline TickType_t ms_to_ticks(uint32_t ms)
{
    // slash: 0 ms artinya non-blocking
    return (ms == 0) ? 0 : pdMS_TO_TICKS(ms);
}

// ============================================================
// Wrap-safe time compare (uint32)
// slash: aman untuk waktu relatif 32-bit yang wrap
// ============================================================

static inline bool time_after_eq_u32(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static bool handle_ok(const nocti_stream_t *s)
{
    return (s && s->inited && s->mutex);
}

static esp_err_t lock(nocti_stream_t *s, uint32_t timeout_ms)
{
    if (!s || !s->mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s->mutex, ms_to_ticks(timeout_ms)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

static void unlock(nocti_stream_t *s)
{
    if (!s || !s->mutex) return;
    (void)xSemaphoreGive(s->mutex);
}

static uint32_t rel_us_from_now(const nocti_stream_t *s, nocti_time_us_t now_us)
{
    // slash: t_rel_us dipaksa non-negatif, wrap natural saat cast ke uint32
    nocti_time_us_t rel = now_us - s->t0_us;
    if (rel < 0) rel = 0;
    return (uint32_t)rel;
}

static RingbufHandle_t pick_rb(nocti_stream_t *s, nocti_rec_type_t type)
{
    if (!s) return NULL;

    // slash: ctrl ringbuffer memegang record kecil tapi penting untuk state decoder
    if (type == NOCTI_REC_STATUS ||
        type == NOCTI_REC_EVENT  ||
        type == NOCTI_REC_TIME64 ||
        type == NOCTI_REC_FNIRS_CFG ||
        type == NOCTI_REC_SESSION ||
        type == NOCTI_REC_BIOPOT_CFG) {
        return s->rb_ctrl;
    }

    // slash: fNIRS lama dan fNIRS frame baru sama sama masuk rb_fnirs
    if (type == NOCTI_REC_FNIRS || type == NOCTI_REC_FNIRS_FRAME) {
        return s->rb_fnirs;
    }

    // slash: biopot data rate paling tinggi
    if (type == NOCTI_REC_BIOPOT || type == NOCTI_REC_BIOPOT_FRAME) {
        return s->rb_biopot;
    }

    // slash: default aman, anggap biopot
    return s->rb_biopot;
}

static void incr_drop(nocti_stream_t *s, RingbufHandle_t rb)
{
    if (!s) return;
    if (rb == s->rb_ctrl) s->drop_ctrl++;
    else if (rb == s->rb_fnirs) s->drop_fnirs++;
    else if (rb == s->rb_biopot) s->drop_biopot++;
}

static bool drop_one_oldest_from_rb(RingbufHandle_t rb)
{
    if (!rb) return false;

    // slash: RINGBUF_TYPE_BYTEBUF return pointer ke item contigu
    size_t item_size = 0;
    void *item = xRingbufferReceive(rb, &item_size, 0);
    if (!item) return false;

    vRingbufferReturnItem(rb, item);
    return true;
}

// slash: drop policy prioritas
// slash: kalau perlu ruang, drop biopot dulu, lalu fnirs, baru ctrl
static bool drop_oldest_by_priority(nocti_stream_t *s)
{
    if (!s) return false;

    if (drop_one_oldest_from_rb(s->rb_biopot)) {
        s->drop_biopot++;
        return true;
    }
    if (drop_one_oldest_from_rb(s->rb_fnirs)) {
        s->drop_fnirs++;
        return true;
    }
    if (drop_one_oldest_from_rb(s->rb_ctrl)) {
        s->drop_ctrl++;
        return true;
    }
    return false;
}

static esp_err_t send_with_drop_locked(nocti_stream_t *s,
                                      RingbufHandle_t target_rb,
                                      const uint8_t *frame,
                                      size_t frame_len)
{
    if (!s || !target_rb || !frame || frame_len == 0) return ESP_ERR_INVALID_ARG;

    // slash: coba kirim dulu non-blocking
    if (xRingbufferSend(target_rb, (void *)frame, frame_len, 0) == pdTRUE) {
        return ESP_OK;
    }

    // slash: kalau penuh, drop oldest sesuai prioritas sampai cukup ruang atau buntu
    for (int tries = 0; tries < 8; tries++) {
        if (!drop_oldest_by_priority(s)) break;
        if (xRingbufferSend(target_rb, (void *)frame, frame_len, 0) == pdTRUE) {
            return ESP_OK;
        }
    }

    // slash: masih gagal, drop newest (frame ini)
    incr_drop(s, target_rb);
    return ESP_ERR_NO_MEM;
}

// ============================================================
// Internal: build + send frame (caller holds mutex)
// slash: atomic untuk seq dan schedule TIME64
// ============================================================

static esp_err_t push_locked(nocti_stream_t *s,
                            nocti_rec_type_t type,
                            nocti_time_us_t now_us,
                            const void *payload,
                            size_t payload_len)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    if (type == NOCTI_REC_INVALID) return ESP_ERR_INVALID_ARG;
    if (payload_len > NOCTI_STREAM_MAX_PAYLOAD_BYTES) return ESP_ERR_INVALID_SIZE;
    if (payload_len > 0 && !payload) return ESP_ERR_INVALID_ARG;

    RingbufHandle_t rb = pick_rb(s, type);
    if (!rb) return ESP_ERR_INVALID_STATE;

    // slash: stack frame buffer ukuran maksimum 526 byte (aman untuk task stack normal)
    uint8_t frame[NOCTI_STREAM_MAX_FRAME_BYTES];
    size_t frame_len = 0;

    nocti_stream_hdr_t hdr;
    hdr.magic = NOCTI_STREAM_MAGIC;
    hdr.version = NOCTI_STREAM_VERSION;
    hdr.type = (uint8_t)type;
    hdr.flags = (uint8_t)NOCTI_STREAM_F_HAS_CRC16;
    hdr.seq = s->seq++;
    hdr.t_rel_us = rel_us_from_now(s, now_us);
    hdr.payload_len = (uint16_t)payload_len;

    memcpy(&frame[0], &hdr, sizeof(hdr));
    frame_len = sizeof(hdr);

    if (payload_len) {
        memcpy(&frame[frame_len], payload, payload_len);
        frame_len += payload_len;
    }

    uint16_t crc = nocti_crc16_ccitt_false(frame, frame_len);
    frame[frame_len + 0] = (uint8_t)((crc >> 8) & 0xFFu);
    frame[frame_len + 1] = (uint8_t)(crc & 0xFFu);
    frame_len += 2;

    return send_with_drop_locked(s, rb, frame, frame_len);
}

static esp_err_t push_time64_locked(nocti_stream_t *s, nocti_time_us_t now_us)
{
    // slash: payload TIME64 membawa absolute timestamp untuk re-anchor sesi panjang
    uint8_t p[8];
    uint64_t v = (uint64_t)now_us;

    p[0] = (uint8_t)((v >> 56) & 0xFFu);
    p[1] = (uint8_t)((v >> 48) & 0xFFu);
    p[2] = (uint8_t)((v >> 40) & 0xFFu);
    p[3] = (uint8_t)((v >> 32) & 0xFFu);
    p[4] = (uint8_t)((v >> 24) & 0xFFu);
    p[5] = (uint8_t)((v >> 16) & 0xFFu);
    p[6] = (uint8_t)((v >>  8) & 0xFFu);
    p[7] = (uint8_t)((v >>  0) & 0xFFu);

    return push_locked(s, NOCTI_REC_TIME64, now_us, p, sizeof(p));
}

static esp_err_t push_session_locked(nocti_stream_t *s, nocti_time_us_t now_us, uint32_t session_id)
{
    nocti_payload_session_v1_t p;
    memset(&p, 0, sizeof(p));
    p.version = NOCTI_PAYLOAD_SESSION_V1_VERSION;
    p.session_id = session_id;
    p.t0_us = (uint64_t)now_us;

    return push_locked(s, NOCTI_REC_SESSION, now_us, &p, sizeof(p));
}

// ============================================================
// Internal: cfg validation and RAM guard
// ============================================================

static esp_err_t validate_cfg(const nocti_stream_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    if (cfg->rb_ctrl_bytes == 0 || cfg->rb_fnirs_bytes == 0 || cfg->rb_biopot_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // slash: minimal rb harus bisa menampung beberapa frame
    if (cfg->rb_ctrl_bytes < NOCTI_STREAM_RB_MIN_BYTES) return ESP_ERR_INVALID_SIZE;
    if (cfg->rb_fnirs_bytes < NOCTI_STREAM_RB_MIN_BYTES) return ESP_ERR_INVALID_SIZE;
    if (cfg->rb_biopot_bytes < NOCTI_STREAM_RB_MIN_BYTES) return ESP_ERR_INVALID_SIZE;

    // slash: limit per rb
    if (cfg->rb_ctrl_bytes > NOCTI_STREAM_RB_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    if (cfg->rb_fnirs_bytes > NOCTI_STREAM_RB_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    if (cfg->rb_biopot_bytes > NOCTI_STREAM_RB_MAX_BYTES) return ESP_ERR_INVALID_SIZE;

    // slash: limit total untuk jaga heap internal (tanpa PSRAM)
    size_t total = cfg->rb_ctrl_bytes + cfg->rb_fnirs_bytes + cfg->rb_biopot_bytes;
    if (total > NOCTI_STREAM_RB_TOTAL_MAX_BYTES) return ESP_ERR_INVALID_SIZE;

    return ESP_OK;
}

static void log_heap_snapshot(const char *where)
{
    // slash: MALLOC_CAP_8BIT mewakili heap umum internal
    size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t min8  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    // slash: largest free block penting untuk deteksi fragmentasi
    // /belajar: walau total free besar, alokasi besar bisa gagal jika heap terfragmentasi
    size_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    NOCTI_LOGI(TAG, "heap %s: free=%u bytes, min_free=%u bytes",
              where, (unsigned)free8, (unsigned)min8);
    NOCTI_LOGI(TAG, "heap %s: largest_free_block=%u bytes",
              where, (unsigned)largest8);
}

// ============================================================
// Internal: clear ringbuffer
// ============================================================

static void clear_rb(RingbufHandle_t rb)
{
    if (!rb) return;

    while (1) {
        size_t sz = 0;
        void *item = xRingbufferReceive(rb, &sz, 0);
        if (!item) break;
        vRingbufferReturnItem(rb, item);
    }
}

// ============================================================
// Public
// ============================================================


esp_err_t nocti_stream_init(nocti_stream_t *s, const nocti_stream_cfg_t *cfg, nocti_time_us_t now_us)
{
    if (!s || !cfg) return ESP_ERR_INVALID_ARG;

    // slash: cek konfigurasi agar tidak makan RAM kebanyakan
    esp_err_t v = validate_cfg(cfg);
    if (v != ESP_OK) {
        NOCTI_LOGE(TAG, "cfg invalid: ctrl=%u fnirs=%u biopot=%u (min_rb=%u max_rb=%u total_max=%u)",
                  (unsigned)cfg->rb_ctrl_bytes,
                  (unsigned)cfg->rb_fnirs_bytes,
                  (unsigned)cfg->rb_biopot_bytes,
                  (unsigned)NOCTI_STREAM_RB_MIN_BYTES,
                  (unsigned)NOCTI_STREAM_RB_MAX_BYTES,
                  (unsigned)NOCTI_STREAM_RB_TOTAL_MAX_BYTES);
        return v;
    }

    memset(s, 0, sizeof(*s));

    // slash: snapshot heap sebelum alokasi
    log_heap_snapshot("before_stream_alloc");

    // slash: ring buffer tipe byte buffer supaya frame tersimpan contigu
    // /belajar: alokasi besar butuh block kontigu, jadi bisa gagal jika heap terfragmentasi
    s->rb_ctrl = xRingbufferCreate(cfg->rb_ctrl_bytes, RINGBUF_TYPE_BYTEBUF);
    if (!s->rb_ctrl) {
        log_heap_snapshot("rb_ctrl_alloc_failed");
        (void)nocti_stream_deinit(s);
        NOCTI_LOGE(TAG, "ringbuffer alloc failed: rb_ctrl bytes=%u", (unsigned)cfg->rb_ctrl_bytes);
        return ESP_ERR_NO_MEM;
    }

    s->rb_fnirs = xRingbufferCreate(cfg->rb_fnirs_bytes, RINGBUF_TYPE_BYTEBUF);
    if (!s->rb_fnirs) {
        log_heap_snapshot("rb_fnirs_alloc_failed");
        (void)nocti_stream_deinit(s);
        NOCTI_LOGE(TAG, "ringbuffer alloc failed: rb_fnirs bytes=%u", (unsigned)cfg->rb_fnirs_bytes);
        return ESP_ERR_NO_MEM;
    }

    s->rb_biopot = xRingbufferCreate(cfg->rb_biopot_bytes, RINGBUF_TYPE_BYTEBUF);
    if (!s->rb_biopot) {
        log_heap_snapshot("rb_biopot_alloc_failed");
        (void)nocti_stream_deinit(s);
        NOCTI_LOGE(TAG, "ringbuffer alloc failed: rb_biopot bytes=%u", (unsigned)cfg->rb_biopot_bytes);
        return ESP_ERR_NO_MEM;
    }

    s->mutex = xSemaphoreCreateMutex();
    if (!s->mutex) {
        (void)nocti_stream_deinit(s);
        NOCTI_LOGE(TAG, "mutex alloc failed");
        return ESP_ERR_NO_MEM;
    }

    s->seq = 0;
    s->t0_us = now_us;
    s->next_time64_due_us = rel_us_from_now(s, now_us) + NOCTI_STREAM_TIME64_PERIOD_US;
    s->inited = true;

    // slash: snapshot heap setelah alokasi
    log_heap_snapshot("after_stream_alloc");

    // slash: push SESSION + TIME64 sekali saat init, session_id default 0
    {
        esp_err_t err = lock(s, 0);
        if (err == ESP_OK) {
            (void)push_session_locked(s, now_us, 0u);
            (void)push_time64_locked(s, now_us);
            unlock(s);
        }
    }

    NOCTI_LOGI(TAG, "stream inited: rb_total=%u bytes, frame_max=%u bytes",
              (unsigned)(cfg->rb_ctrl_bytes + cfg->rb_fnirs_bytes + cfg->rb_biopot_bytes),
              (unsigned)NOCTI_STREAM_MAX_FRAME_BYTES);

    return ESP_OK;
}

esp_err_t nocti_stream_init_now(nocti_stream_t *s, const nocti_stream_cfg_t *cfg)
{
    // slash: convenience init
    return nocti_stream_init(s, cfg, (nocti_time_us_t)nocti_timebase_now_us());
}

esp_err_t nocti_stream_deinit(nocti_stream_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;

    if (s->rb_ctrl) {
        vRingbufferDelete(s->rb_ctrl);
        s->rb_ctrl = NULL;
    }
    if (s->rb_fnirs) {
        vRingbufferDelete(s->rb_fnirs);
        s->rb_fnirs = NULL;
    }
    if (s->rb_biopot) {
        vRingbufferDelete(s->rb_biopot);
        s->rb_biopot = NULL;
    }

    if (s->mutex) {
        vSemaphoreDelete(s->mutex);
        s->mutex = NULL;
    }

    memset(s, 0, sizeof(*s));
    return ESP_OK;
}

esp_err_t nocti_stream_clear(nocti_stream_t *s)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;

    // slash: lock agar aman jika producer masih mendorong frame
    esp_err_t err = lock(s, 50);
    if (err != ESP_OK) return err;

    clear_rb(s->rb_ctrl);
    clear_rb(s->rb_fnirs);
    clear_rb(s->rb_biopot);

    unlock(s);
    return ESP_OK;
}

esp_err_t nocti_stream_start_session(nocti_stream_t *s, nocti_time_us_t now_us, uint32_t session_id)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;

    esp_err_t err = lock(s, 50);
    if (err != ESP_OK) return err;

    // slash: buang semua item lama tanpa copy
    clear_rb(s->rb_ctrl);
    clear_rb(s->rb_fnirs);
    clear_rb(s->rb_biopot);

    // slash: reset metadata sesi
    s->seq = 0;
    s->t0_us = now_us;
    s->next_time64_due_us = 0u + NOCTI_STREAM_TIME64_PERIOD_US;

    // slash: push boundary record
    (void)push_session_locked(s, now_us, session_id);
    (void)push_time64_locked(s, now_us);

    unlock(s);
    return ESP_OK;
}

esp_err_t nocti_stream_start_session_now(nocti_stream_t *s, uint32_t session_id)
{
    return nocti_stream_start_session(s, (nocti_time_us_t)nocti_timebase_now_us(), session_id);
}

esp_err_t nocti_stream_push(nocti_stream_t *s,
                           nocti_rec_type_t type,
                           nocti_time_us_t now_us,
                           const void *payload,
                           size_t payload_len)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;

    // slash: lock 0 ms agar producer tidak block lama (jaga realtime)
    esp_err_t err = lock(s, 0);
    if (err != ESP_OK) return err;

    err = push_locked(s, type, now_us, payload, payload_len);

    unlock(s);
    return err;
}

esp_err_t nocti_stream_push_now(nocti_stream_t *s,
                               nocti_rec_type_t type,
                               const void *payload,
                               size_t payload_len)
{
    // slash: convenience push
    return nocti_stream_push(s, type, (nocti_time_us_t)nocti_timebase_now_us(), payload, payload_len);
}

esp_err_t nocti_stream_push_time64(nocti_stream_t *s, nocti_time_us_t now_us)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;

    esp_err_t err = lock(s, 0);
    if (err != ESP_OK) return err;

    err = push_time64_locked(s, now_us);

    unlock(s);
    return err;
}

esp_err_t nocti_stream_maybe_push_time64(nocti_stream_t *s, nocti_time_us_t now_us)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;

    esp_err_t err = lock(s, 0);
    if (err != ESP_OK) return err;

    uint32_t rel_us = rel_us_from_now(s, now_us);

    // slash: wrap-safe check rel_us >= next_due
    if (time_after_eq_u32(rel_us, s->next_time64_due_us)) {
        s->next_time64_due_us = rel_us + NOCTI_STREAM_TIME64_PERIOD_US;
        err = push_time64_locked(s, now_us);
        unlock(s);
        return err;
    }

    unlock(s);
    return ESP_OK;
}

esp_err_t nocti_stream_maybe_push_time64_now(nocti_stream_t *s)
{
    // slash: convenience maybe TIME64
    return nocti_stream_maybe_push_time64(s, (nocti_time_us_t)nocti_timebase_now_us());
}

// ============================================================
// Zero-copy receive
// ============================================================

static void *recv_from_rb(RingbufHandle_t rb, size_t *out_sz, TickType_t ticks, RingbufHandle_t *owner)
{
    if (owner) *owner = NULL;
    if (!rb) return NULL;

    size_t sz = 0;
    void *item = xRingbufferReceive(rb, &sz, ticks);
    if (!item) return NULL;

    if (out_sz) *out_sz = sz;
    if (owner) *owner = rb;
    return item;
}

esp_err_t nocti_stream_recv_item(nocti_stream_t *s,
                                 const uint8_t **out_item_ptr,
                                 size_t *out_len,
                                 void **out_owner,
                                 uint32_t timeout_ms)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;
    if (!out_item_ptr || !out_len || !out_owner) return ESP_ERR_INVALID_ARG;

    *out_item_ptr = NULL;
    *out_len = 0;
    *out_owner = NULL;

    size_t item_size = 0;
    RingbufHandle_t owner = NULL;
    void *item = NULL;

    // slash: coba non-blocking semua dulu (prioritas ctrl -> fnirs -> biopot)
    item = recv_from_rb(s->rb_ctrl, &item_size, 0, &owner);
    if (!item) item = recv_from_rb(s->rb_fnirs, &item_size, 0, &owner);
    if (!item) item = recv_from_rb(s->rb_biopot, &item_size, 0, &owner);

    // slash: kalau kosong semua, block sebentar di ctrl, lalu retry semua
    if (!item) {
        item = recv_from_rb(s->rb_ctrl, &item_size, ms_to_ticks(timeout_ms), &owner);
        if (!item) {
            item = recv_from_rb(s->rb_fnirs, &item_size, 0, &owner);
            if (!item) item = recv_from_rb(s->rb_biopot, &item_size, 0, &owner);
        }
    }

    if (!item) return ESP_ERR_TIMEOUT;

    *out_item_ptr = (const uint8_t *)item;
    *out_len = item_size;
    *out_owner = (void *)owner;
    return ESP_OK;
}

void nocti_stream_return_item(void *owner, void *item_ptr)
{
    if (!owner || !item_ptr) return;
    vRingbufferReturnItem((RingbufHandle_t)owner, item_ptr);
}

// ============================================================
// Copy read (legacy)
// ============================================================

esp_err_t nocti_stream_read(nocti_stream_t *s,
                           uint8_t *out,
                           size_t out_cap,
                           size_t *out_len,
                           uint32_t timeout_ms)
{
    if (!handle_ok(s)) return ESP_ERR_INVALID_STATE;
    if (!out || !out_len) return ESP_ERR_INVALID_ARG;

    *out_len = 0;

    const uint8_t *item_ptr = NULL;
    size_t item_len = 0;
    void *owner = NULL;

    esp_err_t r = nocti_stream_recv_item(s, &item_ptr, &item_len, &owner, timeout_ms);
    if (r != ESP_OK) return r;

    if (item_len > out_cap) {
        nocti_stream_return_item(owner, (void *)item_ptr);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(out, item_ptr, item_len);
    *out_len = item_len;

    nocti_stream_return_item(owner, (void *)item_ptr);
    return ESP_OK;
}

// ============================================================
// Drop counters
// ============================================================

uint32_t nocti_stream_drop_ctrl(const nocti_stream_t *s)
{
    return s ? s->drop_ctrl : 0;
}

uint32_t nocti_stream_drop_fnirs(const nocti_stream_t *s)
{
    return s ? s->drop_fnirs : 0;
}

uint32_t nocti_stream_drop_biopot(const nocti_stream_t *s)
{
    return s ? s->drop_biopot : 0;
}
