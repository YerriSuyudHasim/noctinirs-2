// ============================================================
// File: components/nocti_storage/include/nocti_storage.h
// ============================================================

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// NoctiNIRS Storage v0 (Recording Manager)
// slash: di versi ini, storage tidak menulis ke media fisik
// slash: fungsi utamanya mengelola sesi recording dan menerbitkan STATUS, EVENT ke nocti_stream
// slash: nanti bisa ditambah backend flash spool tanpa ubah producer dan BLE
// ============================================================

typedef struct nocti_stream_t nocti_stream_t;

// Event codes sederhana untuk logging
typedef enum {
    NOCTI_EVT_REC_START       = 1, // slash: mulai sesi
    NOCTI_EVT_REC_STOP        = 2, // slash: stop sesi
    NOCTI_EVT_LINK_UP         = 3, // slash: koneksi uplink aktif (BLE connected dan notify enabled)
    NOCTI_EVT_LINK_DOWN       = 4, // slash: uplink putus
    NOCTI_EVT_DROP_REPORT     = 5, // slash: laporan drop counter
} nocti_evt_code_t;

// Payload STATUS untuk monitoring dari sisi PC
// slash: dibuat fixed size agar gampang diparse
typedef struct __attribute__((packed)) {
    uint32_t session_id;         // slash: id sesi aktif
    uint32_t t_rec_ms;           // slash: waktu sejak start recording dalam ms (wrap ok)
    uint32_t drop_ctrl;          // slash: drop dari stream ctrl
    uint32_t drop_fnirs;         // slash: drop dari stream fnirs
    uint32_t drop_biopot;        // slash: drop dari stream biopot
    uint8_t  rec_on;             // slash: 1 kalau recording aktif
    uint8_t  link_on;            // slash: 1 kalau uplink aktif
    uint16_t reserved;           // slash: align dan ruang future
} nocti_status_v0_t;

// Config
typedef struct {
    nocti_stream_t *stream;         // slash: target stream untuk push record
    uint32_t status_period_ms;      // slash: periode STATUS, 0 untuk disable
    uint32_t drop_report_period_ms; // slash: periode event drop report, 0 untuk disable
} nocti_storage_cfg_t;

// Runtime handle
typedef struct {
    bool inited;

    nocti_storage_cfg_t cfg;

    uint32_t session_id;            // slash: naik tiap START
    bool rec_on;                    // slash: state recording
    bool link_on;                   // slash: state uplink

    nocti_time_us_t t0_boot_us;     // slash: base time saat init
    nocti_time_us_t t0_rec_us;      // slash: base time saat start recording

    uint32_t next_status_due_ms;    // slash: scheduler periodik
    uint32_t next_drop_due_ms;      // slash: scheduler periodik

    // cache drop terakhir, agar laporan tidak spam kalau tidak berubah
    uint32_t last_drop_ctrl;
    uint32_t last_drop_fnirs;
    uint32_t last_drop_biopot;
} nocti_storage_t;

// ============================================================
// API
// ============================================================

// Init recording manager
esp_err_t nocti_storage_init(nocti_storage_t *st, const nocti_storage_cfg_t *cfg, nocti_time_us_t now_us);
esp_err_t nocti_storage_deinit(nocti_storage_t *st);

// Convenience init (now_us dari timebase boleh dilakukan di app, bukan di sini)
esp_err_t nocti_storage_init_now(nocti_storage_t *st, const nocti_storage_cfg_t *cfg);

// Start dan stop sesi recording
esp_err_t nocti_storage_start(nocti_storage_t *st, nocti_time_us_t now_us);
esp_err_t nocti_storage_stop(nocti_storage_t *st, nocti_time_us_t now_us);

// Update state uplink dari BLE layer
// slash: panggil saat connect atau subscribe notify berubah
esp_err_t nocti_storage_set_link(nocti_storage_t *st, bool link_on, nocti_time_us_t now_us);

// Periodic service, dipanggil dari loop main atau task ringan
// slash: tugasnya push STATUS dan event drop report sesuai period
esp_err_t nocti_storage_service(nocti_storage_t *st, nocti_time_us_t now_us);

// Getter sederhana
uint32_t nocti_storage_session_id(const nocti_storage_t *st);
bool     nocti_storage_is_recording(const nocti_storage_t *st);
bool     nocti_storage_is_link_on(const nocti_storage_t *st);

#ifdef __cplusplus
}
#endif
