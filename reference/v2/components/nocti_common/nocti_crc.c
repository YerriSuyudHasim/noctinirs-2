// ============================================================
// File: components/nocti_common/nocti_crc.c
// ============================================================
//
// Implementasi CRC16-CCITT-FALSE
// - poly: 0x1021
// - init: 0xFFFF
// - refin: false
// - refout: false
// - xorout: 0x0000
//
// Catatan
// - Implementasi bitwise, ringan dan portable
// - Cukup cepat untuk payload BLE kita yang relatif kecil

#include "nocti_crc.h"

static uint16_t crc16_step(uint16_t crc, uint8_t data)
{
    // slash: masukkan byte ke MSB
    crc ^= (uint16_t)data << 8;

    // slash: proses 8 bit
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000u) {
            crc = (uint16_t)((crc << 1) ^ 0x1021u);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t nocti_crc16_ccitt_false(const void *data, size_t len)
{
    return nocti_crc16_ccitt_false_update((uint16_t)0xFFFFu, data, len);
}

uint16_t nocti_crc16_ccitt_false_update(uint16_t crc, const void *data, size_t len)
{
    if (!data || len == 0) {
        return crc; // slash: tidak ada data, CRC tidak berubah
    }

    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc = crc16_step(crc, p[i]);
    }
    return crc;
}
