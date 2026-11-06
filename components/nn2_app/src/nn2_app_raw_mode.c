#include "nn2_app_raw_mode.h"
nn2_status_t nn2_app_raw_mode_set(nn2_raw_buffer_t **buffer,
    const nn2_port_raw_memory_t *port, bool engineering)
{
    if (!buffer || !port || !port->allocate || !port->release || !port->bind)
        return NN2_ERR_INVALID_ARG;
    if (engineering) {
        if (*buffer) return NN2_OK;
        nn2_raw_buffer_t *candidate = port->allocate(port->ctx, sizeof(*candidate));
        if (!candidate) return NN2_ERR_FULL;
        nn2_raw_buffer_reset(candidate);
        nn2_status_t result = port->bind(port->ctx, candidate);
        if (result != NN2_OK) {
            port->release(port->ctx, candidate);
            return result;
        }
        *buffer = candidate;
        return NN2_OK;
    }
    if (!*buffer) return NN2_OK;
    nn2_status_t result = port->bind(port->ctx, NULL);
    if (result != NN2_OK) return result; /* Never free a bound/live buffer. */
    nn2_raw_buffer_t *old = *buffer;
    *buffer = NULL;
    port->release(port->ctx, old);
    return NN2_OK;
}
