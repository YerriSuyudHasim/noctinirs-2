// ============================================================
// File: components/nocti_common/include/nocti_status.h
// ============================================================
//
// Tujuan file ini
// - Menyatukan format status device, terutama battery
// - Status ini boleh dipush periodik via BLE atau dipull oleh host
//
// Catatan desain
// - Hanya "format data", bukan cara membaca hardware battery

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Battery status
// ------------------------------------------------------------

typedef struct {
    uint16_t        mv;            // slash: tegangan baterai dalam millivolt
    uint8_t         percent;       // slash: 0-100 estimasi SoC
    bool            is_charging;   // slash: true jika sedang charge
    bool            is_valid;      // slash: false jika sensor belum siap atau nilai tidak dipercaya
    nocti_time_us_t t_rec_us;      // slash: timestamp recording time saat status dibuat
} nocti_battery_status_t;

// ------------------------------------------------------------
// Device status flags (bitmask)
// ------------------------------------------------------------

typedef enum {
    NOCTI_STATF_STREAMING       = (1u << 0),  // slash: sedang streaming data
    NOCTI_STATF_BLE_CONNECTED   = (1u << 1),  // slash: BLE link aktif
    NOCTI_STATF_LOW_BATT        = (1u << 2),  // slash: baterai rendah
    NOCTI_STATF_OVERFLOW        = (1u << 3),  // slash: buffer overflow / data drop
    NOCTI_STATF_OVERTEMP        = (1u << 4),  // slash: over temperature terdeteksi
    NOCTI_STATF_SENSOR_FAULT    = (1u << 5),  // slash: fault sensor (fnirs/biopot) terdeteksi
    NOCTI_STATF_STORAGE_FAULT   = (1u << 6),  // slash: fault terkait storage/logging
} nocti_status_flags_t;

// Snapshot status device
typedef struct {
    nocti_battery_status_t battery;   // slash: status baterai terbaru
    uint32_t               flags;     // slash: gabungan nocti_status_flags_t
    nocti_time_us_t        t_rec_us;  // slash: timestamp snapshot dibuat
} nocti_device_status_t;

// Helper set/clear flag biar tidak typo
static inline void nocti_status_set_flag(nocti_device_status_t *st, uint32_t flag, bool on)
{
    if (!st) return;
    if (on) st->flags |= flag;
    else    st->flags &= ~flag;
}

static inline bool nocti_status_has_flag(const nocti_device_status_t *st, uint32_t flag)
{
    if (!st) return false;
    return ((st->flags & flag) != 0u);
}

#ifdef __cplusplus
}
#endif
