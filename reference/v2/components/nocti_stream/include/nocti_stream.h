// ============================================================
// File: components/nocti_stream/include/nocti_stream.h
// ============================================================

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// NoctiNIRS Stream v1
// slash: stream membungkus payload menjadi frame konsisten untuk WiFi, BLE, dan storage
// slash: stream bukan transport, dia hanya buffer + framing + drop policy
// ============================================================

#define NOCTI_STREAM_MAGIC                 (0xA5u)
#define NOCTI_STREAM_VERSION               (0x01u)

#define NOCTI_STREAM_MAX_PAYLOAD_BYTES     (512u)
#define NOCTI_STREAM_HDR_BYTES             (12u)
#define NOCTI_STREAM_CRC_BYTES             (2u)
#define NOCTI_STREAM_MAX_FRAME_BYTES       (NOCTI_STREAM_HDR_BYTES + NOCTI_STREAM_MAX_PAYLOAD_BYTES + NOCTI_STREAM_CRC_BYTES)

// ============================================================
// RAM guard policy (optional override)
// slash: boleh di-override dari build system dengan -DNOCTI_STREAM_RB_*
// ============================================================

#ifndef NOCTI_STREAM_RB_MIN_BYTES
#define NOCTI_STREAM_RB_MIN_BYTES        ((size_t)(NOCTI_STREAM_MAX_FRAME_BYTES * 4u)) // slash: min 4 frame
#endif

#ifndef NOCTI_STREAM_RB_MAX_BYTES
#define NOCTI_STREAM_RB_MAX_BYTES        ((size_t)(192u * 1024u)) // slash: limit per ringbuffer
#endif

#ifndef NOCTI_STREAM_RB_TOTAL_MAX_BYTES
#define NOCTI_STREAM_RB_TOTAL_MAX_BYTES  ((size_t)(256u * 1024u)) // slash: limit total 3 ringbuffer
#endif

// slash: TIME64 sync dipush saat start dan setiap 30 detik
#define NOCTI_STREAM_TIME64_PERIOD_US      (30000000u)

// ============================================================
// Record types
// ============================================================

typedef enum {
    NOCTI_REC_INVALID = 0,
    NOCTI_REC_STATUS  = 1,
    NOCTI_REC_EVENT   = 2,
    NOCTI_REC_TIME64  = 3,
    NOCTI_REC_FNIRS   = 4,
    NOCTI_REC_BIOPOT  = 5,

    NOCTI_REC_FNIRS_CFG   = 6,  // slash: config sekali
    NOCTI_REC_FNIRS_FRAME = 7,  // slash: data per frame

    NOCTI_REC_SESSION     = 8,  // slash: meta sesi, dikirim pada START

    // slash: biopot v1 (ADS1299)
    // /belajar: cfg dikirim sekali saat START agar decoder tahu fs, gain, dan mask
    // /belajar: frame adalah batching beberapa sample agar overhead transport turun
    NOCTI_REC_BIOPOT_CFG   = 9,
    NOCTI_REC_BIOPOT_FRAME = 10,
} nocti_rec_type_t;

// ============================================================
// Flags (header)
// ============================================================

typedef enum {
    NOCTI_STREAM_F_NONE       = 0x00,
    NOCTI_STREAM_F_HAS_CRC16  = 0x01, // slash: v1 selalu pakai CRC16
} nocti_stream_flags_t;

// ============================================================
// Frame header (packed)
// slash: t_rel_us adalah waktu relatif sejak stream start, 32-bit (wrap sekitar 71.6 menit)
// slash: TIME64 record dipakai untuk re-anchor sesi panjang
// ============================================================

typedef struct __attribute__((packed)) {
    uint8_t  magic;        // 0xA5
    uint8_t  version;      // 0x01
    uint8_t  type;         // nocti_rec_type_t
    uint8_t  flags;        // nocti_stream_flags_t
    uint16_t seq;          // rolling sequence
    uint32_t t_rel_us;     // relative timestamp us (wrap ok)
    uint16_t payload_len;  // bytes
} nocti_stream_hdr_t;

// ============================================================
// Payload: session v1 (NOCTI_REC_SESSION)
// slash: dipush saat START untuk memberi boundary sesi dan bantu decoder
// ============================================================

#define NOCTI_PAYLOAD_SESSION_V1_VERSION   (1u)

typedef struct __attribute__((packed)) {
    uint8_t  version;      // = NOCTI_PAYLOAD_SESSION_V1_VERSION
    uint8_t  _rsv0[3];
    uint32_t session_id;   // slash: sama dengan session_id UDP jika dipakai
    uint64_t t0_us;        // slash: absolute base time at session start
} nocti_payload_session_v1_t;

// ============================================================
// Config
// slash: ukuran ring buffer dalam bytes, sesuaikan dengan PSRAM
// ============================================================

typedef struct {
    size_t rb_ctrl_bytes;   // status, event, time64, session, cfg
    size_t rb_fnirs_bytes;  // fnirs records
    size_t rb_biopot_bytes; // biopot records
} nocti_stream_cfg_t;

// ============================================================
// Runtime handle
// ============================================================

typedef struct nocti_stream_t{
    bool inited;

    RingbufHandle_t rb_ctrl;
    RingbufHandle_t rb_fnirs;
    RingbufHandle_t rb_biopot;

    SemaphoreHandle_t mutex;

    uint16_t seq;
    nocti_time_us_t t0_us;         // absolute base time (us) at stream start
    uint32_t next_time64_due_us;   // relative us time for next TIME64 record

    uint32_t drop_ctrl;
    uint32_t drop_fnirs;
    uint32_t drop_biopot;
} nocti_stream_t;

// ============================================================
// API
// ============================================================

esp_err_t nocti_stream_init(nocti_stream_t *s, const nocti_stream_cfg_t *cfg, nocti_time_us_t now_us);
esp_err_t nocti_stream_deinit(nocti_stream_t *s);

// Convenience init (now_us dari timebase)
// slash: memudahkan pemanggil, tapi dependency jadi ke nocti_timebase
esp_err_t nocti_stream_init_now(nocti_stream_t *s, const nocti_stream_cfg_t *cfg);

// ============================================================
// Session control
// ============================================================

// slash: reset boundary sesi, clear ringbuffer, reset seq, push SESSION + TIME64
esp_err_t nocti_stream_start_session(nocti_stream_t *s, nocti_time_us_t now_us, uint32_t session_id);

// slash: convenience start session (now_us dari timebase)
esp_err_t nocti_stream_start_session_now(nocti_stream_t *s, uint32_t session_id);

// slash: clear semua ringbuffer tanpa copy
esp_err_t nocti_stream_clear(nocti_stream_t *s);

// ============================================================
// Push generic payload
// slash: panggil dari producer (fnirs, biopot, status)
// slash: now_us sebaiknya dari nocti_timebase_now_us
// ============================================================

esp_err_t nocti_stream_push(nocti_stream_t *s,
                            nocti_rec_type_t type,
                            nocti_time_us_t now_us,
                            const void *payload,
                            size_t payload_len);

// Convenience push (now_us dari timebase)
esp_err_t nocti_stream_push_now(nocti_stream_t *s,
                                nocti_rec_type_t type,
                                const void *payload,
                                size_t payload_len);

// ============================================================
// Convenience push untuk payload struct (hindari salah ukuran)
// slash: ini wrapper tipis, tetap memakai nocti_stream_push
// ============================================================

static inline esp_err_t nocti_stream_push_fnirs_v1(nocti_stream_t *s,
                                                   nocti_time_us_t now_us,
                                                   const nocti_payload_fnirs_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_FNIRS, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_status_v1(nocti_stream_t *s,
                                                    nocti_time_us_t now_us,
                                                    const nocti_payload_status_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_STATUS, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_event_v1(nocti_stream_t *s,
                                                   nocti_time_us_t now_us,
                                                   const nocti_payload_event_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_EVENT, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_session_v1(nocti_stream_t *s,
                                                     nocti_time_us_t now_us,
                                                     const nocti_payload_session_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_SESSION, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_fnirs_cfg_v1(nocti_stream_t *s,
                                                       nocti_time_us_t now_us,
                                                       const nocti_payload_fnirs_cfg_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_FNIRS_CFG, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_fnirs_cfg_v2(nocti_stream_t *s,
                                                       nocti_time_us_t now_us,
                                                       const nocti_payload_fnirs_cfg_v2_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_FNIRS_CFG, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_fnirs_frame_v1(nocti_stream_t *s,
                                                         nocti_time_us_t now_us,
                                                         const nocti_payload_fnirs_frame_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_FNIRS_FRAME, now_us, p, sizeof(*p));
}

// ============================================================
// Biopot v1 wrappers
// slash: wrapper tipis agar ukuran payload tidak salah
// ============================================================

static inline esp_err_t nocti_stream_push_biopot_cfg_v1(nocti_stream_t *s,
                                                        nocti_time_us_t now_us,
                                                        const nocti_payload_biopot_cfg_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_BIOPOT_CFG, now_us, p, sizeof(*p));
}

static inline esp_err_t nocti_stream_push_biopot_frame_v1(nocti_stream_t *s,
                                                          nocti_time_us_t now_us,
                                                          const nocti_payload_biopot_frame_v1_t *p)
{
    return nocti_stream_push(s, NOCTI_REC_BIOPOT_FRAME, now_us, p, sizeof(*p));
}

// ============================================================
// TIME64
// ============================================================

// Push TIME64 sync record
// slash: record ini membawa absolute time untuk resync sesi panjang dan wrap t_rel_us
esp_err_t nocti_stream_push_time64(nocti_stream_t *s, nocti_time_us_t now_us);

// Auto TIME64 (dipanggil producer atau scheduler saat idle ringan)
esp_err_t nocti_stream_maybe_push_time64(nocti_stream_t *s, nocti_time_us_t now_us);

// Convenience maybe TIME64 (now_us dari timebase)
esp_err_t nocti_stream_maybe_push_time64_now(nocti_stream_t *s);

// ============================================================
// Read API
// ============================================================

// Pop next frame into user buffer (copy)
// slash: consumer side (WiFi, BLE, storage) panggil ini jika butuh buffer contigu
// slash: urutan pop: ctrl -> fnirs -> biopot
esp_err_t nocti_stream_read(nocti_stream_t *s,
                            uint8_t *out,
                            size_t out_cap,
                            size_t *out_len,
                            uint32_t timeout_ms);

// Zero-copy receive
// slash: out_item_ptr menunjuk ke memory internal ringbuffer
// slash: caller wajib panggil nocti_stream_return_item(owner, item_ptr) setelah selesai
esp_err_t nocti_stream_recv_item(nocti_stream_t *s,
                                 const uint8_t **out_item_ptr,
                                 size_t *out_len,
                                 void **out_owner,
                                 uint32_t timeout_ms);

// Return item dari recv_item
// slash: owner adalah token yang dikembalikan recv_item
void nocti_stream_return_item(void *owner, void *item_ptr);

// Drop counters
uint32_t nocti_stream_drop_ctrl(const nocti_stream_t *s);
uint32_t nocti_stream_drop_fnirs(const nocti_stream_t *s);
uint32_t nocti_stream_drop_biopot(const nocti_stream_t *s);

#ifdef __cplusplus
}
#endif
