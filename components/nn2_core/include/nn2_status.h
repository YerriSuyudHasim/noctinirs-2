#ifndef NN2_STATUS_H
#define NN2_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Core status codes. NN2_OK is zero. Adapters translate platform errors
   into these values. No ESP-IDF or FreeRTOS codes cross this header. */
typedef enum {
    NN2_OK = 0,
    NN2_ERR_INVALID_ARG = 1,
    NN2_ERR_TIMEOUT = 2,
    NN2_ERR_CRC = 3,
    NN2_ERR_FAULT = 4,
    NN2_ERR_OVERFLOW = 5,
    NN2_ERR_IO = 6,
    NN2_ERR_STATE = 7,
    NN2_ERR_MISSING = 8
} nn2_status_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_STATUS_H */
