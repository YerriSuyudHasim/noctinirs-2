// ============================================================
// File: components/nocti_common/include/nocti_crc.h
// ============================================================
//
// Tujuan file ini
// - CRC untuk menjaga integritas packet di transport stream (BLE) dan storage
//
// Catatan
// - Kita pakai CRC16-CCITT-FALSE yang umum dan ringan
// - Implementasi ada di nocti_crc.c

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// CRC16-CCITT-FALSE
// - poly: 0x1021
// - init: 0xFFFF
// - xorout: 0x0000
// - refin: false, refout: false
uint16_t nocti_crc16_ccitt_false(const void *data, size_t len);

// Helper untuk update CRC dengan chunk bertahap
uint16_t nocti_crc16_ccitt_false_update(uint16_t crc, const void *data, size_t len);

// Nilai init standar
static inline uint16_t nocti_crc16_init(void)
{
    return (uint16_t)0xFFFFu;           // slash: init CRC16-CCITT-FALSE
}

#ifdef __cplusplus
}
#endif
