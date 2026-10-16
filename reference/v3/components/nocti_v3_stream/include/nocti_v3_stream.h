#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v3_adc.h"
#include "nocti_v3_board.h"
#include "nocti_v3_health.h"
#include "nocti_v3_led.h"
#include "nocti_v3_optical.h"
#include "nocti_v3_stream_compat.h"
#include "nocti_v3_timebase.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOCTI_V3_COMMAND_MAX_BYTES 192u
#define NOCTI_V3_REPLY_MAX_BYTES   NOCTI_V3_SLOT_DUMP_MAX_BYTES

typedef struct {
    nocti_v3_board_t *board;
    nocti_v3_timebase_t *timebase;
    nocti_v3_adc_t *adc;
    nocti_v3_led_t *led;
    nocti_v3_optical_t *optical;
    nocti_v3_health_t *health;
    nocti_v3_mode_t preferred_mode;
} nocti_v3_stream_t;

esp_err_t nocti_v3_stream_init(nocti_v3_stream_t *stream,
                               nocti_v3_board_t *board,
                               nocti_v3_timebase_t *timebase,
                               nocti_v3_adc_t *adc,
                               nocti_v3_led_t *led,
                               nocti_v3_optical_t *optical,
                               nocti_v3_health_t *health);
esp_err_t nocti_v3_stream_handle_command(nocti_v3_stream_t *stream,
                                         const uint8_t *input,
                                         size_t input_length,
                                         char *reply,
                                         size_t reply_capacity,
                                         size_t *out_reply_length);

#ifdef __cplusplus
}
#endif
