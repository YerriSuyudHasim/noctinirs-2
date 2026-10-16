#include "nn2_core_types.h"

const char *nn2_core_status_str(nn2_status_t status)
{
    switch (status) {
    case NN2_OK: return "OK";
    case NN2_ERR_INVALID_ARG: return "INVALID_ARG";
    case NN2_ERR_INVALID_STATE: return "INVALID_STATE";
    case NN2_ERR_FULL: return "FULL";
    case NN2_ERR_EMPTY: return "EMPTY";
    case NN2_ERR_IO: return "IO";
    case NN2_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
    case NN2_ERR_TIMEOUT: return "TIMEOUT";
    default: return "UNKNOWN";
    }
}

const char *nn2_core_sample_src_str(nn2_sample_src_t src)
{
    switch (src) {
    case NN2_SAMPLE_SRC_UNKNOWN: return "UNKNOWN";
    case NN2_SAMPLE_SRC_FAKE: return "FAKE";
    case NN2_SAMPLE_SRC_ADC: return "ADC";
    default: return "INVALID";
    }
}
