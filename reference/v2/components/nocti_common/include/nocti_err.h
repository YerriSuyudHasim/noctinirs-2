// ============================================================
// File: components/nocti_common/include/nocti_err.h
// ============================================================
//
// Tujuan file ini
// - Menyatukan model error dan result NoctiNIRS
// - Menghindari tiap modul bikin enum error sendiri-sendiri
//
// Prinsip desain
// - Ringkas
// - Bisa dipakai tanpa ESP-IDF
// - Bisa di-map ke esp_err_t jika diperlukan

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Result code umum
// ------------------------------------------------------------

typedef enum {
    NOCTI_OK = 0,                 // slash: operasi sukses
    NOCTI_ERR_GENERIC = -1,       // slash: error tidak spesifik
    NOCTI_ERR_INVALID_ARG = -2,   // slash: argumen tidak valid
    NOCTI_ERR_TIMEOUT = -3,       // slash: timeout menunggu event
    NOCTI_ERR_NO_MEM = -4,        // slash: kehabisan memori atau buffer
    NOCTI_ERR_BUSY = -5,          // slash: resource sedang dipakai
    NOCTI_ERR_NOT_READY = -6,     // slash: modul belum siap
    NOCTI_ERR_IO = -7,            // slash: error I/O (SPI, I2C, dll)
    NOCTI_ERR_CRC = -8,           // slash: CRC mismatch
    NOCTI_ERR_OVERFLOW = -9,      // slash: buffer overflow / data drop
    NOCTI_ERR_UNSUPPORTED = -10,  // slash: fitur tidak didukung
} nocti_err_t;

// ------------------------------------------------------------
// Helper macro sederhana
// ------------------------------------------------------------

// Cek result dan langsung return jika error
#define NOCTI_RETURN_ON_ERR(expr)        \
    do {                                 \
        nocti_err_t __e = (expr);        \
        if (__e != NOCTI_OK) {           \
            return __e;                  \
        }                                \
    } while (0)

// Boolean helper
#define NOCTI_SUCCEEDED(e) ((e) == NOCTI_OK)
#define NOCTI_FAILED(e)    ((e) != NOCTI_OK)

#ifdef __cplusplus
}
#endif
