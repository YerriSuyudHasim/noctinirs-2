#ifndef NN2_STATUS_H
#define NN2_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Core status codes. Adapters translate platform errors into these values.
   NN2_OK is zero. These codes do not carry ESP-IDF or FreeRTOS values. */
typedef enum {
    NN2_OK = 0,
    NN2_ERR_INVALID_ARG = 1,
    NN2_ERR_MISSING = 2,
    NN2_ERR_INVALID = 3,
    NN2_ERR_NVS_CORRUPT = 4,
    NN2_ERR_AUTH_FAIL = 5,
    NN2_ERR_TIMEOUT = 6,
    NN2_ERR_CANCELLED = 7,
    NN2_ERR_NO_MEM = 8,
    NN2_ERR_BUSY = 9,
    NN2_ERR_IO = 10,
    NN2_ERR_OVERFLOW = 11
} nn2_status_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_STATUS_H */
