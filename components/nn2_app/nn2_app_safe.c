#include "nn2_app_safe.h"

bool nn2_app_adc_hold_required(nn2_app_run_state_t state)
{
    return state == NN2_APP_BOOT || state == NN2_APP_FAULTED ||
           state == NN2_APP_SAFE_OFF;
}
