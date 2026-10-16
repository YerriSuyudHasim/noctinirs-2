#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nocti_v3_stream.h"

int main(void)
{
    static const uint8_t check[] = "123456789";
    assert(nocti_v3_stream_crc16_ccitt_false(check, 9u) == 0x29B1u);

    nocti_v3_stream_encoder_t encoder = {
        .sequence = 0x1234u,
        .session_id = 7u,
        .t0_us = 1000000u,
    };
    nocti_v3_payload_session_v1_t payload = {
        .version = NOCTI_V3_PAYLOAD_SESSION_V1_VERSION,
        .session_id = 7u,
        .t0_us = 1000000u,
    };
    uint8_t bytes[NOCTI_V3_STREAM_MAX_FRAME_BYTES];
    const size_t count = nocti_v3_stream_build_record(
        &encoder, NOCTI_V3_REC_SESSION, 1000125u, &payload, sizeof(payload),
        bytes, sizeof(bytes));
    assert(count == NOCTI_V3_STREAM_HEADER_BYTES + sizeof(payload) +
                    NOCTI_V3_STREAM_CRC_BYTES);
    nocti_v3_stream_header_v1_t header;
    memcpy(&header, bytes, sizeof(header));
    assert(header.magic == 0xA5u);
    assert(header.version == 1u);
    assert(header.type == NOCTI_V3_REC_SESSION);
    assert(header.flags == NOCTI_V3_STREAM_FLAG_HAS_CRC16);
    assert(header.sequence == 0x1234u);
    assert(header.relative_timestamp_us == 125u);
    assert(header.payload_length == sizeof(payload));
    const uint16_t crc = nocti_v3_stream_crc16_ccitt_false(bytes, count - 2u);
    assert(bytes[count - 2u] == (uint8_t)(crc >> 8));
    assert(bytes[count - 1u] == (uint8_t)crc);
    assert(encoder.sequence == 0x1235u);

    assert(sizeof(nocti_v3_payload_fnirs_cfg_v2_t) == 16u);
    assert(sizeof(nocti_v3_payload_fnirs_frame_v2_t) == 356u);
    puts("PASS stream-v1 CRC/layout/sequence");
    return 0;
}
