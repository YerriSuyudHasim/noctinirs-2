#ifndef NN2_CRC16_H
#define NN2_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC-16, seed 0xFFFF, non-reflected, xorout 0.
   CCITT polynomial x^16+x^12+x^5+1 (0x1021).
   ANSI polynomial x^16+x^15+x^2+1 (0x8005).
   Which polynomial the ADS131M04 frame uses is selected by the init
   config (MODE.CRC_TYPE). That choice is pending Agent 04. */
uint16_t nn2_crc16_ccitt(const uint8_t *data, size_t length);
uint16_t nn2_crc16_ansi(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* NN2_CRC16_H */
