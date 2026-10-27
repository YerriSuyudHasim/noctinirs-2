#ifndef NN2_CORE_OPTICAL_H
#define NN2_CORE_OPTICAL_H
#include "nn2_core_types.h"
#define NN2_OPTICAL_SLOTS 20u
#define NN2_OPTICAL_DEFAULT_FS 5u
#define NN2_OPTICAL_MAX_FS 10u
#define NN2_OPTICAL_BRINGUP_MA 30u
/* Fs is a complete 20-slot frame, not the ADC rate or individual edge rate. */
static inline bool nn2_core_optical_fs_valid(uint8_t fs) {
    return fs == 1 || fs == 2 || fs == 4 || fs == 5 || fs == 8 || fs == 10;
}
static inline uint32_t nn2_core_optical_half_slot_us(uint8_t fs) {
    return nn2_core_optical_fs_valid(fs) ? 1000000u / (40u * fs) : 0;
}
#endif
