/* Logger port (ADR-0001 3.6 "log"). Task context only, never ISR.
 * msg is borrowed for the duration of the call. May be NULL-function (logging off). */
#ifndef NN2_PORT_LOG_H
#define NN2_PORT_LOG_H

#include "nn2_core_types.h"

typedef enum {
    NN2_LOG_ERROR = 0,
    NN2_LOG_WARN,
    NN2_LOG_INFO,
    NN2_LOG_DEBUG
} nn2_log_level_t;

typedef struct {
    void (*write)(void *ctx, nn2_log_level_t level, const char *msg);
    void *ctx;
} nn2_port_log_t;

#endif /* NN2_PORT_LOG_H */
