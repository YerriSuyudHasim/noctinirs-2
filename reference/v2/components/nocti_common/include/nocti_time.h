// ============================================================
// File: components/nocti_common/include/nocti_time.h
// ============================================================
//
// Tujuan file ini
// - Mengunci konsep dua waktu: t_boot_us dan t_rec_us
// - Menyediakan state kecil untuk mengubah "boot time" menjadi "recording time"
//
// Catatan desain
// - Tidak memanggil nocti_timebase di sini
// - Modul lain boleh mengisi now_boot_us dari mana saja (misal timebase now_us)

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// State konversi waktu untuk sesi recording
typedef struct {
    bool            rec_active;         // slash: true jika recording sedang berjalan
    nocti_time_us_t rec_start_boot_us;   // slash: t_boot_us saat START, dipakai sebagai nol sesi
    nocti_time_us_t last_boot_us;        // slash: debug atau validasi monotonik
} nocti_time_state_t;

// Init state waktu
static inline void nocti_time_init(nocti_time_state_t *st)
{
    if (!st) return;
    st->rec_active = false;
    st->rec_start_boot_us = 0;
    st->last_boot_us = 0;
}

// Dipanggil saat START recording
static inline void nocti_time_on_rec_start(nocti_time_state_t *st, nocti_time_us_t now_boot_us)
{
    if (!st) return;
    st->rec_active = true;
    st->rec_start_boot_us = now_boot_us;
    st->last_boot_us = now_boot_us;
}

// Dipanggil saat STOP recording
static inline void nocti_time_on_rec_stop(nocti_time_state_t *st, nocti_time_us_t now_boot_us)
{
    if (!st) return;
    st->rec_active = false;
    st->last_boot_us = now_boot_us;
}

// Hitung t_rec_us dari now_boot_us
// - Jika belum recording, return 0
static inline nocti_time_us_t nocti_time_rec_us(const nocti_time_state_t *st, nocti_time_us_t now_boot_us)
{
    if (!st) return 0;
    if (!st->rec_active) return 0;
    return (now_boot_us - st->rec_start_boot_us);
}

#ifdef __cplusplus
}
#endif
