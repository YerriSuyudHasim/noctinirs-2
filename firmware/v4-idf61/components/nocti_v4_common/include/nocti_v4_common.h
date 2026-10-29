#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define NOCTI_V4_FW_NAME "NoctiNIRS V4 IDF61 migration"
#define NOCTI_V4_FW_VERSION "4.0.0-idf61-migration-unqualified"
#define NOCTI_V4_BUILD_PHASE "V4_SOURCE_PORT_IDF61_UNBUILT_UNQUALIFIED"
#define NOCTI_V4_BUILD_ID "V4-IDF61-SOURCE-MIGRATION"

const char *nocti_v4_err_name(esp_err_t err);
bool nocti_v4_parse_u32(const char *text, uint32_t *out_value);
/* Subscribe and kick the calling task. Idle loops must wake inside the TWDT window. */
void nocti_v4_task_wdt_subscribe(void);
void nocti_v4_task_wdt_kick(void);
