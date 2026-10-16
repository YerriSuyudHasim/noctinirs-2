#include "nocti_v4_health.h"

#include <string.h>

void nocti_v4_health_reset(nocti_v4_health_t *health)
{
    if (health != NULL) {
        memset(health, 0, sizeof(*health));
    }
}
