#ifndef NN2_ADS131_BUILD_GUARD_H
#define NN2_ADS131_BUILD_GUARD_H
#include "sdkconfig.h"
#if CONFIG_SPIRAM || CONFIG_ESPTOOLPY_OCT_FLASH || CONFIG_SPIRAM_MODE_OCT
#error ADS131 diagnostic requires PSRAM and octal flash disabled
#endif
#endif
