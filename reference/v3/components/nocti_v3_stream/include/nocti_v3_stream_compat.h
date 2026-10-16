#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NoctiNIRSv2(16) stream-v1 envelope. All integer payload fields are sent in
 * native ESP32 little-endian form, exactly as in v16. CRC16 bytes are appended
 * MSB first after header+payload. */
#define NOCTI_V3_STREAM_MAGIC                 0xA5u
#define NOCTI_V3_STREAM_FORMAT_VERSION        0x01u
#define NOCTI_V3_STREAM_HEADER_BYTES          12u
#define NOCTI_V3_STREAM_CRC_BYTES             2u
#define NOCTI_V3_STREAM_MAX_PAYLOAD_BYTES     512u
#define NOCTI_V3_STREAM_MAX_FRAME_BYTES       \
    (NOCTI_V3_STREAM_HEADER_BYTES + NOCTI_V3_STREAM_MAX_PAYLOAD_BYTES + \
     NOCTI_V3_STREAM_CRC_BYTES)

typedef enum {
    NOCTI_V3_REC_INVALID = 0,
    NOCTI_V3_REC_STATUS = 1,
    NOCTI_V3_REC_EVENT = 2,
    NOCTI_V3_REC_TIME64 = 3,
    NOCTI_V3_REC_FNIRS = 4,
    NOCTI_V3_REC_BIOPOT = 5,
    NOCTI_V3_REC_FNIRS_CFG = 6,
    NOCTI_V3_REC_FNIRS_FRAME = 7,
    NOCTI_V3_REC_SESSION = 8,
    NOCTI_V3_REC_BIOPOT_CFG = 9,
    NOCTI_V3_REC_BIOPOT_FRAME = 10,
} nocti_v3_record_type_t;

typedef enum {
    NOCTI_V3_STREAM_FLAG_NONE = 0x00,
    NOCTI_V3_STREAM_FLAG_HAS_CRC16 = 0x01,
} nocti_v3_stream_flags_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint16_t sequence;
    uint32_t relative_timestamp_us;
    uint16_t payload_length;
} nocti_v3_stream_header_v1_t;

#define NOCTI_V3_PAYLOAD_SESSION_V1_VERSION 1u
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t reserved[3];
    uint32_t session_id;
    uint64_t t0_us;
} nocti_v3_payload_session_v1_t;

#define NOCTI_V3_PAYLOAD_FNIRS_CFG_V2_VERSION 2u
#define NOCTI_V3_FNIRS_MAP_LEDID2_WL 0u
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t map_mode;
    uint8_t frame_fs_hz;
    uint8_t slot_count;
    uint8_t k_on;
    uint8_t k_off;
    uint8_t current_735_mA;
    uint8_t current_850_mA;
    uint16_t on_settle_us;
    uint16_t on_window_us;
    uint16_t off_min_settle_us;
    uint16_t slot_period_us;
} nocti_v3_payload_fnirs_cfg_v2_t;

#define NOCTI_V3_PAYLOAD_FNIRS_FRAME_V2_VERSION 2u
typedef enum {
    NOCTI_V3_FNIRS_FRAME_F_NONE = 0x00,
    NOCTI_V3_FNIRS_FRAME_F_PARTIAL = 0x01,
    NOCTI_V3_FNIRS_FRAME_F_LED_OFF = 0x02,
    NOCTI_V3_FNIRS_FRAME_F_PAIRED = 0x04,
} nocti_v3_fnirs_frame_flags_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t flags;
    uint16_t frame_id;
    uint32_t valid_mask;
    uint32_t underk_mask;
    uint32_t crc_mask;
    int32_t avg[20][4];
    uint8_t n_used[20];
} nocti_v3_payload_fnirs_frame_v2_t;

_Static_assert(sizeof(nocti_v3_stream_header_v1_t) ==
                   NOCTI_V3_STREAM_HEADER_BYTES,
               "stream-v1 envelope must remain 12 bytes");
_Static_assert(sizeof(nocti_v3_payload_session_v1_t) == 16u,
               "session-v1 payload layout changed");
_Static_assert(sizeof(nocti_v3_payload_fnirs_cfg_v2_t) == 16u,
               "fNIRS cfg-v2 payload layout changed");
_Static_assert(sizeof(nocti_v3_payload_fnirs_frame_v2_t) == 356u,
               "fNIRS frame-v2 payload layout changed");
_Static_assert(sizeof(nocti_v3_payload_fnirs_frame_v2_t) <=
                   NOCTI_V3_STREAM_MAX_PAYLOAD_BYTES,
               "fNIRS frame-v2 must fit stream-v1 payload");

typedef struct {
    uint16_t sequence;
    uint32_t session_id;
    uint64_t t0_us;
} nocti_v3_stream_encoder_t;

uint16_t nocti_v3_stream_crc16_ccitt_false(const uint8_t *data, size_t length);
size_t nocti_v3_stream_build_record(nocti_v3_stream_encoder_t *encoder,
                                    nocti_v3_record_type_t type,
                                    uint64_t timestamp_us,
                                    const void *payload,
                                    size_t payload_length,
                                    uint8_t *output,
                                    size_t output_capacity);

#ifdef __cplusplus
}
#endif
