#include "nn2_crc16.h"

static uint16_t nn2_crc16(const uint8_t *data, size_t length, uint16_t poly)
{
    uint16_t crc = 0xFFFFu;
    size_t i;

    if (data == 0 && length != 0u) {
        return 0u;
    }
    for (i = 0u; i < length; i++) {
        uint8_t bit;
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ poly);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t nn2_crc16_ccitt(const uint8_t *data, size_t length)
{
    return nn2_crc16(data, length, 0x1021u);
}

uint16_t nn2_crc16_ansi(const uint8_t *data, size_t length)
{
    return nn2_crc16(data, length, 0x8005u);
}
