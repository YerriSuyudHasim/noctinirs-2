#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define NOCTI_V4_FW_NAME "NoctiNIRS V4"
#define NOCTI_V4_FW_VERSION "4.0.0-v4-7-2-idf-5-5-5-patch"
#define NOCTI_V4_BUILD_PHASE "V4-1_SAFE_BOOTSTRAP+V4-2_SYNC_PCA_GOLDEN_PATH+V4-3_ADS_DARK+V4-4_ASYNC_PARITY+V4-5_TIMING_SCHEDULER+V4-6_HARDWARE_VERIFIED+V4-7_2_WIFI_RT_HANDOFF_REPAIR+IDF_5_5_5_PATCH"
#define NOCTI_V4_BUILD_ID "V4-P7.2-IDF5.5.5-PATCH"

const char *nocti_v4_err_name(esp_err_t err);
bool nocti_v4_parse_u32(const char *text, uint32_t *out_value);
