#ifndef NN2_CORE_OPTICAL_H
#define NN2_CORE_OPTICAL_H
#include "nn2_core_types.h"
#define NN2_OPTICAL_SLOTS 20u
#define NN2_OPTICAL_DEFAULT_FS 5u
#define NN2_OPTICAL_MIN_FS 2u
#define NN2_OPTICAL_MAX_FS 10u
#define NN2_OPTICAL_DEFAULT_MA 30
#define NN2_OPTICAL_MAX_MA 50
_Static_assert(NN2_OPTICAL_DEFAULT_MA <= NN2_OPTICAL_MAX_MA,
               "default optical current must not exceed maximum");
/* Compatibility for the unchanged 30 mA bring-up/advisory profile. */
#define NN2_OPTICAL_BRINGUP_MA NN2_OPTICAL_DEFAULT_MA
/* Fs is a complete 20-slot frame, not the ADC rate or individual edge rate. */
static inline bool nn2_core_optical_fs_valid(uint8_t fs) {
    return fs >= NN2_OPTICAL_MIN_FS && fs <= NN2_OPTICAL_MAX_FS;
}
/* Timer resolution is1 us: floor each half-slot; nominal frame error <40 us. */
static inline uint32_t nn2_core_optical_half_slot_us(uint8_t fs) {
    return nn2_core_optical_fs_valid(fs) ? 1000000u / (40u * fs) : 0;
}
#endif
