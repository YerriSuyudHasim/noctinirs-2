#ifndef NN2_ADC_FRAME_H
#define NN2_ADC_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One raw ADC record. Chip product names stay out of core types.
   The 24-bit status + 4 channels + CRC layout is applied by the adapter. */
typedef struct {
    uint32_t status_word;
    int32_t channel[4];
    uint16_t crc_received;
    uint16_t crc_calculated;
    bool crc_ok;
    uint64_t timestamp_us;
} nn2_adc_raw_frame_t;

typedef enum {
    NN2_ADC_CRC_CCITT = 1,
    NN2_ADC_CRC_ANSI = 2
} nn2_adc_crc_kind_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_ADC_FRAME_H */
