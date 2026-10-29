#include "nocti_v4_common.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "esp_task_wdt.h"

const char *nocti_v4_err_name(esp_err_t err)
{
    return esp_err_to_name(err);
}

bool nocti_v4_parse_u32(const char *text, uint32_t *out_value)
{
    if (text == NULL || out_value == NULL || *text == '\0' || isspace((unsigned char)*text)) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

void nocti_v4_task_wdt_subscribe(void)
{
    /* The startup path initializes TWDT when CONFIG_ESP_TASK_WDT_INIT is set.
     * A second subscribe of this task returns an error and is ignored. */
    (void)esp_task_wdt_add(NULL);
}

void nocti_v4_task_wdt_kick(void)
{
    (void)esp_task_wdt_reset();
}
