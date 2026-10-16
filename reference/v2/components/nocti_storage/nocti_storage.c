// ============================================================
// File: components/nocti_storage/nocti_storage.c
// ============================================================

#include "nocti_storage.h"

#include <string.h>

#include "nocti_log.h"
#include "nocti_stream.h"
#include "nocti_timebase.h" // slash: untuk init_now dan now_us helper

static const char *TAG = "nocti_storage";

static inline uint32_t us_to_ms_u32(nocti_time_us_t us)
{
    if (us <= 0) return 0;
    return (uint32_t)(us / 1000); // slash: wrap natural setelah 2^32 ms
}

static inline bool time_after_eq_u32(uint32_t a, uint32_t b)
{
    // slash: wrap safe compare untuk ms counter
    return (int32_t)(a - b) >= 0;
}

static esp_err_t push_event(nocti_storage_t *st, nocti_evt_code_t code, nocti_time_us_t now_us, uint32_t v0, uint32_t v1)
{
    // slash: payload event kecil, fixed 12 byte, mudah diparse di PC
    typedef struct __attribute__((packed)) {
        uint8_t  code;       // slash: nocti_evt_code_t
        uint8_t  rsv0;
        uint16_t rsv1;
        uint32_t v0;         // slash: nilai bebas
        uint32_t v1;         // slash: nilai bebas
    } evt_v0_t;

    if (!st || !st->cfg.stream) return ESP_ERR_INVALID_STATE;

    evt_v0_t e;
    memset(&e, 0, sizeof(e));
    e.code = (uint8_t)code;
    e.v0 = v0;
    e.v1 = v1;

    return nocti_stream_push(st->cfg.stream, NOCTI_REC_EVENT, now_us, &e, sizeof(e));
}

static esp_err_t push_status(nocti_storage_t *st, nocti_time_us_t now_us)
{
    if (!st || !st->cfg.stream) return ESP_ERR_INVALID_STATE;

    nocti_status_v0_t s;
    memset(&s, 0, sizeof(s));

    s.session_id = st->session_id;

    nocti_time_us_t t_rec_us = now_us - st->t0_rec_us;
    if (!st->rec_on) t_rec_us = 0; // slash: jika tidak recording, t_rec_ms dibuat 0 agar sederhana
    if (t_rec_us < 0) t_rec_us = 0;

    s.t_rec_ms = us_to_ms_u32(t_rec_us);

    // slash: drop counter diambil dari nocti_stream
    s.drop_ctrl   = nocti_stream_drop_ctrl(st->cfg.stream);
    s.drop_fnirs  = nocti_stream_drop_fnirs(st->cfg.stream);
    s.drop_biopot = nocti_stream_drop_biopot(st->cfg.stream);

    s.rec_on  = (uint8_t)(st->rec_on ? 1 : 0);
    s.link_on = (uint8_t)(st->link_on ? 1 : 0);

    return nocti_stream_push(st->cfg.stream, NOCTI_REC_STATUS, now_us, &s, sizeof(s));
}

esp_err_t nocti_storage_init(nocti_storage_t *st, const nocti_storage_cfg_t *cfg, nocti_time_us_t now_us)
{
    if (!st || !cfg) return ESP_ERR_INVALID_ARG;
    if (!cfg->stream) return ESP_ERR_INVALID_ARG;

    memset(st, 0, sizeof(*st));
    st->cfg = *cfg;

    st->t0_boot_us = now_us;
    st->t0_rec_us = now_us;

    st->session_id = 0;
    st->rec_on = false;
    st->link_on = false;

    st->next_status_due_ms = 0;
    st->next_drop_due_ms = 0;

    st->last_drop_ctrl = 0;
    st->last_drop_fnirs = 0;
    st->last_drop_biopot = 0;

    st->inited = true;

    NOCTI_LOGI(TAG, "storage manager inited");
    return ESP_OK;
}

esp_err_t nocti_storage_init_now(nocti_storage_t *st, const nocti_storage_cfg_t *cfg)
{
    // slash: convenience init
    return nocti_storage_init(st, cfg, (nocti_time_us_t)nocti_timebase_now_us());
}

esp_err_t nocti_storage_deinit(nocti_storage_t *st)
{
    if (!st) return ESP_ERR_INVALID_ARG;
    memset(st, 0, sizeof(*st));
    return ESP_OK;
}

esp_err_t nocti_storage_start(nocti_storage_t *st, nocti_time_us_t now_us)
{
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;

    st->session_id += 1;
    st->rec_on = true;
    st->t0_rec_us = now_us;

    // schedule status period
    uint32_t now_ms = us_to_ms_u32(now_us - st->t0_boot_us);
    if (st->cfg.status_period_ms > 0) {
        st->next_status_due_ms = now_ms;
    }
    if (st->cfg.drop_report_period_ms > 0) {
        st->next_drop_due_ms = now_ms;
    }

    (void)push_event(st, NOCTI_EVT_REC_START, now_us, st->session_id, 0);
    (void)nocti_stream_push_time64(st->cfg.stream, now_us); // slash: anchor absolut saat start

    NOCTI_LOGI(TAG, "rec start, session=%u", (unsigned)st->session_id);
    return ESP_OK;
}

esp_err_t nocti_storage_stop(nocti_storage_t *st, nocti_time_us_t now_us)
{
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;

    st->rec_on = false;

    (void)push_event(st, NOCTI_EVT_REC_STOP, now_us, st->session_id, 0);

    NOCTI_LOGI(TAG, "rec stop, session=%u", (unsigned)st->session_id);
    return ESP_OK;
}

esp_err_t nocti_storage_set_link(nocti_storage_t *st, bool link_on, nocti_time_us_t now_us)
{
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;

    if (st->link_on == link_on) {
        return ESP_OK;
    }

    st->link_on = link_on;

    if (link_on) {
        (void)push_event(st, NOCTI_EVT_LINK_UP, now_us, 0, 0);
    } else {
        (void)push_event(st, NOCTI_EVT_LINK_DOWN, now_us, 0, 0);
    }

    return ESP_OK;
}

esp_err_t nocti_storage_service(nocti_storage_t *st, nocti_time_us_t now_us)
{
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;

    // slash: schedule pakai ms relatif boot agar wrap safe
    uint32_t now_ms = us_to_ms_u32(now_us - st->t0_boot_us);

    // push STATUS periodik
    if (st->cfg.status_period_ms > 0) {
        if (time_after_eq_u32(now_ms, st->next_status_due_ms)) {
            st->next_status_due_ms = now_ms + st->cfg.status_period_ms;
            (void)push_status(st, now_us);
        }
    }

    // drop report periodik, hanya kalau drop berubah atau memang interval lewat
    if (st->cfg.drop_report_period_ms > 0) {
        if (time_after_eq_u32(now_ms, st->next_drop_due_ms)) {
            st->next_drop_due_ms = now_ms + st->cfg.drop_report_period_ms;

            uint32_t dc = nocti_stream_drop_ctrl(st->cfg.stream);
            uint32_t df = nocti_stream_drop_fnirs(st->cfg.stream);
            uint32_t db = nocti_stream_drop_biopot(st->cfg.stream);

            bool changed = (dc != st->last_drop_ctrl) || (df != st->last_drop_fnirs) || (db != st->last_drop_biopot);

            st->last_drop_ctrl = dc;
            st->last_drop_fnirs = df;
            st->last_drop_biopot = db;

            if (changed) {
                // slash: laporkan ringkas, v0 dan v1 dipakai packing
                // slash: v0 = drop_ctrl, v1 high16 drop_fnirs low16 drop_biopot
                uint32_t v0 = dc;
                uint32_t v1 = ((df & 0xFFFFu) << 16) | (db & 0xFFFFu);
                (void)push_event(st, NOCTI_EVT_DROP_REPORT, now_us, v0, v1);
            }
        }
    }

    // slash: TIME64 periodik tetap dipanggil dari tempat lain juga boleh
    (void)nocti_stream_maybe_push_time64(st->cfg.stream, now_us);

    return ESP_OK;
}

uint32_t nocti_storage_session_id(const nocti_storage_t *st)
{
    return st ? st->session_id : 0;
}

bool nocti_storage_is_recording(const nocti_storage_t *st)
{
    return st ? st->rec_on : false;
}

bool nocti_storage_is_link_on(const nocti_storage_t *st)
{
    return st ? st->link_on : false;
}
