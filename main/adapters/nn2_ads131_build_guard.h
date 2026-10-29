#ifndef NN2_ADS131_BUILD_GUARD_H
#define NN2_ADS131_BUILD_GUARD_H
#include "sdkconfig.h"
#if CONFIG_SPIRAM || CONFIG_ESPTOOLPY_OCT_FLASH || CONFIG_SPIRAM_MODE_OCT
#error ADS131 diagnostic requires PSRAM and octal flash disabled
#endif
#if CONFIG_NN2_OPTICAL_ACQUISITION && (!CONFIG_ESPTOOLPY_FLASHSIZE_16MB || !CONFIG_PARTITION_TABLE_CUSTOM)
#error ADS131 acquisition requires 16 MiB flash and the custom acquisition partition table
#endif
#endif
